/*
 * This file belongs to the Galois project, a C++ library for exploiting parallelism.
 * The code is being released under the terms of the 3-Clause BSD License (a
 * copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2018, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 */

#ifndef AGILE_UNIONFIND_H
#define AGILE_UNIONFIND_H

#include <atomic>

namespace blaze {
/**
 * Intrusive union-find implementation. Users subclass this to get disjoint
 * functionality for the subclass object.
 */
template <typename T>
class UnionFindNode {
    T* findImpl() const {
        if (isRep())
            return m_component.load(std::memory_order_relaxed);

        T* rep = m_component;
        while (rep->m_component != rep) {
            T* next = rep->m_component.load(std::memory_order_relaxed);
            rep     = next;
        }
        return rep;
    }

protected:
    std::atomic<T*> m_component;

    UnionFindNode(T* s) : m_component(s) {}

public:
    typedef UnionFindNode<T> SuperTy;

    bool isRep() const {
        return m_component.load(std::memory_order_relaxed) == this;
    }

    const T* find() const { return findImpl(); }

    T* find() { return findImpl(); }

    T* findAndCompress() {
        // Basic outline of race in synchronous path compression is that two path
        // compressions along two different paths to the root can create a cycle
        // in the union-find tree. Prevent that from happening by compressing
        // incrementally.
        if (isRep())
            return m_component.load(std::memory_order_relaxed);

        T* rep  = m_component;
        T* prev = 0;
        while (rep->m_component.load(std::memory_order_relaxed) != rep) {
            T* next = rep->m_component.load(std::memory_order_relaxed);

            if (prev && prev->m_component.load(std::memory_order_relaxed) == rep) {
                prev->m_component.store(next, std::memory_order_relaxed);
            }
            prev = rep;
            rep  = next;
        }

        return rep;
    }

    //! Lock-free merge. Returns if merge was done.
    T* merge(T* b) {
        T* a = m_component.load(std::memory_order_relaxed);
        while (true) {
            a = a->findAndCompress();
            b = b->findAndCompress();
            if (a == b)
                return 0;
            // Avoid cycles by directing edges consistently
            if (a < b)
                std::swap(a, b);
            if (a->m_component.compare_exchange_strong(a, b)) {
                return b;
            }
        }
    }
};

template <typename Graph>
void findLargest(Graph& graph, blaze::Array<uint32_t>& data) {

    using ReducerMap =
            galois::GMapPerItemReduce<uint32_t, int, std::plus<int>>;
    using Map = typename ReducerMap::container_type;

    using ComponentSizePair = std::pair<uint32_t, int>;

    ReducerMap accumMap;
    galois::GAccumulator<size_t> accumReps;

    galois::do_all(galois::iterate(graph),
                     [&](const VID& x) {
                         auto& n = data[x];
                         accumReps += 1;
                         accumMap.update(n, 1);
                     },
                     galois::loopname("CountLargest"));

    Map& map    = accumMap.reduce();
    size_t reps = accumReps.reduce();

    auto sizeMax = [](const ComponentSizePair& a, const ComponentSizePair& b) {
        if (a.second > b.second) {
            return a;
        }
        return b;
    };

    using MaxComp =
            galois::GSimpleReducible<decltype(sizeMax), ComponentSizePair>;
    MaxComp maxComp(sizeMax);

    galois::do_all(galois::iterate(map),
                    [&](const ComponentSizePair& x) { maxComp.update(x); });


    ComponentSizePair largest = maxComp.reduce();

    galois::GAccumulator<int> nonTrivialReps;
    galois::do_all(galois::iterate(map),
                    [&](const ComponentSizePair& x) { if (x.second > 1) nonTrivialReps += 1; });

    int nonTrivial = nonTrivialReps.reduce();

    // Compensate for dropping representative node of components
    size_t largestSize  = largest.second;
    double ratio        = (double)largestSize / graph.NumberOfNodes();

    std::cout << "Total components: " << reps << "\n";
    std::cout << "Number of non-trivial components: " << nonTrivial
              << " (largest component: " << largest.first << ", size: " << largestSize << " [" << ratio << "])\n";
}

} // namespace blaze
#endif
