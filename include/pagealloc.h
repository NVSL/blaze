#ifndef AGILE_PAGEALLOC_H
#define AGILE_PAGEALLOC_H

#include "filesystem.h"
#include "Util.h"

namespace agile {

void* allocPages(uint64_t num) {
    if (num == 0) return nullptr;
    return map_anonymous(num * PAGE_SIZE);
}

void* allocPagesPmem(uint64_t num) {
    if (num == 0) return nullptr;
    const char* pmem_path = getenv("AGILE_PMEM_PATH"); 
    if (!pmem_path) AGILE_DIE("For PMEM allocation, env AGILE_PMEM_PATH must be set");
    std::string path(pmem_path);
    path.append("/allocated_pages");
    void* addr = create_and_map_file(path, num * PAGE_SIZE);
    unlink(path.c_str());
    return addr;
}

void freePages(void* ptr, uint64_t num) {
    munmap(ptr, num * PAGE_SIZE);
}

} // namespace agile

#endif  // AGILE_PAGEALLOC_H
