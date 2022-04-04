#ifndef BLAZE_IO_ENGINE_H
#define BLAZE_IO_ENGINE_H

#include <vector>
#include "galois/Galois.h"
#include "Type.h"
#include "Graph.h"
#include "IoWorker.h"
#include "IoScheduler.h"
#include "IoSync.h"
#include "Synchronization.h"
#include "Queue.h"
#include "Param.h"

using namespace std;

namespace blaze {

class IoEngine {
 public:
    IoEngine(int num_io_workers,
             int num_compute_workers,
             uint64_t io_buffer_size,
             std::vector<MPMCQueue<IoItem*>*>& out)
        :   _num_workers(num_io_workers),
            _num_compute_workers(num_compute_workers),
            _frontier(nullptr),
            _sparse_page_frontier(nullptr),
            _out(out),
            _thread_pool(galois::substrate::getThreadPool())
    {
        uint64_t io_buf_per_worker = io_buffer_size / num_io_workers;
        for (int i = 0; i < num_io_workers; i++) {
            _workers.push_back(new IoWorker(i, io_buf_per_worker, out[i]));
        }
    }

    ~IoEngine() {
        for (auto worker : _workers) {
            delete worker;
        }
    }

    void setFrontier(Worklist<VID>* frontier, std::vector<CountableBag<PAGEID>*>& sparse_page_frontier) {
        _frontier = frontier;
        _sparse_page_frontier = &sparse_page_frontier;
    }

    int getWorkerTID(int idx) {
        return 1 + _num_compute_workers + idx;
    }

    template <typename Gr>
    double run(Gr& graph, Synchronization& sync, IoSync& io_sync) {
        auto time_start = std::chrono::steady_clock::now();

        bool dense_all = (_frontier == nullptr);

        for (int i = 0; i < _num_workers; i++) {
            int fd = graph.GetEdgeFileDescriptor(i);
            Bitmap* page_bitmap = graph.GetActivatedPages(i);
            function<void(void)> f = bind(&IoWorker::run,
                                        _workers[i],
                                        fd,
                                        dense_all,
                                        page_bitmap,
                                        _sparse_page_frontier->size() ? (*_sparse_page_frontier)[i] : nullptr,
                                        std::ref(sync),
                                        std::ref(io_sync));
            _thread_pool.fork(getWorkerTID(i), f);
        }

        sync.notify_io_start();

        for (int i = 0; i < _num_workers; i++) {
            _thread_pool.join(getWorkerTID(i));
        }

        sync.mark_io_done();

        auto time_end = std::chrono::steady_clock::now();
        std::chrono::duration<double> duration = time_end - time_start;

        return duration.count();
    }

    uint64_t getTotalBytesAccessed() const {
        uint64_t sum = 0;
        for (int i = 0; i < _num_workers; ++i) {
            sum += _workers[i]->getBytesAccessed();
        }
        return sum;
    }

    void initState() {
        for (int i = 0; i < _num_workers; ++i) {
            _workers[i]->initState();
        }
    }

    double getSkewness() const {
        uint64_t min_bytes = UINT64_MAX;
        uint64_t max_bytes = 0;

        for (int i = 0; i < _num_workers; ++i) {
            uint64_t bytes = _workers[i]->getBytesAccessed();

            if (bytes < min_bytes)
                min_bytes = bytes;
            if (bytes > max_bytes)
                max_bytes = bytes;
        }
        return (double)max_bytes / min_bytes;
    }

    void printStat() const {
        uint64_t min_bytes = UINT64_MAX;
        uint64_t max_bytes = 0;
        uint64_t sum_bytes = 0;

        std::cout << "        io:  ";
        for (int i = 0; i < _num_workers; ++i) {
            if (i != 0)
                std::cout << " + ";
            uint64_t bytes = _workers[i]->getBytesAccessed();
            std::cout << bytes;

            if (bytes < min_bytes)
                min_bytes = bytes;
            if (bytes > max_bytes)
                max_bytes = bytes;

            sum_bytes += bytes;
        }
        double gap = (double)max_bytes / min_bytes;
        std::cout << " = " << sum_bytes;
        std::cout << " (" << std::fixed << std::setprecision(2) << gap << ")";
        std::cout << std::endl;
    }

 private:
    int                                 _num_workers;
    int                                 _num_compute_workers;
    vector<IoWorker*>                   _workers;
    IoScheduler                         _scheduler;
    Worklist<VID>*                      _frontier;
    std::vector<CountableBag<PAGEID>*>* _sparse_page_frontier;
    std::vector<MPMCQueue<IoItem*>*>&   _out;
    galois::substrate::ThreadPool&      _thread_pool;
};

} // namespace blaze

#endif // BLAZE_IO_ENGINE_H
