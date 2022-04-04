#ifndef AGILE_PARALLEL_FOR_H
#define AGILE_PARALLEL_FOR_H

#include <vector>
#include <iomanip>
#include <sstream>
#include "galois/Galois.h"
#include "Graph.h"
#include "Type.h"
#include "IoEngine.h"
#include "ComputeEngine.h"
#include "PBEngine.h"
#include "Synchronization.h"
#include "VertexMap.h"
#include "Queue.h"
#include "Param.h"
#include "Runtime.h"

namespace agile {

enum FrontierType {
    EMPTY, DENSE_ALL, DENSE, SPARSE
};

template <typename Gr, typename Func>
class EdgeMapExecutor {
 public:
    EdgeMapExecutor(Gr& graph,
                    Worklist<VID>* frontier,
                    Func&& func,
                    FLAGS flags)
        :   _runtime(Runtime::getRuntimeInstance()),
            _graph(graph),
            _out_frontier(nullptr),
            _fetched_tasks(nullptr),
            _io_engine(_runtime.getIoEngine()),
            _compute_engine(_runtime.getComputeEngine()),
            _pb_engine(_runtime.getPBEngine()),
            _func(func),
            _flags(flags),
            _work_exists(true),
            _num_activated_nodes(0),
            _num_activated_edges(0),
            _frontier_type(EMPTY),
            _io_time(0.0), _compute_time(0.0)
    {
        _runtime.incRound();

        uint64_t n = _graph.NumberOfNodes();
        uint64_t m = _graph.NumberOfEdges();
        _num_activated_edges = frontier ? getNumberOfActiveEdges(frontier) : m;

        // nothing to do
        if (!_num_activated_edges) {
            _work_exists = false;
            _out_frontier = new Worklist<VID>(n);
            return;
        }

        // filter out empty nodes
        filterOutEmptyNodes(frontier);

        _num_activated_nodes = frontier ? frontier->count() : n;

        // switch between dense, sparse
        if (frontier) {
            if (_num_activated_nodes + _num_activated_edges > m * DENSE_THRESHOLD) {
                if (!frontier->is_dense())
                    frontier->to_dense();
            } else {
                if (frontier->is_dense())
                    frontier->to_sparse();
                else
                    frontier->fill_dense();
            }

            if (frontier->is_dense())
                _frontier_type = DENSE;
            else
                _frontier_type = SPARSE;

        } else {
            _frontier_type = DENSE_ALL;
        }

        // build sparse page frontier in sparse case
        if (frontier) {
            if (frontier->is_dense()) {
                buildDensePageFrontier(frontier);
            } else {
                buildSparsePageFrontier(frontier);
            }
        } else {
            buildDensePageFrontier(frontier);
        }

        // set frontier for compute engine

        if (use_prop_blocking(_flags))
            _pb_engine->setFrontier(_graph, frontier, flags);
        else
            _compute_engine->setFrontier(_graph, frontier, flags);

        _io_engine->setFrontier(frontier, _sparse_page_frontier);
    }

    ~EdgeMapExecutor() {
        for (auto frontier : _sparse_page_frontier) {
            delete frontier;
        }
    }

    void run() {
        if (!_work_exists) return;

        int num_disks = _graph.NumberOfDisks();
        Synchronization sync(num_disks);
        IoSync io_sync(num_disks);

        if (use_prop_blocking(_flags)) {
            _pb_engine->start(_graph, _func, sync);
            _io_time = _io_engine->run(_graph, sync, io_sync);
            _compute_time = _pb_engine->stop(_graph, _func, sync);
            _out_frontier = _pb_engine->getOutFrontier();

        } else {
            _compute_engine->start(_graph, _func, sync);
            _io_time = _io_engine->run(_graph, sync, io_sync);
            _compute_time = _compute_engine->stop(_graph);
            _out_frontier = _compute_engine->getOutFrontier();
        }

        _graph.ResetPageActivation();

        if (_work_exists) {
            uint64_t io_bytes = _io_engine->getTotalBytesAccessed();
            _runtime.addAccessedIoBytes(io_bytes);
            _runtime.addAccessedEdges(_num_activated_edges);
            _runtime.addIoTime(_io_time);
        }

        print();

        _io_engine->initState();
    }

    Worklist<VID>* newFrontier() {
        return _out_frontier;
    }

