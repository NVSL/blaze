#include <iostream>
#include <string>
#include <queue>
#include <deque>
#include "llvm/Support/CommandLine.h"
#include "galois/Galois.h"
#include "galois/Bag.h"
#include "Type.h"
#include "Graph.h"
#include "atomics.h"
#include "Array.h"
#include "Util.h"
#include "EdgeMap.h"
#include "boilerplate.h"
#include "Runtime.h"
#include "VertexMap.h"
#include "VertexFilter.h"
#include "PageRank.h"
#include "Bin.h"

using namespace blaze;
namespace cll = llvm::cl;

constexpr static const unsigned MAX_ITER = 20;

static cll::opt<unsigned int>
        maxIterations("maxIterations",
                cll::desc("Maximum iterations (default: 20)"),
                cll::init(MAX_ITER));

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

struct Node {
    float value, ngh_sum;
};

// EdgeMap functions
struct SPMV_F : public EDGEMAP_F<float> {
    Graph& graph;
    Array<Node>& data;

    SPMV_F(Graph& g, Array<Node>& d, Bins* b):
        graph(g), data(d), EDGEMAP_F(b)
    {}

    inline float scatter(VID src, VID dst) {
        return data[src].value * 2.0;
    }

    inline bool gather(VID dst, float val) {
        data[dst].ngh_sum += val;
        return 1;
    }
};

// VertexMap functions
struct SPMV_Vertex_Init {
    Array<Node>& data;

    SPMV_Vertex_Init(Array<Node>& d): data(d) {}

    inline bool operator() (const VID& node) {
        data[node].value = 1.0;
        data[node].ngh_sum = 0.0;
        return 1;
    }
};

struct SPMV_VertexApply {
    Array<Node>& data;

    SPMV_VertexApply(Array<Node>& d): data(d) {}

    inline bool operator() (const VID& node) {
        auto& dnode = data[node];
        dnode.value = dnode.ngh_sum;
        return 1;
    }
};

int main(int argc, char **argv) {
    AgileStart(argc, argv);
    Runtime runtime(numComputeThreads, numIoThreads, ioBufferSize * MB);
    runtime.initBinning(binningRatio);

    Graph outGraph;
    outGraph.BuildGraph(outIndexFilename, outAdjFilenames);

    uint64_t n = outGraph.NumberOfNodes();

    Array<Node> data;
    data.allocate(n);

    // Allocate bins
    unsigned nthreads = galois::getActiveThreads();
    uint64_t binSpaceBytes = (uint64_t)binSpace * MB;
    Bins *bins = new Bins(outGraph, nthreads, binSpaceBytes,
                          binCount, binBufSize, binningRatio);

    // Initialize values
    vertexMap(outGraph, SPMV_Vertex_Init(data));

    galois::StatTimer time("Time", "SPMV_MAIN");
    time.start();

    long iter = 0;

    while (iter++ < maxIterations) {
        edgeMap(outGraph, SPMV_F(outGraph, data, bins), no_output | prop_blocking);
        vertexFilter(outGraph, SPMV_VertexApply(data));
    }

    time.stop();

    delete bins;

    return 0;
}
