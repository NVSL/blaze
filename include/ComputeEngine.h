#ifndef AGILE_COMPUTE_ENGINE_H
#define AGILE_COMPUTE_ENGINE_H

#include "galois/Galois.h"
#include "Type.h"
#include "Graph.h"
#include "ComputeWorker.h"
#include "Synchronization.h"
#include "Queue.h"

namespace agile {

class ComputeEngine {
 public:
    ComputeEngine(int start_tid,
                  int num_compute_workers,
                  std::vector<MPMCQueue<IoItem*>*>& fetched_pages)
        :   _start_tid(start_tid),
            _in_frontier(nullptr),
            _out_frontier(nullptr),
            _thread_pool(galois::substrate::getThreadPool())
    {
        for (int i = 0; i < num_compute_workers; i++) {
            _workers.push_back(new ComputeWorker(i, fetched_pages));
        }
    }

    ~ComputeEngine() {
    }

    template <typename Gr>
    void setFrontier(Gr& graph, Worklist<VID>* frontier, FLAGS flags) {
        _in_frontier = frontier;

        // prepare out frontier
        if (should_output(flags)) {
            uint64_t n = graph.NumberOfNodes();
            _out_frontier = new Worklist<VID>(n);
            if (!frontier || frontier->is_dense()) {
                _out_frontier->to_dense();
            }
        } else {
            _out_frontier = nullptr;
        }

        // give in/out frontiers to workers
        for (auto worker : _workers) {
            worker->setFrontiers(_in_frontier, _out_frontier);
        }
    }

    template <typename Gr, typename Func>
    void start(Gr& graph, Func& func, Synchronization& sync) {
        _time_start = std::chrono::steady_clock::now();

        std::vector<std::function<void(void)>> functions;
        for (auto worker : _workers) {
            std::function<void(void)> f = std::bind(&ComputeWorker::run<Gr, Func>,
                                                    worker,
                                                    std::ref(graph),
                                                    std::ref(func),
                                                    std::ref(sync));
            functions.push_back(f);
        }
        _thread_pool.fork(_start_tid, _workers.size(), functions); // run compute workers after 2 * io workers to avoid interference
    }

    template <typename Gr>
    double stop(Gr& graph) {
        _thread_pool.join(_start_tid);

        _time_end = std::chrono::steady_clock::now();
        std::chrono::duration<double> duration = _time_end - _time_start;

        return duration.count();
    }

    Worklist<VID>* getOutFrontier() {
        return _out_frontier;
    }

 private:
    int                                     _start_tid;
    std::vector<ComputeWorker*>             _workers;
    Worklist<VID>*                          _in_frontier;
    std::vector<Bitmap*>                    _out_frontiers;
    Worklist<VID>*                          _out_frontier;
    galois::substrate::ThreadPool&          _thread_pool;
    std::chrono::time_point<std::chrono::steady_clock>  _time_start;
    std::chrono::time_point<std::chrono::steady_clock>  _time_end;
};

} // namespace agile

#endif // AGILE_COMPUTE_ENGINE_H
