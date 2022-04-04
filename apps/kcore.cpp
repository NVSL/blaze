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
#include "boilerplate.h"
#include "EdgeMap.h"
#include "VertexMap.h"
#include "VertexFilter.h"
#include "Runtime.h"

using namespace agile;
namespace cll = llvm::cl;

static cll::opt<unsigned int>
    minK("minK", cll::desc("minK"), cll::init(1));

static cll::opt<unsigned int>
    maxK("maxK", cll::desc("maxK"), cll::init(10000));

static cll::opt<std::string>
    inIndexFilename("inIndexFilename", cll::desc("<in index file>"), cll::Required);

static cll::list<std::string>
    inAdjFilenames("inAdjFilenames", cll::desc("<in adj files>"), cll::OneOrMore);


struct Update_Deg : public EDGEMAP_F<uint32_t> {
    Array<int32_t>& degrees;

    Update_Deg(Array<int32_t>& d): degrees(d) {}

    inline bool update(VID src, VID dst) {
        degrees[dst]--;
        return 1;
    }  

    inline bool updateAtomic(VID src, VID dst) {
        atomic_add(&degrees[dst], -1);
        return 1;
    }

    inline bool cond(VID dst) {
        return degrees[dst] > 0;
    }
};

struct Vertex_Init {
    Graph& out_graph;
    Graph& in_graph;
    Array<int32_t>& degrees;
    Array<uint32_t>& core_numbers;

    Vertex_Init(Graph& og, Graph& ig, Array<int32_t>& d, Array<uint32_t>& c):
        out_graph(og), in_graph(ig), degrees(d), core_numbers(c) {}

    inline bool operator () (const VID& node) {
        core_numbers[node] = 0;
        degrees[node] = out_graph.GetDegree(node) + in_graph.GetDegree(node);
    }
};

struct Deg_LessThan_K {
    Array<int32_t>& degrees;
    Array<uint32_t>& core_numbers;
    long k;

    Deg_LessThan_K(Array<int32_t>& d, Array<uint32_t>& c, long _k):
        degrees(d), core_numbers(c), k(_k) {}

    inline bool operator () (const VID& node) {
        if (degrees[node] < k) {
            core_numbers[node] = k - 1;
            degrees[node] = 0;
            return 1;

        } else {
            return 0;
        }
    }
};

struct Deg_AtLeast_K {
    Array<int32_t>& degrees;
    long k;

    Deg_AtLeast_K(Array<int32_t>& d, long _k):
        degrees(d), k(_k) {}

    inline bool operator () (const VID& node) {
        return degrees[node] >= k;
    }
};

int main(int argc, char **argv) {
    AgileStart(argc, argv);
    Runtime runtime(numComputeThreads, numIoThreads, ioBufferSize * MB);

    Graph outGraph;
    outGraph.BuildGraph(outIndexFilename, outAdjFilenames);

    Graph inGraph;
    inGraph.BuildGraph(inIndexFilename, inAdjFilenames);

    uint64_t n = outGraph.NumberOfNodes();

    Array<uint32_t> core_numbers;
    core_numbers.allocate(n);
    Array<int32_t> degrees;
    degrees.allocate(n);

    Worklist<VID>* frontier = new Worklist<VID>(n);
    
    vertexMap(outGraph, Vertex_Init(outGraph, inGraph, degrees, core_numbers));
    frontier->activate_all();

    galois::StatTimer time("Time", "KCORE_MAIN");
    time.start();

    long k, largeset_core = -1;

    for (k = minK; k <= maxK; k++) {
        while (true) {
            Worklist<VID>* to_remove = vertexFilter(frontier, Deg_LessThan_K(degrees, core_numbers, k));
            Worklist<VID>* remaining = vertexFilter(frontier, Deg_AtLeast_K(degrees, k));
            delete frontier;
            frontier = remaining;
            if (to_remove->count() == 0) {      // fixed point. found k-core
                delete to_remove;
                break;

            } else {
                edgeMap(outGraph, to_remove, Update_Deg(degrees), no_output);
                edgeMap(inGraph, to_remove, Update_Deg(degrees), no_output);
                delete to_remove;
            }
        }
        if (frontier->count() == 0) {
            break;
        }
    }
    largeset_core = k - 1;

    delete frontier;

    time.stop();

    printf("Largest core is %ld\n", largeset_core);
    galois::GAccumulator<size_t> num_cores;

    galois::do_all(galois::iterate(outGraph),
                [&](const VID& node) {
                    if (degrees[node] > 0) {
                        num_cores += 1;
                    }
                });
    printf("Number of cores in [%u, %u]: %lu\n", minK.getValue(), maxK.getValue(), num_cores.reduce());

    return 0;
}
