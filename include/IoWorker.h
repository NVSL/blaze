#ifndef BLAZE_IO_WORKER_H
#define BLAZE_IO_WORKER_H

#include "Type.h"
#include "Graph.h"
#include "galois/Bag.h"
#include "Param.h"
#include "IoSync.h"
#include "Synchronization.h"
#include "AsyncIo.h"
#include "Queue.h"
#include "Param.h"
#include <unordered_set>

namespace blaze {

class Runtime;

class IoWorker {
 public:
    IoWorker(int id,
             uint64_t buffer_size,
             MPMCQueue<IoItem*>* out)
        :   _id(id),
            _buffered_tasks(out),
            _queued(0), _sent(0), _received(0), _requested_all(false),
            _total_bytes_accessed(0), _time(0.0)
    {
        initAsyncIo();
        _num_buffer_pages = (int64_t)buffer_size / PAGE_SIZE;
    }

    ~IoWorker() {
        deinitAsyncIo();
    }

    void initAsyncIo() {
        _ctx = 0;
        int ret = io_setup(IO_QUEUE_DEPTH, &_ctx);
        assert(ret == 0);
        _iocb = (struct iocb*)calloc(IO_QUEUE_DEPTH, sizeof(*_iocb));
        _iocbs = (struct iocb**)calloc(IO_QUEUE_DEPTH, sizeof(*_iocbs));
        _events = (struct io_event*)calloc(IO_QUEUE_DEPTH, sizeof(*_events));
    }

    void deinitAsyncIo() {
        io_destroy(_ctx);
        free(_iocb);
        free(_iocbs);
        free(_events);
    }

    void run(int fd, bool dense_all, Bitmap* page_bitmap, CountableBag<PAGEID>* sparse_page_frontier,
            Synchronization& sync, IoSync& io_sync) {
        _fd = fd;
        sync.set_num_free_pages(_id, _num_buffer_pages);

        if (dense_all) {
            run_dense_all(page_bitmap, sync, io_sync);
        }

        if (sparse_page_frontier) {
            run_sparse(sparse_page_frontier, page_bitmap, sync, io_sync);

        } else {
            run_dense(page_bitmap, sync, io_sync);
        }
    }

    uint64_t getBytesAccessed() const {
        return _total_bytes_accessed;
    }

    void initState() {
        _queued = 0;
        _sent = 0;
        _received = 0;
        _requested_all = false;
        _total_bytes_accessed = 0;
        _time = 0;
    }

 private:
    void run_dense_all(Bitmap* page_bitmap, Synchronization& sync, IoSync& io_sync) {
        IoItem* done_tasks[IO_QUEUE_DEPTH];
        int received;

        PAGEID beg = 0;
        const PAGEID end = page_bitmap->get_size();

        while (!_requested_all || _received < _queued) {
            submitTasks_dense_all(beg, end, sync, io_sync);
            received = receiveTasks(done_tasks);
            dispatchTasks(done_tasks, received);
        }
    }

    void run_dense(Bitmap* page_bitmap, Synchronization& sync, IoSync& io_sync) {
        IoItem* done_tasks[IO_QUEUE_DEPTH];
        int received;

        PAGEID beg = 0;
        const PAGEID end = page_bitmap->get_size();

        while (!_requested_all || _received < _queued) {
            submitTasks_dense(page_bitmap, beg, end, sync, io_sync);
            received = receiveTasks(done_tasks);
            dispatchTasks(done_tasks, received);
        }
    }

    void run_sparse(CountableBag<PAGEID>* sparse_page_frontier, Bitmap* page_bitmap, Synchronization& sync, IoSync& io_sync) {
        IoItem* done_tasks[IO_QUEUE_DEPTH];
        int received;

        auto beg = sparse_page_frontier->begin();
        auto const end = sparse_page_frontier->end();

        while (!_requested_all || _received < _queued) {
            submitTasks_sparse(beg, end, page_bitmap, sync, io_sync);
            received = receiveTasks(done_tasks);
            dispatchTasks(done_tasks, received);
        }
    }

    void submitTasks_dense_all(PAGEID& beg, const PAGEID& end,
                            Synchronization& sync, IoSync& io_sync)
    {
        char* buf;
        off_t offset;
        void* data;

        while (beg < end && (_queued - _sent) < IO_QUEUE_DEPTH) {
            // check continuous pages up to 16kB
            PAGEID page_id = beg;
            uint32_t num_pages = IO_MAX_PAGES_PER_REQ;
            if (beg + num_pages > end)
                num_pages = end - beg;

            // wait until free pages are available
            while (sync.get_num_free_pages(_id) < num_pages) {}
            sync.add_num_free_pages(_id, (int64_t)num_pages * (-1));

            uint32_t len = num_pages * PAGE_SIZE;
            buf = (char*)aligned_alloc(PAGE_SIZE, len);
            offset = (uint64_t)page_id * PAGE_SIZE;
            IoItem* item = new IoItem(_id, page_id, num_pages, buf);
            enqueueRequest(buf, len, offset, item);

            beg += num_pages;
        }

        if (beg >= end) _requested_all = true;

        if (_queued - _sent == 0) return;

        for (size_t i = 0; i < _queued - _sent; i++) {
            _iocbs[i] = &_iocb[(_sent + i) % IO_QUEUE_DEPTH];
        }

        int ret = io_submit(_ctx, _queued - _sent, _iocbs);
        if (ret > 0) {
            _sent += ret;
        }
    }

