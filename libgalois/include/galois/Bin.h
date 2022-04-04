#ifndef GALOIS_BIN_H
#define GALOIS_BIN_H

#include "galois/Array.h"
#include "galois/LargeArray.h"
#include "galois/LargePmemArray.h"
#include "galois/util.h"

namespace galois {

template <typename U>
using Pool = LargeArray<U>;
template <typename U>
using Bin = std::vector<std::pair<int, U*>>;

template <typename T>
class Bins {
  Pool<T>  m_pool;
  uint64_t m_pool_allocated;
  int  m_count;
  int  m_width_bits;
  std::vector<Bin<T>> m_bins;
  T*                  m_buffer;
  uint8_t*            m_buffer_idx;

public:
  typedef T value_type;
  const static int write_buffer_size = 64;
  const static int write_buffer_items = write_buffer_size / sizeof(T);
  const static int bin_chunk_size = 32 * 1024* 1024;
  const static int bin_chunk_items = bin_chunk_size / sizeof(T);
  struct item_size {
    const static size_t value = sizeof(T);
  };

public:
  Bins(int count, int width_bits) : m_count(count), m_width_bits(width_bits) {
    for (int i = 0; i < count; i++) {
      m_bins.emplace_back();
    }
    m_buffer = (T*)aligned_alloc(write_buffer_size,
                       m_count * write_buffer_size);
    m_buffer_idx = new uint8_t[count]();
    m_pool_allocated = 0;
  }
  T* allocate_from_pool() {
    // TODO: lock
    T* chunk = m_pool.data() + m_pool_allocated;
    m_pool_allocated += bin_chunk_items;
    // TODO: unlock
    return chunk;
  }
  void allocate(size_t pool_size) {
    m_pool.allocateLocal(pool_size);
    for (int i = 0; i < m_count; i++) {
      T* chunk = allocate_from_pool();
      m_bins[i].push_back(std::make_pair(0, chunk));
    }
  }
  ~Bins() {
    free(m_buffer);
    delete m_buffer_idx;
  }
  T* get_buffer(int bin_idx) { return m_buffer + bin_idx * write_buffer_items; }
  void append(T item) {
    uint32_t i = item.key >> m_width_bits;
    T* buf = get_buffer(i);
    *(buf + m_buffer_idx[i]++) = item;
    if (m_buffer_idx[i] == write_buffer_items) {
      flush(i);
      if (m_bins[i].back().first == bin_chunk_items) {
        T* chunk = allocate_from_pool();
        m_bins[i].push_back(std::make_pair(0, chunk));
      }
    }
  }
  void flush(int bin_idx) {
    T* dst;
    int idx;
    std::tie(idx, dst) = m_bins[bin_idx].back();
    dst += idx;
    T* buf = get_buffer(bin_idx);
    ntstore_64byte(dst, buf);
    m_bins[bin_idx].back().first += m_buffer_idx[bin_idx];
    m_buffer_idx[bin_idx] = 0;
  }
  void flush_all() {
    for (int i = 0; i < m_count; i++) {
      if (m_buffer_idx[i] > 0) flush(i);
    }
  }
  Bin<T>& operator[](int i) {
    return m_bins[i];
  }
  Bin<T>& at(int i) {
    return m_bins[i];
  }
//T* begin(int i) { return m_bins[i].begin(); }
//T* end(int i) { return m_bins[i].begin() + m_bin_count[i]; }
  int size() const { return m_count; }
//size_t bytes() const {
//  size_t ret = 0;
//  for (int i = 0; i < m_count; i++) {
//    ret += m_bins[i]->bytes();
//  }
//  return ret;
//}
};

} // namespace galois
#endif
