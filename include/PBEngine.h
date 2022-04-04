#ifndef BLAZE_PB_ENGINE_H
#define BLAZE_PB_ENGINE_H

#include "galois/Galois.h"
#include "Type.h"
#include "Graph.h"
#include "ScatterWorker.h"
#include "GatherWorker.h"
#include "Synchronization.h"
#include "Bin.h"

namespace blaze {

class PBEngine {
 public:
    PBEngine(int start_tid,
             int num_scatter_workers,
             int num_gather_workers,
             std::vector<MPMCQueue<IoItem*>*>& fetch_pages)
        :   _start_tid(start_tid),
            _in_frontier(nullptr),
            _out_frontier(nullptr),
            _thread_pool(galois::substrate::getThreadPool())
    {
        // binning workers
        for (int i = 0; i < num_scatter_workers; ++i) {
            _scatter_workers.push_back(new ScatterWorker(i, fetch_pages));
        }

        // accumulate workers
        for (int i = 0; i < num_gather_workers; ++i) {
            _gather_workers.push_back(new GatherWorker(i));
        }
    }

    ~PBEngine() {
    }

    template <typename Gr>
    void setFrontier(Gr& graph, Worklist<VID>* frontier, FLAGS flags) {
        _in_frontier = frontier;

        // give in/out frontiers to workers
        for (auto worker : _scatter_workers) {
            worker->setFrontier(_in_frontier);
        }

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

        for (auto worker : _gather_workers) {
            worker->setFrontier(_out_frontier);
        }
    }

    template <typename Gr, typename Func>
    void start(Gr& graph, Func& func, Synchronization& sync) {
        _time_start = std::chrono::steady_clock::now();

        // start binning workers
        std::vector<std::function<void(void)>> functions;
        for (auto worker : _scatter_workers) {
            std::function<void(void)> f = std::bind(&ScatterWorker::run<Gr, Func>,
                                                    worker,
                                                    std::ref(graph),
                                                    std::ref(func), 
                                                    std::ref(sync));
            functions.push_back(f);
        }
        _thread_pool.fork(_start_tid, _scatter_workers.size(), functions);

        // start accumulate workers
        functions.clear();
        for (auto worker : _gather_workers) {
            std::function<void(void)> f = std::bind(&GatherWorker::run<Gr, Func>,
                                                    worker,
                                                    std::ref(graph),
                                                    std::ref(func), 
                                                    std::ref(sync));
            functions.push_back(f);
        }
        _thread_pool.fork(_start_tid + _scatter_workers.size(), _gather_workers.size(), functions);
    }

    template <typename Gr, typename Func>
    double stop(Gr& graph, Func& func, Synchronization& sync) {
        // join binning workers
        _thread_pool.join(_start_tid);

        func.get_bins()->flush_all();

        sync.mark_binning_done();

        // join accumulate workers
        _thread_pool.join(_start_tid + _scatter_workers.size());

        _time_end = std::chrono::steady_clock::now();
        std::chrono::duration<double> duration = _time_end - _time_start;

        return duration.count();
    }

    int getNumberOfScatterWorkers() const {
        return _scatter_workers.size();
    }

    int getNumberOfGatherWorkers() const {
        return _gather_workers.size();
    }

    Worklist<VID>* getOutFrontier() {
        return _out_frontier;
    }

    double getScatterSkewness() const {
        double min_time, max_time, time, gap;

        min_time = 10000000.0;
        max_time = 0.0;

        for (int i = 0; i < _scatter_workers.size(); i++) {
            time = _scatter_workers[i]->getTime();

            if (time < min_time)
                min_time = time;
            if (time > max_time)
                max_time = time;
        }
        return max_time / min_time;
    }

    double getGatherSkewness() const {
        double min_time, max_time, time, gap;

        min_time = 10000000.0;
        max_time = 0.0;

        for (int i = 0; i < _gather_workers.size(); i++) {
            time = _gather_workers[i]->getTime();

            if (time < min_time)
                min_time = time;
            if (time > max_time)
                max_time = time;
        }
        return max_time / min_time;
    }

    void printStat() const {
        double min_time, max_time, time, gap;

        // scatter
        min_time = 10000000.0;
        max_time = 0.0;

        std::cout << "    scatter: ";
        for (int i = 0; i < _scatter_workers.size(); i++) {
            if (i != 0)
                std::cout << ",";
            time = _scatter_workers[i]->getTime();
            std::cout << std::fixed << std::setprecision(2) << time;

            if (time < min_time)
                min_time = time;
            if (time > max_time)
                max_time = time;
        }
        gap = max_time / min_time;
        std::cout << " (" << std::fixed << std::setprecision(2) << gap << ")";
        std::cout << std::endl;

        // gather
        min_time = 10000000.0;
        max_time = 0.0;

        std::cout << "    gather:  ";
        for (int i = 0; i < _gather_workers.size(); i++) {
            if (i != 0)
                std::cout << ",";
            time = _gather_workers[i]->getTime();
            std::cout << std::fixed << std::setprecision(2) << time;

            if (time < min_time)
                min_time = time;
            if (time > max_time)
                max_time = time;
        }
        gap = max_time / min_time;
        std::cout << " (" << std::fixed << std::setprecision(2) << gap << ")";
        std::cout << std::endl;
    }

 private:
    int                                     _start_tid;
    std::vector<ScatterWorker*>             _scatter_workers;
    std::vector<GatherWorker*>              _gather_workers;
    Worklist<VID>*                          _in_frontier;
    Worklist<VID>*                          _out_frontier;
    galois::substrate::ThreadPool&          _thread_pool;
    std::chrono::time_point<std::chrono::steady_clock>  _time_start;
    std::chrono::time_point<std::chrono::steady_clock>  _time_end;
};

} // namespace blaze

#endif // BLAZE_PB_ENGINE_H
