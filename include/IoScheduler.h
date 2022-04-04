#ifndef BLAZE_IO_SCHEDULER_H
#define BLAZE_IO_SCHEDULER_H

#include "Type.h"
#include "Graph.h"
#include "galois/Bag.h"
#include "Param.h"
#include "Synchronization.h"
#include "AsyncIo.h"
#include "IoSync.h"

namespace blaze {

class IoScheduler {
 public:
    IoScheduler() {}

    ~IoScheduler() {}

    template <typename Gr>
    void run(Gr& graph, Worklist<VID>* frontier, Synchronization& sync, IoSync& io_sync) {
        // do nothing for dense all or sparse
        if (!frontier || !frontier->is_dense())
            return;

        // dense
        assert(frontier->is_dense());

        int disk_id, num_disks = graph.NumberOfDisks();
        std::vector<Bitmap *> page_bitmaps;
        for (disk_id = 0; disk_id < num_disks; disk_id++) {
            page_bitmaps.push_back(graph.GetActivatedPages(disk_id));
        }

        int latest_pid[num_disks];
        for (disk_id = 0; disk_id < num_disks; disk_id++) {
            latest_pid[disk_id] = 0;
        }

        uint64_t n = graph.NumberOfNodes();
        int num_disks_bit = (int)log2((float)num_disks);

        Bitmap* fbitmap = frontier->get_dense();
        uint64_t num_words = fbitmap->get_num_words();
        uint64_t i, mask, vid;
        uint64_t pos = 0;
        PAGEID pid, pid_end;
        PAGEID pid_in_disk;

        for (pos = 0; pos < num_words; pos++) {
            uint64_t word = fbitmap->get_word(pos);
            if (!word) continue;

            for (i = 0, mask = 0x1; i < 64; i++, mask <<= 1) {
                if (word & mask) {
                    vid = (pos << 6 | i);

                    graph.GetPageRange(vid, &pid, &pid_end);
                    while (pid <= pid_end) {
                        disk_id = pid % num_disks;
                        pid_in_disk = pid >> num_disks_bit;
                        page_bitmaps[disk_id]->set_bit(pid_in_disk);
                        latest_pid[disk_id] = pid_in_disk;
                        pid++;
                    }
                }
            }

            for (disk_id = 0; disk_id < num_disks; disk_id++) {
                io_sync.update_pos(disk_id, latest_pid[disk_id]);
            }
        }

        for (disk_id = 0; disk_id < num_disks; disk_id++) {
            io_sync.update_pos(disk_id, graph.GetNumPages(disk_id));
        }
    }
};

} // namespace blaze

#endif // BLAZE_IO_SCHEDULER_H
