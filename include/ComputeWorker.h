#ifndef AGILE_COMPUTE_WORKER_H
#define AGILE_COMPUTE_WORKER_H

#include <string>
#include "Type.h"
#include "galois/Bag.h"
#include "Synchronization.h"
#include "Queue.h"
#include "Param.h"

namespace agile {

class ComputeWorker {
 public:
    ComputeWorker(int id,
                  std::vector<MPMCQueue<IoItem*>*>& fetched_pages)
        :   _id(id),
            _num_disks(0),
            _fetched_pages(fetched_pages),
            _in_frontier(nullptr),
            _out_frontier(nullptr),
            _time(0.0),
            _num_processed_pages(0)
    {}

    ~ComputeWorker() {}

    void setFrontiers(Worklist<VID>* in, Worklist<VID>* out) {
        _in_frontier = in;
        _out_frontier = out;
    }

    template <typename Gr, typename Func>
    void run(Gr& graph, Func& func, Synchronization& sync) {
        _num_disks = graph.NumberOfDisks();
        _p2v_map = &graph.GetP2VMap();

        sync.wait_io_start();

        IoItem *items[IO_PAGE_QUEUE_BULK_DEQ];
        size_t count;
        bool io_done = false;

        while (1) {
            do {
                count = _fetched_pages[_id % _num_disks]->try_dequeue_bulk(items, IO_PAGE_QUEUE_BULK_DEQ);
                for (size_t i = 0; i < count; i++) {
                    processFetchedPages(graph, func, *items[i], sync);
                    delete items[i];
                }
            } while (count > 0);

            if (sync.check_io_done()) {
                // All completed IOs are sent and placed in the ring buffer.
                // One more loop is required to process them.
                if (!io_done) io_done = true;
                else                    break;
            }
        }

        _in_frontier = nullptr;
        _out_frontier = nullptr;
    }

 private:
    template <typename Gr, typename Func>
    void processFetchedPages(Gr& graph, Func& func, IoItem& item, Synchronization& sync) {
        PAGEID ppid_start = item.page;
        const PAGEID ppid_end = item.page + item.num;
        char* buffer = item.buf;
        while (ppid_start < ppid_end) {
            const PAGEID pid = ppid_start * _num_disks + item.disk_id;
            processFetchedPage(graph, func, pid, buffer);
            ppid_start++;
            buffer += PAGE_SIZE;
        }
        _num_processed_pages += item.num;
        free(item.buf);
        sync.add_num_free_pages(item.disk_id, item.num);
    }

    template <typename Gr, typename Func>
    void processFetchedPage(Gr& graph, Func& func, PAGEID pid, char* buffer) {
        const VID vid_start = _p2v_map[pid].first;
        const VID vid_end = _p2v_map[pid].second;

        const uint64_t page_start = (uint64_t)pid * PAGE_SIZE;
        const uint64_t page_end = page_start + PAGE_SIZE;

        VID vid = vid_start;
        while (vid <= vid_end) {
            applyFunction(graph, func, vid, page_start, page_end, buffer);
            vid++;
        }
    }

    template <typename Gr, typename Func>
    bool applyFunction(Gr& graph, Func& func, const VID& vid, const uint64_t page_start, const uint64_t page_end, char *buffer) {
        uint32_t degree = graph.GetDegree(vid);
        if (!degree || (_in_frontier && !_in_frontier->activated(vid)))
            return false;

        uint64_t offset = graph.GetOffset(vid) * sizeof(VID);
        uint64_t offset_end = offset + (degree << EDGE_WIDTH_BITS);
        uint32_t offset_in_buf;

        if (offset < page_start) {
            degree -= (page_start - offset) >> EDGE_WIDTH_BITS;
            offset_in_buf = 0;

        } else {
            offset_in_buf = offset - page_start;
        }

        if (offset_end > page_end) {
            degree -= (offset_end - page_end) >> EDGE_WIDTH_BITS;
        }

        VID* edges = (VID*)(buffer + offset_in_buf);
        PAGEID pid, pid_end;

        for (uint32_t i = 0; i < degree; i++) {
            VID dst = edges[i];
            if (func.cond(dst) && func.updateAtomic(vid, dst)) {
                // activate
                if (_out_frontier) {
                    _out_frontier->activate(dst);
                }
            }
        }

        return true;
    }

 private:
    int                     _id;
    int                     _num_disks;
    VidRange*               _p2v_map;
    std::vector<MPMCQueue<IoItem*>*>&    _fetched_pages;
    Worklist<VID>*          _in_frontier;
    Worklist<VID>*          _out_frontier;
    double                  _time;
    uint64_t                _num_processed_pages;
};

} // namespace agile

#endif // AGILE_COMPUTE_WORKER_H
