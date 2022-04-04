#ifndef BLAZE_BARRIER_H
#define BLAZE_BARRIER_H

#include <mutex>
#include <condition_variable>

class Barrier {
 public:
    Barrier(): _ready(false) {}

    ~Barrier() {}

    void notify_all() {
        {
            std::lock_guard<std::mutex> guard(_cv_m);
            _ready = true;
        }
        _cv.notify_all();
    }

    void wait() {
        std::unique_lock<std::mutex> guard(_cv_m);
        _cv.wait(guard, [&] { return _ready; });
        assert(_ready);
    }

 private:
    std::condition_variable _cv;
    std::mutex              _cv_m;
    bool                    _ready;
};

#endif // BLAZE_BARRIER_H
