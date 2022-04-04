#ifndef BLAZE_BIN_H
#define BLAZE_BIN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include "Util.h"
#include "Type.h"
#include "atomics.h"
#include "Param.h"
#include "blockingconcurrentqueue.h"
#include "concurrentqueue.h"

#define __int64 long long

using namespace std;

namespace blaze {

union converter { uint32_t i; float f; };

struct Bin {
    int             _id;
    uint64_t        _size;
    uint64_t*       _bin;
    int             _idx;
    atomic<int>     _state; // 0: binning, 1: accumulate

    Bin(int id, uint64_t size): _id(id), _size(size) {
        size_t alloc_bin_size = ALIGN_UPTO(_size * 8, PAGE_SIZE);
        int ret = posix_memalign((void **)&_bin, PAGE_SIZE, alloc_bin_size);
        assert(ret == 0);
        memset(_bin, 0, alloc_bin_size);
        _idx = 0;
        _state = 0;
    }

    ~Bin() {
        free(_bin);
    }

    void mark_binning() {
        atomic_store(&_state, 0);
    }

    void mark_accumulate() {
        atomic_store(&_state, 1);
    }

    bool is_accumulate() {
        return atomic_load(&_state);
    }

    void make_empty() {
        _idx = 0;
    }

    bool empty() {
        return _idx == 0;
    }

    void reset() {
        make_empty();
        mark_binning();
    }

    uint64_t* get_bin() const {
        return _bin;
    }

    int get_bin_id() const {
        return _id;
    }

    int get_idx() const {
        return _idx;
    }

    uint64_t get_size() const {
        return _size;
    }
};


struct FullBins {
    moodycamel::ConcurrentQueue<Bin*> _queue;

    FullBins() {}

    ~FullBins() {}

    void push(Bin *bin) {
        _queue.enqueue(bin);
    }

    Bin* pop() {
        Bin *bin;
        if (!_queue.try_dequeue(bin))
            return nullptr;
        return bin;
    }
};

// Single bin
struct BinPair {
    int             _id;
    atomic<int>     _active;    // 0 <-> 1
    Bin*            _pair[2];
    mutex           _lock;
    FullBins*       _full_bins;

    BinPair(int id, uint64_t size, FullBins* fbins)
        : _id(id), _active(0), _full_bins(fbins)
    {
        _pair[0] = new Bin(id, size / 2);
        _pair[1] = new Bin(id, size / 2);
    }

    ~BinPair() {
        delete _pair[0];
        delete _pair[1];
    }

    // concurrent trial of this maybe possible
    // so only one trial should succeed while others fail
    // non-active bin should be in the accumulate state
    void switch_bin(int count) {
        const lock_guard<mutex> lock(_lock);

        int me = atomic_load(&_active);
        Bin *mybin = _pair[me];

        // do nothing if the current bin is not full
        // which means the switching is already done by another thread
        if (mybin->_idx + count < mybin->_size)
            return;

        int other = me ? 0 : 1;
        Bin *otherbin = _pair[other];

        // wait until the other bin is available
        while (otherbin->is_accumulate()) {}

        // send my bin to accumulator
        mybin->mark_accumulate();
        _full_bins->push(mybin);

        // switch bins
        atomic_store(&_active, other);
    }

    // blocking until it gets a valid tail
    // this might update which bin is active
    void get_tail(int count, int *active, int *tail) {
        int me, cur_tail, new_tail;
        Bin *mybin;

        while (1) {
            me = atomic_load(&_active);
            mybin = _pair[me];
            cur_tail = mybin->_idx;
            new_tail = cur_tail + count;

            // current bin is full, try the next bin
            if (new_tail > mybin->_size) {
                switch_bin(count);

            } else if (compare_and_swap(mybin->_idx, cur_tail, new_tail)) {
                break;
            }
        }

        *active = me;
        *tail = cur_tail;
    }

    bool append(uint64_t *src, int count) {
        int active, tail;
        get_tail(count, &active, &tail);
        uint64_t *dst = _pair[active]->_bin + tail;
        memcpy(dst, src, count * 8);

        return true;
    }

    void flush() {
        Bin *bin;
        for (int i = 0; i < 2; i++) {
            bin = _pair[i];
            if (!bin->is_accumulate() && !bin->empty()) {
                _full_bins->push(bin);
            }
        }
    }

    void reset() {
        _pair[0]->reset();
        _pair[1]->reset();
        atomic_store(&_active, 0);
    }
};

// multiple bins
struct Bins {
    // config params
    unsigned            _nthreads;
    int                 _bin_count;
    int                 _bin_buf_size;
    float               _binning_ratio;
    uint64_t            _bin_size;
    int                 _bin_shift;
    // data structures
    uint64_t**          _buf;
    int**               _buf_idx;
    struct BinPair**    _bin_pairs;
    FullBins            _full_bins;

