#ifndef AGILE_UTIL_H
#define AGILE_UTIL_H

#include <locale>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cerrno>
#include <cstdlib>
#include <string.h>
#include <sys/resource.h>
#include <immintrin.h>
#include "galois/gIO.h"
#include "Param.h"

#define kB      (1024)
#define MB      (1024*kB)
#define GB      (1024*MB)

// Align
#define ALIGN_UPTO(size, align) ((((uint64_t)size)+(align)-1u)&~((align)-1u))

// Page related
#define PAGE_NUM(o)             ((o) >> PAGE_SHIFT)
#define OFFSET_IN_PAGE(o)       ((PAGE_SIZE-1) & (o))
#define ROUND_UP_TO_PAGE(o)     ALIGN_UPTO(o, PAGE_SIZE)

// Error checking
#define AGILE_SYS_DIE(...)                                                       \
    do {                                                                         \
        galois::gError(__FILE__, ":", __LINE__, ": ", strerror(errno), ": ",     \
                     ##__VA_ARGS__);                                             \
        abort();                                                                 \
    } while (0)

#define AGILE_DIE(...)                                                           \
    do {                                                                         \
        galois::gError(__FILE__, ":", __LINE__, ": ", ##__VA_ARGS__);            \
        abort();                                                                 \
    } while (0)

#define AGILE_ASSERT(cond, ...)                                                  \
    do {                                                                         \
        bool b = (cond);                                                         \
        if (!b) {                                                                \
            galois::gError(__FILE__, ":", __LINE__, ": assertion failed: ", #cond,  \
                                         " ", ##__VA_ARGS__);                    \
            abort();                                                             \
        }                                                                        \
    } while (0)

// borrowed the code from gap
template <typename T_>
class RangeIter {
    T_ x_;
 public:
    explicit RangeIter(T_ x) : x_(x) {}
    bool operator!=(RangeIter const& other) const { return x_ != other.x_; }
    T_ const& operator*() const { return x_; }
    RangeIter& operator++() {
        ++x_;
        return *this;
    }
};

template <typename T_>
class Range{
    T_ from_;
    T_ to_;
 public:
    explicit Range(T_ to) : from_(0), to_(to) {}
    Range(T_ from, T_ to) : from_(from), to_(to) {}
    RangeIter<T_> begin() const { return RangeIter<T_>(from_); }
    RangeIter<T_> end() const { return RangeIter<T_>(to_); }
};

#define NOP10() asm("nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;")

class comma_numpunct : public std::numpunct<char>
{
    protected:
        virtual char do_thousands_sep() const
        {
            return ',';
        }

        virtual std::string do_grouping() const
        {
            return "\03";
        }
};

class MemoryCounter {
 public:
    MemoryCounter() {
        getrusage(RUSAGE_SELF, &__memory);
        __previous_mem = __memory.ru_maxrss;
    }

    ~MemoryCounter() {
        getrusage(RUSAGE_SELF, &__memory);
        uint64_t used_mem = __memory.ru_maxrss - __previous_mem;
        printf("MemoryCounter: %lu MB -> %lu MB, %lu MB total\n", __previous_mem/1024, __memory.ru_maxrss/1024, used_mem/1024);
    }

 private:
     struct rusage __memory;
     uint64_t __previous_mem;
};

#define declare_memory_counter \
     struct rusage __memory; \
     uint64_t __previous_mem = 0, __used_mem; \
     getrusage(RUSAGE_SELF, &__memory); \
     __previous_mem = __memory.ru_maxrss;

#define get_memory_usage(msg, args...) \
     getrusage(RUSAGE_SELF, &__memory); \
     __used_mem = __memory.ru_maxrss - __previous_mem; \
     __previous_mem = __memory.ru_maxrss; \
     printf("(%s,%d) [%lu MB total - %lu MB since last measure] " msg "\n", __FUNCTION__ , __LINE__, __previous_mem/1024, __used_mem/1024, ##args);

#define drop_page_cache \
    do {                \
        sync();         \
        int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);    \
        write(fd, "3", 1);          \
        close(fd);                  \
    } while(0);

inline void prefetch_range(char *addr, size_t len) {
    char *end = addr + len;
    while (addr < end) {
        _mm_prefetch(addr, _MM_HINT_T0);
        addr += 64;
    }
}

inline void prefetch_range_offset(char *base, long idx, size_t len)
{
    asm volatile (
        "xor    %%r8, %%r8 \n"      /* r8: start addr */
        "xor    %%r9, %%r9 \n"      /* r9:  */
        "lea    (%[base], %[idx], 4), %%r8 \n"
"LOOP_RANGE%=: \n"
        "prefetcht0  (%%r8) \n"
        "add    $0x40, %%r8 \n"
        "add    $0x40, %%r9 \n"
        "cmp    %[len], %%r9 \n"
        "jl     LOOP_RANGE%= \n"
        :
        : [base]"r"(base), [idx]"r"(idx), [len]"r"(len)
        : "r8", "r9");
}

#endif // AGILE_UTIL_H