    void print() {
        int round = _runtime.getRound();
        uint64_t io_bytes = 0;
        if (_io_engine)
            io_bytes = _io_engine->getTotalBytesAccessed();

        // frontier type
        string frontier_type_name;
        if (_frontier_type == DENSE_ALL) frontier_type_name = "dense_all";
        else if (_frontier_type == DENSE) frontier_type_name = "dense";
        else if (_frontier_type == SPARSE) frontier_type_name = "sparse";
        else if (_frontier_type == EMPTY) frontier_type_name = "empty";

        // general info
        std::string info;
        char buf[1024];
        sprintf(buf, "# EDGEMAP %4d : %12lu nodes %9s, %12lu edges, %12lu bytes, %8.5f sec, %8.5f sec",
                round, _num_activated_nodes, frontier_type_name.c_str(), _num_activated_edges, io_bytes, _compute_time, _io_time);
        info.append(buf);

        std::cout << info;

        if (_pb_engine) {
            double bin_skew = _pb_engine->getBinningSkewness();
            double acc_skew = _pb_engine->getAccumulateSkewness();

            std::cout << " (bin: ";
            std::cout << std::fixed << std::setprecision(2) << bin_skew;
            std::cout << ", acc: ";
            std::cout << std::fixed << std::setprecision(2) << acc_skew;
            std::cout << ")";
        }

        if (_io_engine) {
            double io_skew = _io_engine->getSkewness();
            std::cout << " (io: ";
            std::cout << std::fixed << std::setprecision(2) << io_skew;
            std::cout << ")";
        }

        std::cout << std::endl;
    }

 private:
    void filterOutEmptyNodes(Worklist<VID>* frontier) {
        if (!frontier)
            return;

        if (frontier->is_dense()) {
            Bitmap *bitmap = frontier->get_dense();
            Bitmap::and_bitmap(bitmap, _graph.GetNonEmptyNodes());

        } else {
            auto old_sparse = frontier->get_sparse();
            auto new_sparse = new CountableBag<VID>();
            galois::do_all(galois::iterate(*old_sparse),
                        [&](const VID& node) {
                            if (_graph.GetDegree(node) > 0)
                                new_sparse->push(node);
                        }, galois::no_stats(), galois::steal());
            delete old_sparse;
            frontier->set_sparse(new_sparse);
        }   
    }

    uint64_t getNumberOfActiveEdges(Worklist<VID>* frontier) {
        galois::GAccumulator<uint64_t> active_edges;
        vertexMap(frontier,
                [&](const VID& node) {
                    active_edges += _graph.GetDegree(node);
                }); 

        return active_edges.reduce();
    }  

    void buildSparsePageFrontier(Worklist<VID>* frontier) {
        int num_disks = _graph.NumberOfDisks();
        int num_disks_bit = (int)log2((float)num_disks);

        for (int i = 0; i < num_disks; i++) {
            _sparse_page_frontier.push_back(new CountableBag<PAGEID>());
        }

        vertexMap(frontier,
                [&](const VID& vid) {
                    PAGEID pid, pid_end;
                    _graph.GetPageRange(vid, &pid, &pid_end);
                    while (pid <= pid_end) {
                        int disk_id = pid % num_disks;
                        _sparse_page_frontier[disk_id]->push(pid++ >> num_disks_bit);
                    }
                });
    }

    void buildDensePageFrontier(Worklist<VID>* frontier) {
        int num_disks = _graph.NumberOfDisks();
        int num_disks_bit = (int)log2((float)num_disks);

        if (!frontier) {
            for (int i = 0; i < num_disks; i++) {
                _graph.GetActivatedPages(i)->set_all_parallel();
            }
            return;
        }

        vertexMap(frontier,
                [&](const VID& vid) {
                    PAGEID pid, pid_end;
                    _graph.GetPageRange(vid, &pid, &pid_end);
                    while (pid <= pid_end) {
                        int disk_id = pid % num_disks;
                        _graph.GetActivatedPages(disk_id)
                            ->set_bit_atomic(pid++ >> num_disks_bit);
                    }
                });
    }

 private:
    Runtime&                    _runtime;
    Gr&                         _graph;
    Worklist<VID>*              _out_frontier;          // Output worklist
    MPMCQueue<IoItem*>*         _fetched_tasks;
    IoEngine*                   _io_engine;             // IO engine
    ComputeEngine*              _compute_engine;        // Compute engine
    PBEngine*                   _pb_engine;             // PB engine
    std::vector<CountableBag<PAGEID>*>     _sparse_page_frontier;  // Page frontier for sparse case
    Func&                       _func;
    FLAGS                       _flags;
    bool                        _work_exists;
    uint64_t                    _num_activated_nodes;
    uint64_t                    _num_activated_edges;
    FrontierType                _frontier_type;
    double                      _io_time;
    double                      _compute_time;
};

template <typename G, typename F>
Worklist<VID>* edgeMap(G& graph, Worklist<VID>* frontier, F&& func, FLAGS flags = 0) {
    EdgeMapExecutor<G, F> executor(graph, frontier, std::forward<F>(func), flags);
    executor.run();
    return executor.newFrontier();
}

template <typename G, typename F>
Worklist<VID>* edgeMap(G& graph, F&& func, FLAGS flags = 0) {
    EdgeMapExecutor<G, F> executor(graph, nullptr, std::forward<F>(func), flags);
    executor.run();
    return executor.newFrontier();
}

} // namespace agile

#endif // AGILE_PARALLEL_FOR_H
