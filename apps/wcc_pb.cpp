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
#include "Connectivity.h"

using namespace agile;
namespace cll = llvm::cl;

static cll::opt<std::string>
		inIndexFilename("inIndexFilename", cll::desc("<in index file>"), cll::Required);

static cll::list<std::string>
		inAdjFilenames("inAdjFilenames", cll::desc("<in adj files>"), cll::OneOrMore);

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


struct WCC_F : public EDGEMAP_F<uint32_t> {
	Array<uint32_t>& ids;

	WCC_F(Array<uint32_t>& i, Bins* b): ids(i), EDGEMAP_F(b) {}

    inline uint32_t scatter(VID src, VID dst) {
        return ids[src];
    }

    inline bool gather(VID dst, uint32_t val) {
		uint32_t orig_id = ids[dst];
		if (val < orig_id) {
			ids[dst] = val;
		}
		return 1;
    }

	inline bool cond(VID dst) {
		return 1;
	}
};

struct WCC_Shortcut {
	Array<uint32_t>& ids;
	Array<uint32_t>& prev_ids;

	WCC_Shortcut(Array<uint32_t>& i, Array<uint32_t>& p): ids(i), prev_ids(p) {}

	inline bool operator() (const VID& node) {
		uint32_t l = ids[ids[node]];
		if (ids[node] != l)
			ids[node] = l;

		if (prev_ids[node] != ids[node]) {
			prev_ids[node] = ids[node];
			return 1;

		} else {
			return 0;
		}
	}
};

int main(int argc, char **argv) {
	AgileStart(argc, argv);
	Runtime runtime(numComputeThreads, numIoThreads, ioBufferSize * MB);
    runtime.initBinning(binningRatio);

	Graph outGraph;
	outGraph.BuildGraph(outIndexFilename, outAdjFilenames);

	Graph inGraph;
	inGraph.BuildGraph(inIndexFilename, inAdjFilenames);

	uint64_t n = outGraph.NumberOfNodes();

	Array<uint32_t> ids;
	Array<uint32_t> prev_ids;
	ids.allocate(n);
	prev_ids.allocate(n);

    // Allocate bins
    unsigned nthreads = galois::getActiveThreads();
    uint64_t binSpaceBytes = (uint64_t)binSpace * MB;
    Bins *bins = new Bins(outGraph, nthreads, binSpaceBytes,
                          binCount, binBufSize, binningRatio);

	galois::do_all(galois::iterate(outGraph),
                     [&](const VID& node) {
                         prev_ids[node] = node;
                         ids[node] = node;
                     });

	Worklist<VID>* active = new Worklist<VID>(n);
	active->activate_all();

	galois::StatTimer time("Time", "WCC_MAIN");
	time.start();

	while (!active->empty()) {
		edgeMap(outGraph, active, WCC_F(ids, bins), no_output | prop_blocking);
        bins->reset();
		edgeMap(inGraph, active, WCC_F(ids, bins), no_output | prop_blocking);
        bins->reset();
		Worklist<VID>* output = vertexFilter(outGraph, WCC_Shortcut(ids, prev_ids));
		delete active;
		active = output;
	}
	delete active;

	time.stop();

    delete bins;

	findLargest(outGraph, ids);

	return 0;
}
