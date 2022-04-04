#ifndef BLAZE_FILESYSTEM_H
#define BLAZE_FILESYSTEM_H

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <algorithm>

#define MAX_WRITE_IO_SIZE       (1024 * 1024 * 1024ULL)

inline bool file_exists(std::string filename) {
    struct stat st;
    return stat(filename.c_str(), &st)==0;
}

inline uint64_t file_size(std::string filename) {
    struct stat st;
    stat(filename.c_str(), &st);
    return st.st_size;
}

inline void create_directory(std::string path) {
    assert(mkdir(path.c_str(), 0764)==0 || errno==EEXIST);
}

// TODO: only on unix-like systems
inline void remove_directory(std::string path) {
    char command[1024];
    int dumpVar;
    sprintf(command, "rm -rf %s", path.c_str());
    dumpVar = system(command);
    if(dumpVar == -500) return;
}

inline ssize_t big_write(int fd, char* buf, ssize_t len) {
    ssize_t ret, done = 0;
    while (done < len) {
        ret = write(fd, buf + done, std::min(len - done, (ssize_t)MAX_WRITE_IO_SIZE));
        done += ret;
    }
    return len;
}

inline ssize_t big_read(int fd, char* buf, ssize_t len) {
    ssize_t ret, done = 0;
    while (done < len) {
        ret = read(fd, buf + done, std::min(len - done, (ssize_t)MAX_WRITE_IO_SIZE));
        done += ret;
    }
    return len;
}

inline
char* create_and_map_file(const std::string& filename, size_t len, bool preFault=true) {
    int fd = open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return nullptr;
    if (fallocate(fd, 0, 0, len) < 0) {
        close(fd);
        return nullptr;
    }
    int flag = MAP_SHARED;
    char *base = reinterpret_cast<char*>(mmap(nullptr, len, PROT_READ | PROT_WRITE, flag, fd, 0));
    if (base == MAP_FAILED) {
        close(fd);
        return nullptr;
    }
    if (preFault) {
        for (size_t pos = 0; pos < len; pos += 4096) {
            volatile char tmp = *(base + pos);
        }
    }
    close(fd);
    return base;
}

inline
std::pair<char*, size_t> map_file(const std::string& filename, bool preFault=true) {
    int fd = open(filename.c_str(), O_RDWR);
    if (fd < 0) return std::make_pair(nullptr, 0);
    struct stat buf;
    if (fstat(fd, &buf) < 0) {
        close(fd);
        return std::make_pair(nullptr, 0);
    }
    int flag = MAP_SHARED;
    size_t len = buf.st_size;
    char *base = reinterpret_cast<char*>(mmap(nullptr, len, PROT_READ | PROT_WRITE, flag, fd, 0));
    if (base == MAP_FAILED) {
        close(fd);
        return std::make_pair(nullptr, 0);
    }
    if (preFault) {
        for (size_t pos = 0; pos < len; pos += 4096) {
            volatile char tmp = *(base + pos);
        }
    }
    close(fd);
    return std::make_pair(base, len);
}

/**
 * Performs an mmap of all provided arguments.
 */
#ifdef HAVE_MMAP64
template <typename... Args>
void* mmap_big(Args... args) {
    return mmap64(std::forward<Args>(args)...);
}
//! offset type for mmap
typedef off64_t offset_t;
#else
template <typename... Args>
void* mmap_big(Args... args) {
    return mmap(std::forward<Args>(args)...);
}
//! offset type for mmap
typedef off_t offset_t;
#endif

// allocation capable of larger than 4GB
inline
char* map_anonymous(size_t len, bool preFault=true) {
    int flag = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    char *base = reinterpret_cast<char*>(mmap_big(nullptr, len, PROT_READ | PROT_WRITE, flag, -1, 0));
    if (base == MAP_FAILED) return nullptr;
    if (preFault) {
        for (size_t pos = 0; pos < len; pos += 4096) {
            volatile char tmp = *(base + pos);
        }
    }
    return base;
}

#endif