    template <typename Gr>
    Bins(Gr& graph, unsigned nthreads, uint64_t bins_size,
         int bin_count, int bin_buf_size, float binning_ratio)
    :   _nthreads(nthreads),
        _bin_count(bin_count),
        _bin_buf_size(bin_buf_size),
        _binning_ratio(binning_ratio)
    {
        uint64_t n = graph.NumberOfNodes();
        init_buffer();
        init_bin(n - 1, bins_size);
        print();
    }

    ~Bins() {
        deinit_buffer();
        deinit_bin();
    }

    void init_buffer() {
        int ret;
        int buf_size = ALIGN_UPTO(_bin_count * _bin_buf_size * 8, PAGE_SIZE);

        _buf = new uint64_t * [_nthreads];
        _buf_idx = new int * [_nthreads];

        for (unsigned i = 0; i < _nthreads; i++) {
            ret = posix_memalign((void **)&_buf[i], PAGE_SIZE, buf_size);
            assert(ret == 0);
            memset(_buf[i], 0, buf_size);

            _buf_idx[i] = (int *)calloc(_bin_count, sizeof(int));
            memset(_buf_idx[i], 0, _bin_count * sizeof(int));
        }
    }

    void deinit_buffer() {
        for (unsigned i = 0; i < _nthreads; i++) {
            free(_buf[i]);
            free(_buf_idx[i]);
        }
        delete [] _buf;
        delete [] _buf_idx;
    }

    void init_bin(uint64_t max_num, uint64_t size) {
        _bin_size = size / _bin_count / 8;

        _bin_pairs = new BinPair * [_bin_count];
        for (int i = 0; i < _bin_count; i++) {
            _bin_pairs[i] = new BinPair(i, _bin_size, &_full_bins);
        }

        // calculate shift size for binning
        uint64_t tmp = max_num;
        int msb = 0;
        while (msb < 32) {
            if (tmp == 0) break;
            tmp >>= 1;
            msb++;
        }
        int bin_count_in_bits = (int)log2((float)_bin_count);
        _bin_shift = msb - bin_count_in_bits;
    }

    void deinit_bin() {
        for (int i = 0; i < _bin_count; i++) {
            delete _bin_pairs[i];
        }
        delete [] _bin_pairs;
    }

    void print() {
        printf("bin width: %u kB\n", (1U << (_bin_shift - 10)));
        printf("bin size: %lu MB = %d * %lu kB bins\n",
                (_bin_size * _bin_count * 8) >> 20,
                _bin_count,
                (_bin_size * 8) >> 10);
        size_t buf_size = ALIGN_UPTO(_bin_count * _bin_buf_size * 8, PAGE_SIZE);
        printf("buffer size: %lu KB\n", (buf_size * _nthreads) >> 10);
    }

    Bin* get_full_bin() {
        return _full_bins.pop();
    }

    template <typename T>
    inline __attribute__((always_inline))
    void append(unsigned tid, uint32_t x1, T x2) {
        // calculate bin index
        int bid = x1 >> _bin_shift;

        // get the current buffer
        uint64_t * const cur_buf = _buf[tid] + bid * _bin_buf_size;
        int buf_idx = _buf_idx[tid][bid];

        // flush the buffer if full
        if (buf_idx == _bin_buf_size) {
            _bin_pairs[bid]->append(cur_buf, _bin_buf_size);

            // reset the buffer index
            buf_idx = 0;
        }

        // insert entry
        uint64_t entry = ((uint64_t)x1 << 32) + *(uint32_t *)&x2;
        cur_buf[buf_idx++] = entry;

        // increment the buffer index
        _buf_idx[tid][bid] = buf_idx;
    }

    inline __attribute__((always_inline))
    void flush(unsigned tid) {
        for (int bid = 0; bid < _bin_count; bid++) {
            uint64_t * const cur_buf = _buf[tid] + bid * _bin_buf_size;
            int buf_idx = _buf_idx[tid][bid];
            if (buf_idx > 0)
                _bin_pairs[bid]->append(cur_buf, buf_idx);
        }
    }

    inline __attribute__((always_inline))
    void flush_all() {
        for (int bid = 0; bid < _bin_count; bid++) {
            _bin_pairs[bid]->flush();
        }
    }

    void reset() {
        // reset buffer
        for (int tid = 0; tid < _nthreads; tid++) {
            for (int bid = 0; bid < _bin_count; bid++) {
                _buf_idx[tid][bid] = 0;
            }
        }
        // reset bins
        for (int bid = 0; bid < _bin_count; bid++) {
            _bin_pairs[bid]->reset();
        }
        // reset full bins
        //delete _full_bins;
        //_full_bins = new FullBins();
    }

    uint64_t get_bin_size() const {
        return _bin_size;
    }

    float get_binning_ratio() const {
        return _binning_ratio;
    }

    void prefetch_bin(char *base, int bid) {
        uint32_t bin_width = 1U << _bin_shift;
        char *beg = base + bid * bin_width;
        //printf("prefetch: base: %p, bin: %d, width: %u, range: [%p, %p]\n",
        //        base, bid, bin_width, beg, beg + bin_width);
        prefetch_range(beg, bin_width);
    }
};

} // namespace blaze

#endif // BLAZE_BIN_H
