#include <stdio.h>
#include "llvm/Support/CommandLine.h"
#include "Runtime.h"
#include "Graph.h"
#include "Array.h"
#include "Util.h"
#include "galois/Galois.h"
#include "boilerplate.h"
#include "PageRank.h"
#include "EdgeMap.h"

using namespace blaze;

struct TEST : public EDGEMAP_F {
};

int main(int argc, char **argv) {
    Runtime runtime;
    AgileStart(argc, argv);

    Graph graph;
    graph.BuildGraph(outIndexFilename, outAdjFilenames, ioBufferSize * MB);

    printf("Build graph DONE\n");

    uint64_t n = graph.NumberOfNodes();
    float one_over_n = 1 / (float)n;

    Worklist<VID> frontier(n);
    for (VID node = 0; node < n; node++) {
        //if (node % 3 != 0) {
            frontier.activate(node);
        //}
    }

    galois::StatTimer total_time("Time", "TOTAL");
    total_time.start();

    // 1. schedule all, output
    //edgeMap(graph, TEST());

    // 2. schedule all, no output
    //edgeMap(graph, TEST(), no_output);

    // 3. schedule selective, output
    //edgeMap(graph, &frontier, TEST());

    // 4. schedule selective, no output
    edgeMap(graph, &frontier, TEST(), no_output);

    total_time.stop();

    return 0;
}
