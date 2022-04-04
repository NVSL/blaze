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

struct Node {
    float value, ngh_sum;
};

// EdgeMap functions
struct SPMV_F : public EDGEMAP_F<float> {
    Graph& graph;
    Array<Node>& data;

    SPMV_F(Graph& g, Array<Node>& d):
        graph(g), data(d)
    {}

    inline bool update(VID src, VID dst) {
        float oldVal = data[dst].ngh_sum;
        data[dst].ngh_sum += data[src].value * 2.0;
        return oldVal == 0;
    }

    inline bool updateAtomic(VID src, VID dst) {
        float oldV, newV;
        do {
            oldV = data[dst].ngh_sum;
            newV = oldV + data[src].value * 2.0;
        } while (!compare_and_swap(data[dst].ngh_sum, oldV, newV));

        return oldV == 0.0;
    }

    inline bool cond (VID dst) {
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

    Graph outGraph;
    outGraph.BuildGraph(outIndexFilename, outAdjFilenames);

    uint64_t n = outGraph.NumberOfNodes();

    Array<Node> data;
    data.allocate(n);

    // Initialize values
    vertexMap(outGraph, SPMV_Vertex_Init(data));

    galois::StatTimer time("Time", "SPMV_MAIN");
    time.start();

    long iter = 0;

    while (iter++ < maxIterations) {
        edgeMap(outGraph, SPMV_F(outGraph, data), no_output);
        vertexFilter(outGraph, SPMV_VertexApply(data));
    }

    time.stop();

    return 0;
}
