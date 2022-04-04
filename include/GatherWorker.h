#ifndef BLAZE_GATHER_WORKER_H
#define BLAZE_GATHER_WORKER_H

#include <string>
#include "Type.h"
#include "galois/Bag.h"
#include "Synchronization.h"
#include "Bin.h"

namespace blaze {

class GatherWorker {
 public:
    GatherWorker(int id)
        :   _id(id),
            _time(0.0),
            _out_frontier(nullptr)
    {}

    ~GatherWorker() {}

    void setFrontier(Worklist<VID>* out) {
        _out_frontier = out;
    }

    template <typename Func>
    inline bool try_gather(Bins* bins, Func &func) {
        Bin *full_bin = bins->get_full_bin();
        if (!full_bin)
            return false;

        uint64_t *bin = full_bin->get_bin();
        uint64_t bin_size = full_bin->get_size();
        int idx = full_bin->get_idx();

        if (_out_frontier) {
            for (int i = 0; i < idx; i++) {
                VID dst = (VID)(bin[i]>> 32);
                uint32_t val = (uint32_t)(bin[i] & 0x00000000ffffffff);
                if (func.gather(dst, *(typename Func::value_type *)&val))
                    _out_frontier->activate(dst);
            }
        } else {
            for (int i = 0; i < idx; i++) {
                VID dst = (VID)(bin[i]>> 32);
                uint32_t val = (uint32_t)(bin[i] & 0x00000000ffffffff);
                func.gather(dst, *(typename Func::value_type *)&val);
            }
        }

        full_bin->reset();

        return true;
    }

    template <typename Gr, typename Func>
    void run(Gr& graph, Func& func, Synchronization& sync) {
        auto time_start = std::chrono::steady_clock::now();

        Bins* bins = func.get_bins();

        sync.wait_io_start();

        bool binning_done = false;
        bool job_exists;

        while (1) {
            job_exists = try_gather(bins, func);

            if (binning_done && !job_exists)
                break;

            if (sync.check_binning_done()) {
                if (!binning_done) binning_done = true;
            }
        }

        _out_frontier = nullptr;

        auto time_end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = time_end - time_start;
        _time = elapsed.count();
    }

    double getTime() const {
        return _time;
    }

    int getId() const {
        return _id;
    }

 private:
    int                     _id;
    double                  _time;
    Worklist<VID>*          _out_frontier;
};

} // namespace blaze

#endif // BLAZE_GATHER_WORKER_H
