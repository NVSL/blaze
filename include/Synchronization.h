#ifndef BLAZE_SYNCHRONIZATION_H
#define BLAZE_SYNCHRONIZATION_H

#include "Barrier.h"

class Synchronization {
 public:
    Synchronization(int num_disks)
    : _io_done(false), _binning_done(false)
    {
        _num_free_pages = new std::atomic<int64_t> [num_disks];
        for (int i = 0; i < num_disks; ++i) {
            _num_free_pages[i] = 0;
        }
    }

    ~Synchronization() {
        delete [] _num_free_pages;
    }

    void wait_io_start() {
        _io_ready.wait();
    }

    void notify_io_start() {
        _io_ready.notify_all();
    }

    void mark_io_done() {
        atomic_store(&_io_done, true);
    }

    bool check_io_done() {
        return atomic_load(&_io_done);
    }
 
    void mark_binning_done() {
        atomic_store(&_binning_done, true);
    }

    bool check_binning_done() {
        return atomic_load(&_binning_done);
    }

    void set_num_free_pages(int disk_id, int64_t num) {
        atomic_store(&_num_free_pages[disk_id], num);
    }
 
    int64_t get_num_free_pages(int disk_id) {
        return atomic_load(&_num_free_pages[disk_id]);
    }

    void add_num_free_pages(int disk_id, int64_t num) {
        _num_free_pages[disk_id].fetch_add(num);
    }
 
 private:
    Barrier                 _io_ready;
    std::atomic<bool>       _io_done;
    std::atomic<bool>       _binning_done;
    std::atomic<int64_t>*   _num_free_pages;
};

#endif // BLAZE_SYNCHRONIZATION_H
