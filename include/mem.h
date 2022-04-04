#ifndef AGILE_MEM_H
#define AGILE_MEM_H

#include <memory>
#include "pagealloc.h"

namespace agile {

struct pageFreer {
    size_t bytes;
    void operator()(void* ptr) {
        freePages(ptr, bytes / PAGE_SIZE);
    }
};
typedef std::unique_ptr<void, struct pageFreer> LAptr;

static size_t roundUp(size_t data, size_t align) {
    auto rem = data % align;
    if (!rem) return data;
    return data + (align - rem);
}

LAptr largeMalloc(size_t bytes, bool on_pmem=false) {
    bytes = roundUp(bytes, PAGE_SIZE);
    void *data;
    if (on_pmem) data = allocPagesPmem(bytes / PAGE_SIZE);
    else                 data = allocPages(bytes / PAGE_SIZE);
    if (!data)
        AGILE_DIE("Cannot alloate memory");
    return LAptr{data, pageFreer{bytes}};
}

} // namespace agile

#endif // AGILE_MEM_H