    void submitTasks_dense(Bitmap* page_bitmap, PAGEID& beg, const PAGEID& end,
                        Synchronization& sync, IoSync& io_sync)
    {
        char* buf;
        off_t offset;
        void* data;

        while (beg < end && (_queued - _sent) < IO_QUEUE_DEPTH) {
            // skip an entire word in bitmap if possible
            // note: this is quite effective to keep IO queue busy
            if (!page_bitmap->get_word(Bitmap::word_offset(beg))) {
                beg = Bitmap::pos_in_next_word(beg);
                continue;
            }

            if (!page_bitmap->get_bit(beg)) {
                beg++;
                continue;

            } else {
                // check continuous pages up to 16kB
                PAGEID page_id = beg;
                uint32_t num_pages = 1;
                while (page_bitmap->get_bit(++beg)
                             && beg < end 
                             && num_pages < IO_MAX_PAGES_PER_REQ)
                {
                    num_pages++;
                }

                // wait until free pages are available
                while (sync.get_num_free_pages(_id) < num_pages) {}
                sync.add_num_free_pages(_id, (int64_t)num_pages * (-1));

                uint32_t len = num_pages * PAGE_SIZE;
                buf = (char*)aligned_alloc(PAGE_SIZE, len);
                offset = (uint64_t)page_id * PAGE_SIZE;
                IoItem* item = new IoItem(_id, page_id, num_pages, buf);
                enqueueRequest(buf, len, offset, item);
            }
        }

        if (beg >= end) _requested_all = true;

        if (_queued - _sent == 0) return;

        for (size_t i = 0; i < _queued - _sent; i++) {
            _iocbs[i] = &_iocb[(_sent + i) % IO_QUEUE_DEPTH];
        }

        int ret = io_submit(_ctx, _queued - _sent, _iocbs);
        if (ret > 0) {
            _sent += ret;
        }
    }

    void submitTasks_sparse(CountableBag<PAGEID>::iterator& beg,
                            const CountableBag<PAGEID>::iterator& end,
                            Bitmap* page_bitmap,
                            Synchronization& sync, IoSync& io_sync)
    {
        char* buf;
        off_t offset;
        void* data;
        PAGEID page_id;

        while (beg != end && (_queued - _sent) < IO_QUEUE_DEPTH) {
            page_id = *beg;

            if (page_bitmap->get_bit(page_id)) {
                beg++;
                continue;
            }

            // wait until free pages are available
            while (sync.get_num_free_pages(_id) < 1) {}
            sync.add_num_free_pages(_id, (int64_t)(-1));

            buf = (char*)aligned_alloc(PAGE_SIZE, PAGE_SIZE);
            offset = (uint64_t)page_id * PAGE_SIZE;
            IoItem* item = new IoItem(_id, page_id, 1, buf);
            enqueueRequest(buf, PAGE_SIZE, offset, item);

            page_bitmap->set_bit(page_id);

            beg++;
        }

        if (beg == end) _requested_all = true;

        if (_queued - _sent == 0) return;

        for (size_t i = 0; i < _queued - _sent; i++) {
            _iocbs[i] = &_iocb[(_sent + i) % IO_QUEUE_DEPTH];
        }

        int ret = io_submit(_ctx, _queued - _sent, _iocbs);
        if (ret > 0) {
            _sent += ret;
        }
    }

    void enqueueRequest(char* buf, size_t len, off_t offset, void* data) {
        uint32_t idx = _queued % IO_QUEUE_DEPTH;
        struct iocb* pIocb = &_iocb[idx];
        memset(pIocb, 0, sizeof(*pIocb));
        pIocb->aio_fildes = _fd;
        pIocb->aio_lio_opcode = IOCB_CMD_PREAD;
        pIocb->aio_buf = (uint64_t)buf;
        pIocb->aio_nbytes = len;
        pIocb->aio_offset = offset;
        pIocb->aio_data = (uint64_t)data;
        _queued++;

        _total_bytes_accessed += len;
    }

    int receiveTasks(IoItem** done_tasks) {
        if (_requested_all && _sent == _received) return 0;

        unsigned min = 0;
        unsigned max = IO_QUEUE_DEPTH;

        int received = io_getevents(_ctx, min, max, _events, NULL);
        assert(received <= max);
        assert(received >= 0);

        for (int i = 0; i < received; i++) {
            assert(_events[i].res > 0);
            auto item = reinterpret_cast<IoItem*>(_events[i].data);
            done_tasks[i] = item;
        }
        _received += received;

        return received;
    }

    void dispatchTasks(IoItem** done_tasks, int received) {
        if (received > 0)
            _buffered_tasks->enqueue_bulk(done_tasks, received);
    }

 protected:
    int                     _id;
    MPMCQueue<IoItem*>*     _buffered_tasks;

    // To control IO
    int                     _fd;
    uint64_t                _queued;
    uint64_t                _sent;
    uint64_t                _received;
    bool                    _requested_all;
    int64_t                 _num_buffer_pages;
    // For statistics
    uint64_t                _total_bytes_accessed;
    double                  _time;

    aio_context_t                       _ctx;
    struct iocb*                        _iocb;
    struct iocb**                       _iocbs;
    struct io_event*                    _events;
};

} // namespace blaze

#endif // BLAZE_IO_WORKER_H
