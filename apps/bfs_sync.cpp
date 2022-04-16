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
#include "VertexMap.h"
#include "boilerplate.h"
#include "Runtime.h"

using namespace blaze;
namespace cll = llvm::cl;

static cll::opt<unsigned int>
    startNode("startNode",
            cll::desc("Node to start search from (default value 0)"),
            cll::init(0));

struct BFS_F : public EDGEMAP_F<uint32_t> {
    Array<VID>& parents;

    BFS_F(Array<VID>& p): parents(p) {}

    inline bool update(VID src, VID dst) {
        if (parents[dst] == UINT_MAX) {
            parents[dst] = src;
            return 1;
        }
        else return 0;
    }

    inline bool updateAtomic(VID src, VID dst) {
        return compare_and_swap(parents[dst], UINT_MAX, src);
    }

    inline bool cond(VID dst) {
        return parents[dst] == UINT_MAX;
    }
};

struct BFS_Vertex_Init {
    Array<VID>& parents;

    BFS_Vertex_Init(Array<VID>& p): parents(p) {}

    inline bool operator() (const VID& node) {
        parents[node] = UINT_MAX;
    }
};

int main(int argc, char **argv) {
    AgileStart(argc, argv);
    Runtime runtime(numComputeThreads, numIoThreads, ioBufferSize * MB);

    Graph outGraph;
    outGraph.BuildGraph(outIndexFilename, outAdjFilenames);

    uint64_t n = outGraph.NumberOfNodes();

    Array<VID> parents;
    parents.allocate(n);

    vertexMap(outGraph, BFS_Vertex_Init(parents));

    parents[startNode] = startNode;

    Worklist<VID>* frontier = new Worklist<VID>(n);
    frontier->activate(startNode);

    galois::StatTimer time("Time", "BFS_MAIN");
    time.start();

    while (!frontier->empty()) {
        Worklist<VID>* output = edgeMap(outGraph, frontier, BFS_F(parents), 0);
        delete frontier;
        frontier = output;
    }

    delete frontier;

    time.stop();

    return 0;
}
