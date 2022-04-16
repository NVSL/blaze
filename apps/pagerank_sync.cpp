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

// All PageRank algorithm variants use the same constants for ease of
// comparison.
constexpr static const float DAMPING        = 0.85;
constexpr static const float EPSILON        = 1.0e-2;
constexpr static const float EPSILON2       = 1.0e-7;
constexpr static const unsigned MAX_ITER    = 1000;

static cll::opt<float>
        damping("damping", cll::desc("damping"),
                cll::init(DAMPING));

static cll::opt<float>
        epsilon("epsilon", cll::desc("epsilon"),
                cll::init(EPSILON));

static cll::opt<float>
        epsilon2("epsilon2", cll::desc("epsilon2"),
                cll::init(EPSILON2));

static cll::opt<unsigned int>
        maxIterations("maxIterations",
                cll::desc("Maximum iterations"),
                cll::init(MAX_ITER));

struct Node {
    float score, delta, ngh_sum;
};

// EdgeMap functions
struct PR_F : public EDGEMAP_F<float> {
    Graph& graph;
    Array<Node>& data;

    PR_F(Graph& g, Array<Node>& d):
        graph(g), data(d)
    {}

    inline bool update(VID src, VID dst) {
        float oldVal = data[dst].ngh_sum;
        data[dst].ngh_sum += data[src].delta / graph.GetDegree(src);
        return oldVal == 0;
    }

    inline bool updateAtomic(VID src, VID dst) {
        float oldV, newV;
        do {
            oldV = data[dst].ngh_sum;
            newV = oldV + data[src].delta / graph.GetDegree(src);
        } while (!compare_and_swap(data[dst].ngh_sum, oldV, newV));

        return oldV == 0.0;
    }

    inline bool cond (VID dst) {
        return 1;
    }
};

// VertexMap functions
struct PR_Vertex_Init {
    Array<Node>& data;
    float one_over_n;

    PR_Vertex_Init(Array<Node>& d, float o)
        : data(d), one_over_n(o) {}

    inline bool operator() (const VID& node) {
        data[node].score = 0.0;
        data[node].delta = one_over_n;
        data[node].ngh_sum = 0.0;
        return 1;
    }
};

struct PR_VertexApply_FirstRound {
    float damping;
    float added_constant;
    float one_over_n;
    float epsilon;
    Array<Node>& data;

    PR_VertexApply_FirstRound(Array<Node>& _d, float _dmp, float _o, float _eps):
        data(_d), damping(_dmp), one_over_n(_o), added_constant((1 - _dmp) * _o), epsilon(_eps) {}

    inline bool operator() (const VID& node) {
        auto& dnode = data[node];
        dnode.delta = damping * dnode.ngh_sum + added_constant;
        dnode.score += dnode.delta;
        dnode.delta -= one_over_n;
        dnode.ngh_sum = 0.0;
        return (std::fabs(dnode.delta) > epsilon * dnode.score);
    }
};

struct PR_VertexApply {
    float damping;
    float epsilon;
    Array<Node>& data;

    PR_VertexApply(Array<Node>& _d, float _dmp, float _eps):
        data(_d), damping(_dmp), epsilon(_eps) {}

    inline bool operator() (const VID& node) {
        auto& dnode = data[node];
        dnode.delta = dnode.ngh_sum * damping;
        dnode.ngh_sum = 0.0;
        if (std::fabs(dnode.delta) > epsilon * dnode.score) {
            dnode.score += dnode.delta;
            return 1;

        } else {
            return 0;
        }
    }
};

struct PR_TotalDelta{
    Array<Node>& data;
    galois::GAccumulator<float>& total_delta;

    PR_TotalDelta(Array<Node>& d, galois::GAccumulator<float>& t)
        : data(d), total_delta(t) {}

    inline bool operator() (const VID& node) {
        auto& dnode = data[node];
        total_delta += std::fabs(dnode.delta);
        return 1;
    }
};

int main(int argc, char **argv) {
    AgileStart(argc, argv);
    Runtime runtime(numComputeThreads, numIoThreads, ioBufferSize * MB);

    Graph outGraph;
    outGraph.BuildGraph(outIndexFilename, outAdjFilenames);

    uint64_t n = outGraph.NumberOfNodes();
    float one_over_n = 1 / (float)n;

    Array<Node> data;
    data.allocate(n);

    Worklist<VID>* frontier = new Worklist<VID>(n);
    frontier->activate_all();

    // Initialize values
    vertexMap(outGraph, PR_Vertex_Init(data, one_over_n));

    galois::GAccumulator<float> totalDelta;

    galois::StatTimer time("Time", "PAGERANK_MAIN");
    time.start();

    long iter = 0;

    while (!frontier->empty() && iter++ < maxIterations) {
        edgeMap(outGraph, frontier, PR_F(outGraph, data), no_output);
        Worklist<VID>* active = (iter == 1) ?
                                vertexFilter(outGraph, PR_VertexApply_FirstRound(data, damping, one_over_n, epsilon)) :
                                vertexFilter(outGraph, PR_VertexApply(data, damping, epsilon));

        //vertexMap(outGraph, PR_TotalDelta(data, totalDelta));
        //if (totalDelta.reduce() < epsilon2) break;
        //totalDelta.reset();

        delete frontier;
        frontier = active;
    }

    delete frontier;

    time.stop();

    printTop(data);

    return 0;
}
