#ifndef BLAZE_ASYNC_IO_H
#define BLAZE_ASYNC_IO_H

#include <unistd.h>
#include <sys/types.h>
#include <linux/aio_abi.h>
#include <sys/syscall.h>
#include "filesystem.h"
#include "Type.h"
#include "Util.h"
#include "Param.h"

namespace blaze {

static int io_setup(unsigned nr, aio_context_t *ctxp) {
    return syscall(__NR_io_setup, nr, ctxp);
}

static int io_submit(aio_context_t ctx, long nr, struct iocb **iocbpp) {
    return syscall(__NR_io_submit, ctx, nr, iocbpp);
}

static int io_getevents(aio_context_t ctx, long min_nr, long max_nr,
        struct io_event *events, struct timespec *timeout) {
    return syscall(__NR_io_getevents, ctx, min_nr, max_nr, events, timeout);
}

static int io_destroy(aio_context_t ctx) {
    return syscall(__NR_io_destroy, ctx);
}


class AsyncIoWorker {
 public:
    AsyncIoWorker(int fd, aio_context_t& ctx, PageReadList& read_list)
        : _fd(fd), _ctx(ctx),
            _read_list(read_list),
            _queued(0), _sent(0), _received(0),
            _total_bytes_accessed(0)
    {
        BLAZE_ASSERT(_fd >= 0, "Failed to open file.");

        _iocb = (struct iocb*)calloc(IO_QUEUE_DEPTH, sizeof(*_iocb));
        _iocbs = (struct iocb**)calloc(IO_QUEUE_DEPTH, sizeof(*_iocbs));
        _events = (struct io_event*)calloc(IO_QUEUE_DEPTH, sizeof(*_events));
        _target = read_list.size();
    }

    ~AsyncIoWorker() {
        free(_iocb);
        free(_iocbs);
        free(_events);
        BLAZE_ASSERT(_queued == _sent && _sent == _received, "Inconsistent IO counters.");
    }

    void run() {
        size_t done = 0;
        while (done < _target) {
            submitTasks();
            done += receiveTasks();
        }
    }

 private:
    void enqueueRequest(int fd, char* buf, size_t len, off_t offset, void* data) {
        uint32_t idx = _queued % IO_QUEUE_DEPTH;
        struct iocb* pIocb = &_iocb[idx];
        memset(pIocb, 0, sizeof(*pIocb));
        pIocb->aio_fildes = fd;
        pIocb->aio_lio_opcode = IOCB_CMD_PREAD;
        pIocb->aio_buf = (uint64_t)buf;
        pIocb->aio_nbytes = len;
        pIocb->aio_offset = offset;
        pIocb->aio_data = (uint64_t)data;
        _queued++;
    }

    void submitTasks() {
        char* buf;
    PAGEID pid;
        off_t offset;
    size_t len = PAGE_SIZE;

        while (_queued < _target && (_queued - _sent) < IO_QUEUE_DEPTH) {
            std::tie(pid, buf) = _read_list.back();
            _read_list.pop_back();
            offset = (off_t)pid * len;
            enqueueRequest(_fd, buf, len, offset, nullptr);

            _total_bytes_accessed += len;
        }

        if (_queued - _sent == 0) return;

        for (size_t i = 0; i < _queued - _sent; i++) {
            _iocbs[i] = &_iocb[(_sent + i) % IO_QUEUE_DEPTH];
        }

        int ret = io_submit(_ctx, _queued - _sent, _iocbs);
        if (ret > 0) {
            _sent += ret;
        }
    }

    size_t receiveTasks() {
        unsigned min = 0;
        unsigned max = IO_QUEUE_DEPTH;

        int received = io_getevents(_ctx, min, max, _events, NULL);
        assert(received <= max);

        if (received < 0) {
            printf("error: %d (%s)\n", errno, strerror(errno));
            abort();
        }

        for (int i = 0; i < received; i++) {
            BLAZE_ASSERT(_events[i].res > 0, "Failed to execute AIO request.");
        }
        _received += received;

        return received;
    }

 private:
    int                 _fd;
    PageReadList&       _read_list;
    aio_context_t&      _ctx;
    struct iocb*        _iocb;
    struct iocb**       _iocbs;
    struct io_event*    _events;
    uint64_t            _target;
    uint64_t            _queued;
    uint64_t            _sent;
    uint64_t            _received;
    uint64_t            _total_bytes_accessed;
};

} // namespace blaze

#endif // BLAZE_ASYNC_IO_H
