#ifndef AGILE_IO_SYNC_H
#define AGILE_IO_SYNC_H

#include <vector>
#include <atomic>
//#include "Type.h"

namespace agile {

class IoSync {
 public:
    IoSync(int num_disks) {
        _pos = new std::atomic<uint64_t> [num_disks];
        for (int i = 0; i < num_disks; i++) {
            atomic_store(&_pos[i], (uint64_t)0);
        }
    }

    ~IoSync() {
        delete [] _pos;
    }

    void update_pos(int idx, uint64_t pos) {
        atomic_store(&_pos[idx], pos);
    }

    uint64_t get_pos(int idx) {
        return atomic_load(&_pos[idx]);
    }

 private:
    std::atomic<uint64_t>*  _pos;
};

} // namespace agile

#endif // AGILE_IO_SYNC_H
