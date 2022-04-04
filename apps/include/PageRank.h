#ifndef AGILE_PAGERANK_H
#define AGILE_PAGERANK_H

#include <string>
#include <map>
#include "Type.h"
#include "Array.h"

namespace blaze {

constexpr static const unsigned PRINT_TOP = 20;

// Verify result
struct Pair {
    float value;
    VID id;

    Pair(float v, VID i): value(v), id(i) {}

    bool operator<(const Pair& b) const {
        if (value == b.value)
            return id > b.id;
        return value < b.value;
    }
};

template <typename Node>
void printTop(Array<Node>& data, unsigned topn = PRINT_TOP) {
    typedef std::map<Pair, VID> TopMap;
    TopMap top;

    uint64_t n = data.size();
    for (uint64_t src = 0; src < n; src++) {
        Pair key(data[src].score, src);

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
    std::cout << "Rank PageRank Id\n";
    for (auto ii = top.rbegin(), ei = top.rend(); ii != ei; ++ii, ++rank) {
        float value = ii->first.value;
        VID node = ii->first.id;
        printf("%3d: %20.10f %10u\n", rank, value, node);
    }
}

} // namespace blaze

#endif // AGILE_PAGERANK_H

