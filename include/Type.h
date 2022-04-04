#ifndef AGILE_TYPE_H
#define AGILE_TYPE_H

#include <vector>
#include <map>
#include "galois/Bag.h"
#include "Worklist.h"
#include "galois/substrate/SimpleLock.h"

typedef uint32_t VID;
#define VID_BITS 2
#define EDGE_WIDTH_BITS 2

typedef int EDGEDATA;
//typedef float EDGEDATA;
struct EdgePair {
    VID dst;
    EDGEDATA data;
};

struct graph_header {
    uint64_t unused;
    uint64_t size_of_edge;
    uint64_t num_nodes;
    uint64_t num_edges;
};

typedef uint32_t PAGEID;
using VidRange = std::pair<VID, VID>;

struct IoItem {
    int     disk_id;
    PAGEID  page;
    int     num;
    char*   buf;
    IoItem(int d, PAGEID p, int n, char* b): disk_id(d), page(p), num(n), buf(b) {}
};

using PageReadList = std::vector<std::pair<PAGEID, char *>>;

using Mutex = galois::substrate::SimpleLock;

typedef uint32_t FLAGS;
const FLAGS no_output           = 0x01;
const FLAGS prop_blocking       = 0x10;

inline bool should_output(const FLAGS& flags) {
    return !(flags & no_output);
}

inline bool use_prop_blocking(const FLAGS& flags) {
    return flags & prop_blocking;
}

enum ComputeWorkerRole { NORMAL, BIN, ACCUMULATE };

#endif // AGILE_TYPES_H
