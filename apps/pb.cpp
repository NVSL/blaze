#include <immintrin.h>
#include <stdio.h>
#include "llvm/Support/CommandLine.h"
#include "Runtime.h"
#include "MemGraph.h"
#include "Array.h"
#include "Util.h"
#include "galois/Galois.h"
#include "boilerplate.h"
#include "PageRank.h"
#include "Bin.h"

namespace cll = llvm::cl;

static cll::opt<unsigned int>
        binSpace("binSpace",
                cll::desc("Size of bin space in MB (default: 256)"),
                cll::init(256));

static cll::opt<int>
        binCount("binCount",
                cll::desc("Number of bins (default: 4096)"),
                cll::init(BIN_COUNT));

static cll::opt<int>
        binBufSize("binBufSize",
                cll::desc("Size of a bin buffer (default: 128)"),
                cll::init(BIN_BUF_SIZE));

static cll::opt<float>
        binningRatio("binningRatio",
                cll::desc("Binning worker ratio (default: 0.67)"),
                cll::init(BINNING_WORKER_RATIO));

using namespace blaze;
namespace cll = llvm::cl;

struct Node {
    float score, delta, ngh_sum;
};

struct PR_F : public EDGEMAP_F {
    MemGraph& graph;
    Array<Node>& data;
    Bins* bins;

    PR_F(MemGraph& g, Array<Node>& d):
        graph(g), data(d)
    {
        unsigned nthreads = galois::getActiveThreads();
        uint64_t binSpaceBytes = (uint64_t)binSpace * MB;
        bins = new Bins(graph, nthreads, binSpaceBytes,
                        binCount, binBufSize, binningRatio);
    }

    ~PR_F() {
        delete bins;
    }

    float calculateValue(VID src) {
        return data[src].delta / graph.GetDegree(src);
    }

    bool update(VID src, VID dst) {
        float oldVal = data[dst].ngh_sum;
        data[dst].ngh_sum += calculateValue(src);
        return oldVal == 0;
    }

    bool updateAtomic(VID src, VID dst) {
        float oldV, newV;
        do {
            oldV = data[dst].ngh_sum;
            newV = oldV + calculateValue(src);
        } while (!compare_and_swap(data[dst].ngh_sum, oldV, newV));

        return oldV == 0.0;
    }

    inline void binning(unsigned tid, VID src, VID dst) {
        float newVal = calculateValue(src);
        bins->append(tid, (int)dst, (int)newVal);
    }

    inline bool accumulate() {
        Bin *full_bin = bins->get_full_bin();
        if (!full_bin)
            return false;
/*
        uint64_t *bin = full_bin->get_bin();
        uint64_t bin_size = full_bin->get_size();
        printf("bin %p, size %lu\n", bin, bin_size);
        VID dst;
        float value;

        for (size_t i = 0; i < bin_size; i++) {
            uint64_t entry = bin[i];
            dst = (VID)((entry >> 32) & 0x00000000ffffffff);
            value = (float)(entry & 0x00000000ffffffff);
            data[dst].ngh_sum += value;
        }
*/

        full_bin->reset();

        return true;
    }

    inline bool cond (VID dst) {
        return 1;
    }
};

template <typename Func>
bool applyFunction(MemGraph& graph, Func& func, const VID& vid,
    const uint64_t page_start, const uint64_t page_end, char *buffer)
{
    uint32_t degree = graph.GetDegree(vid);

    if (!degree)
        return false;

    uint64_t offset = graph.GetOffset(vid) * sizeof(VID);
    uint64_t offset_end = offset + (degree << EDGE_WIDTH_BITS);
    uint32_t offset_in_buf;

    if (offset < page_start) {
        degree -= (page_start - offset) >> EDGE_WIDTH_BITS;
        offset_in_buf = 0;

    } else {
        offset_in_buf = offset - page_start;
    }

    if (offset_end > page_end) {
        degree -= (offset_end - page_end) >> EDGE_WIDTH_BITS;
    }

    VID* edges = (VID*)(buffer + offset_in_buf);

    for (uint32_t i = 0; i < degree; i++) {
        VID dst = edges[i];
        if (func.cond(dst)) {
            //func.update(vid, dst);
            //func.updateAtomic(vid, dst);
            func.binning(0, vid, dst);
        }
    }

    return true;
}

template <typename Func>
void processPage(MemGraph& graph, Func& func, PAGEID pid, char* buffer) {
    auto p2v_map = &graph.GetP2VMap();
    const VID vid_start = p2v_map[pid].first;
    const VID vid_end   = p2v_map[pid].second;

    const uint64_t page_start = (uint64_t)pid * PAGE_SIZE;
    const uint64_t page_end = page_start + PAGE_SIZE;

    VID vid = vid_start;
    while (vid <= vid_end) {
        applyFunction(graph, func, vid, page_start, page_end, buffer);
        vid++;
    }
}

int main(int argc, char **argv) {
    Runtime runtime;
    AgileStart(argc, argv);

    MemGraph graph;
    graph.BuildGraph(outIndexFilename, outAdjFilenames);

    printf("Build graph DONE\n");

    galois::StatTimer total_time("Time", "TOTAL");
    total_time.start();

    uint64_t n = graph.NumberOfNodes();
    float one_over_n = 1 / (float)n;

    Array<Node> data;
    data.allocate(n);

    // Init data
    galois::do_all(galois::iterate(graph),
                    [&](const VID& node) {
                        data[node].score = 0.0;
                        data[node].delta = one_over_n;
                        data[node].ngh_sum = 0.0;
                    });

    // PR function
    auto func = PR_F(graph, data);

    uint64_t num_pages = graph.GetNumPages(0);

    galois::StatTimer time("Time", "PAGERANK");
    time.start();

    galois::do_all(galois::iterate((uint64_t)0, num_pages),
                    [&](uint64_t pid) {
                        unsigned tid = galois::substrate::ThreadPool::getTID();
                        if (tid % 5 != 0) {
                            char *buffer = graph.GetEdgePage(0, pid);
                            processPage(graph, func, pid, buffer);
                        } else {
                            func.accumulate();
                        }
                    });

    time.stop();

    galois::do_all(galois::iterate(graph),
                    [&](const VID& node) {
                        auto& dnode = data[node];
                        dnode.delta = 0.85 * dnode.ngh_sum + 0.15 * one_over_n;
                        dnode.score += dnode.delta;
                    });

/*
    for (uint64_t pid = 0; pid < num_pages; pid++) {
        char *buffer = graph.GetEdgePage(0, pid);
        processPage(graph, func, pid, buffer);
    }
    for (VID node = 0; node < n; node++) {
        auto& dnode = data[node];
        dnode.delta = 0.85 * dnode.ngh_sum + 0.15 * one_over_n;
        dnode.score += dnode.delta;
    }
*/

    total_time.stop();

    printTop(data);

    return 0;
}
