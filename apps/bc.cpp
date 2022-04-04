#include <iostream>
#include <string>
#include <queue>
#include <deque>
#include <math.h>
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
#include "PageRank.h"

namespace cll = llvm::cl;
using namespace blaze;

static cll::opt<unsigned int>
    startNode("startNode",
                        cll::desc("Node to start search from (default value 0)"),
                        cll::init(0));

static cll::opt<std::string>
    inIndexFilename("inIndexFilename", cll::desc("<in index file>"), cll::Required);

static cll::list<std::string>
    inAdjFilenames("inAdjFilenames", cll::desc("<in adj files>"), cll::OneOrMore);


struct Node {
    float num_paths;
    float dependencies;
    float inverse_num_paths;
    bool visited;
};

struct BC_F : public EDGEMAP_F<uint32_t> {
    Array<float>& num_paths;
    Bitmap& visited;

    BC_F(Array<float>& n, Bitmap& v): num_paths(n), visited(v) {}

    inline bool update(VID src, VID dst) {
        float oldV = num_paths[dst];
        num_paths[dst] += num_paths[src];
        return oldV == 0.0;
    } 

    inline bool updateAtomic(VID src, VID dst) {
        float oldV, newV;
        do {
            oldV = num_paths[dst];
            newV = oldV + num_paths[src];
        } while (!compare_and_swap(num_paths[dst], oldV, newV));
        return oldV == 0.0;
    } 

    inline bool cond(VID dst) {
        return !visited.get_bit(dst);
    }
};

struct BC_Back_F : public EDGEMAP_F<uint32_t> {
    Array<float>& dependencies;
    Bitmap& visited;

    BC_Back_F(Array<float>& d, Bitmap& v): dependencies(d), visited(v) {}

    inline bool update(VID src, VID dst) {
        float oldV = dependencies[dst];
        dependencies[dst] += dependencies[src];
        return oldV == 0.0;
    }

    inline bool updateAtomic(VID src, VID dst) {
        float oldV, newV;
        do {
            oldV = dependencies[dst];
            newV = oldV + dependencies[src];
        } while (!compare_and_swap(dependencies[dst], oldV, newV));
        return oldV == 0.0;
    }

    inline bool cond(VID dst) {
        return !visited.get_bit(dst);
    }
};

struct BC_Vertex_F {
    Bitmap& visited;

    BC_Vertex_F(Bitmap& v): visited(v) {}

    inline bool operator() (const VID& node) {
        visited.set_bit_atomic(node);
        return 1;
    }
};

struct BC_Back_Vertex_F {
    Array<float>& dependencies;
    Array<float>& inverse_num_paths;
    Bitmap& visited;

    BC_Back_Vertex_F(Array<float>& d, Array<float>& i, Bitmap& v)
    : dependencies(d), inverse_num_paths(i), visited(v) {}

    inline bool operator() (const VID& node) {
        visited.set_bit_atomic(node);
        dependencies[node] += inverse_num_paths[node];
        return 1;
    }
};

void printTopBC(Array<float>& dependencies, unsigned topn = PRINT_TOP) {
    typedef std::map<Pair, VID> TopMap;
    TopMap top;

    uint64_t n = dependencies.size();
    for (uint64_t src = 0; src < n; src++) {
        if (isnan(dependencies[src])) continue;

        Pair key(dependencies[src], src);

        if (top.size() < topn) {
            top.insert(std::make_pair(key, src));
            continue;
        }

        if (top.begin()->first < key) {
            top.erase(top.begin());
            top.insert(std::make_pair(key, src));
        }
    }

    int rank = 1;
    std::cout << "Rank BetweennessCentrality Id\n";
    for (auto ii = top.rbegin(), ei = top.rend(); ii != ei; ++ii, ++rank) {
        float value = ii->first.value;
        VID node = ii->first.id;
        printf("%3d: %20.10f %10u\n", rank, value, node);
    }
}

int main(int argc, char **argv) {
    AgileStart(argc, argv);
    Runtime runtime(numComputeThreads, numIoThreads, ioBufferSize * MB);

    // Out graph
    Graph outGraph;
    outGraph.BuildGraph(outIndexFilename, outAdjFilenames);

    // In graph
    Graph inGraph;
    inGraph.BuildGraph(inIndexFilename, inAdjFilenames);

    uint64_t n = outGraph.NumberOfNodes();

    Array<float> num_paths;
    num_paths.allocate(n);

    Array<float> dependencies;
    dependencies.allocate(n);

    Array<float> inverse_num_paths;
    inverse_num_paths.allocate(n);

    Bitmap visited(n);
    visited.reset_parallel();

    galois::StatTimer time("Time", "BC_MAIN");
    time.start();

    galois::do_all(galois::iterate(outGraph),
                     [&](const VID& node) {
                         num_paths[node] = 0.0;
                     });

    num_paths[startNode] = 1.0;
    visited.set_bit(startNode);

    Worklist<VID>* frontier = new Worklist<VID>(n);
    frontier->activate(startNode);

    std::vector<Worklist<VID>*> levels;
    levels.push_back(frontier);

    long round = 0;
    while (!frontier->empty()) {
        round++;
        Worklist<VID>* output = edgeMap(outGraph, frontier, BC_F(num_paths, visited), 0);
        vertexMap(output, BC_Vertex_F(visited));
        levels.push_back(output);
        frontier = output;
    }

    galois::do_all(galois::iterate(outGraph),
                     [&](const VID& node) {
                         dependencies[node] = 0.0;
                         inverse_num_paths[node] = 1 / num_paths[node];
                     });

    delete levels[round];

    // reuse visited
    visited.reset_parallel();

    frontier = levels[round-1];
    vertexMap(frontier, BC_Back_Vertex_F(dependencies, inverse_num_paths, visited));

    // backward phase
    for (long r = round - 2; r >= 0; r--) {
        edgeMap(inGraph, frontier, BC_Back_F(dependencies, visited), no_output);
        delete frontier;
        frontier = levels[r];
        vertexMap(frontier, BC_Back_Vertex_F(dependencies, inverse_num_paths, visited));
    }
    delete frontier;

    // update dependencies scores
    galois::do_all(galois::iterate(outGraph),
                     [&](const VID& node) {
                         dependencies[node] = (dependencies[node] - inverse_num_paths[node]) / inverse_num_paths[node];
                     });

    time.stop();

    printTopBC(dependencies);

    return 0;
}
