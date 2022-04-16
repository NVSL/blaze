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

using namespace blaze;
namespace cll = llvm::cl;

static cll::opt<std::string>
		inIndexFilename("inIndexFilename", cll::desc("<in index file>"), cll::Required);

static cll::list<std::string>
		inAdjFilenames("inAdjFilenames", cll::desc("<in adj files>"), cll::OneOrMore);


template <typename T>
inline bool writeMin(T *a, T b) {
	T c;
	bool r = 0;
	do {
		c = *a;
	} while (c > b && !(r = compare_and_swap(*a, c, b)));
	return r;
}

struct WCC_F : public EDGEMAP_F<uint32_t> {
	Array<uint32_t>& ids;

	WCC_F(Array<uint32_t>& i): ids(i) {}

	inline bool update(VID src, VID dst) {
		uint32_t orig_id = ids[dst];
		if (ids[src] < orig_id) {
			ids[dst] = std::min(orig_id, ids[src]);
		}
		return 1;
	}

	inline bool updateAtomic(VID src, VID dst) {
		uint32_t orig_id = ids[dst];
		writeMin(&ids[dst], ids[src]);
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

	Graph outGraph;
	outGraph.BuildGraph(outIndexFilename, outAdjFilenames);

	Graph inGraph;
	inGraph.BuildGraph(inIndexFilename, inAdjFilenames);

	uint64_t n = outGraph.NumberOfNodes();

	Array<uint32_t> ids;
	Array<uint32_t> prev_ids;
	ids.allocate(n);
	prev_ids.allocate(n);

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
		edgeMap(outGraph, active, WCC_F(ids), no_output);
		edgeMap(inGraph, active, WCC_F(ids), no_output);
		Worklist<VID>* output = vertexFilter(outGraph, WCC_Shortcut(ids, prev_ids));
		delete active;
		active = output;
	}
	delete active;

	time.stop();

	findLargest(outGraph, ids);

	return 0;
}
