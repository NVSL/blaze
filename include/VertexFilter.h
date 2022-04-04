#ifndef BLAZE_VERTEXFILTER_H
#define BLAZE_VERTEXFILTER_H

#include "galois/Galois.h"

namespace blaze {

template <typename F>
Worklist<VID>* vertexFilter(Worklist<VID>* frontier, F&& filter) {
    Worklist<VID>* out = new Worklist<VID>(frontier->num_vertices());

    if (frontier->is_dense()) {
        out->to_dense();
        Bitmap *b = frontier->get_dense();
        uint64_t num_words = b->get_num_words();
        uint64_t *words = (uint64_t *)b->ptr();
        galois::do_all(galois::iterate(Bitmap::iterator(0), Bitmap::iterator(num_words)),
                        [&](uint64_t pos) {
                            uint64_t word = words[pos];
                            if (word) {
                                uint64_t mask = 0x1;
                                for (uint64_t i = 0; i < 64; i++, mask <<= 1) {
                                    if (word & mask) {
                                        uint64_t node = Bitmap::get_pos(pos, i);
                                        if (filter(node))
                                            out->activate(node);
                                    }
                                }
                            }
                        }, galois::no_stats(), galois::steal());
        out->set_dense(true);

    } else {
        auto sparse = frontier->get_sparse();
        galois::do_all(galois::iterate(*sparse),
                        [&](const VID& node) {
                            if (filter(node))
                                out->activate(node);
                        }, galois::no_stats(), galois::steal());
        out->set_dense(false);
    }

    return out;
}

template <typename G, typename F>
Worklist<VID>* vertexFilter(G& graph, F&& filter) {
    Worklist<VID>* out = new Worklist<VID>(graph.NumberOfNodes());
    out->to_dense();
    galois::do_all(galois::iterate(graph),
                     [&](const VID& node) {
                         if (filter(node)) out->activate(node);
                     }, galois::no_stats());
    return out;
}

} // namespace blaze

#endif // BLAZE_VERTEXFILTER_H
