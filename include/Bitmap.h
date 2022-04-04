// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#ifndef BITMAP_H_
#define BITMAP_H_

#include <algorithm>
#include <cinttypes>

#include "atomics.h"
#include "filesystem.h"
#include <boost/iterator/counting_iterator.hpp>
#include "galois/Galois.h"


/*
GAP Benchmark Suite
Class:  Bitmap
Author: Scott Beamer

Parallel bitmap that is thread-safe
 - Can set bits in parallel (set_bit_atomic) unlike std::vector<bool>
*/


class Bitmap {
 public:
    using iterator = boost::counting_iterator<uint64_t>;

 public:
    explicit Bitmap(size_t size) {
        uint64_t num_words = (size + kBitsPerWord - 1) / kBitsPerWord;
        start_ = new uint64_t[num_words];
        end_ = start_ + num_words;
        num_words_ = num_words;
        size_ = size;
        reset_parallel();
    }

    ~Bitmap() {
        delete[] start_;
    }

    void reset() {
        std::fill(start_, end_, 0);
    }

    void reset_parallel() {
        galois::do_all(galois::iterate(iterator(0), iterator(num_words_)),
                       [&](uint64_t word) { start_[word] = 0; },
                       galois::no_stats());
    }

    void set_all() {
        std::fill(start_, end_, 0xffffffffffffffff);
    }

    void set_all_parallel() {
        galois::do_all(galois::iterate(iterator(0), iterator(num_words_)),
                       [&](uint64_t word) { start_[word] = 0xffffffffffffffff; },
                       galois::no_stats());
    }

    void set_bit(size_t pos) {
        start_[word_offset(pos)] |= ((uint64_t) 1l << bit_offset(pos));
    }

    void set_bit_atomic(size_t pos) {
        uint64_t old_val, new_val;
        do {
            old_val = start_[word_offset(pos)];
            new_val = old_val | ((uint64_t) 1l << bit_offset(pos));
        } while (!compare_and_swap(start_[word_offset(pos)], old_val, new_val));
    }

    bool try_set_bit_atomic(size_t pos) {
        uint64_t old_val, new_val;
        while (!get_bit(pos)) {
            old_val = start_[word_offset(pos)];
            new_val = old_val | ((uint64_t) 1l << bit_offset(pos));
            if (compare_and_swap(start_[word_offset(pos)], old_val, new_val)) return true;
        }
        return false;
    }

    bool get_bit(size_t pos) const {
        return (start_[word_offset(pos)] >> bit_offset(pos)) & 1l;
    }

    uint64_t get_word(uint64_t pos) const {
        return start_[pos];
    }

    void set_word(uint64_t pos, uint64_t word) {
        start_[pos] = word;
    }

    uint64_t get_num_words() const {
        return num_words_;
    }

    uint64_t get_size() const {
        return size_;
    }

    void swap(Bitmap &other) {
        std::swap(start_, other.start_);
        std::swap(end_, other.end_);
    }

    void save(const std::string& file_name) {
        uint64_t len = bytes();
        char* addr = create_and_map_file(file_name, len);
        memcpy(addr, ptr(), len);
        msync(addr, len, MS_SYNC);
        munmap(addr, len);
    }

    void* ptr() const {
        return (void*)start_;
    }

    uint64_t bytes() const {
        return num_words_ * sizeof(uint64_t);
    }

    size_t count() const {
        galois::GAccumulator<size_t> total_cnt;
        galois::do_all(galois::iterate(iterator(0), iterator(get_num_words())),
                        [&](uint64_t pos) {
                            uint64_t word = get_word(pos);
                            int cnt = 0;
                            uint64_t mask = 0x1;
                            for (int i = 0; i < 64; i++, mask <<= 1) {
                                if (word & mask) cnt++;
                            }
                            total_cnt += cnt;
                        }, galois::no_stats());
        return total_cnt.reduce();
    }

    bool empty() const {
        galois::GReduceLogicalAND is_empty;
        galois::do_all(galois::iterate(iterator(0), iterator(get_num_words())),
                        [&](uint64_t pos) {
                            uint64_t word = get_word(pos);
                                if (word)
                                    is_empty.update(false);
                        }, galois::no_stats());
        return is_empty.reduce();
    }

    iterator begin() const { return iterator(0); }
    iterator end() const { return iterator(size_); }

    static void or_bitmaps(std::vector<Bitmap *>& in_bitmaps, Bitmap *out_bitmap) {
        uint64_t num_words = in_bitmaps[0]->get_num_words();
        uint64_t size = in_bitmaps[0]->get_size();
        int num_bitmaps = in_bitmaps.size();

        galois::do_all(galois::iterate(iterator(0), iterator(num_words)),
                        [&](uint64_t pos) {
                            uint64_t ret = 0;
                            for (int i = 0; i < num_bitmaps; i++) {
                                ret |= in_bitmaps[i]->get_word(pos);
                            }
                            out_bitmap->set_word(pos, ret);
                        }, galois::no_stats());
    }

    static void and_bitmap(Bitmap *b1, Bitmap *b2) {
        uint64_t num_words = b1->get_num_words();
        galois::do_all(galois::iterate(iterator(0), iterator(num_words)),
                        [&](uint64_t pos) {
                            uint64_t word = b1->get_word(pos) & b2->get_word(pos);
                            b1->set_word(pos, word);
                        }, galois::no_stats());
    }

 public:
    static const uint64_t kBitsPerWord = 64;
    static uint64_t word_offset(size_t n) { return n >> 6; }
    static uint64_t bit_offset(size_t n) { return n & (kBitsPerWord - 1); }
    static uint64_t get_pos(uint64_t pos, uint64_t offset) { return (pos << 6) | offset; }
    static size_t pos_in_next_word(uint64_t pos) { return ((pos >> 6) + 1) << 6; }

 private:
    uint64_t*   start_;
    uint64_t*   end_;
    uint64_t    num_words_;
    uint64_t    size_;
};

#endif  // BITMAP_H_
