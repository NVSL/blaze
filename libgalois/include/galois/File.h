#ifndef GALOIS_FILE_H
#define GALOIS_FILE_H

#include <string>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace galois {

namespace file {

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
    int fd = open(filename.c_str(), O_RDONLY);
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

} // namespace file

} // namespace galois

#endif
