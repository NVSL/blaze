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

#ifndef GALOIS_ARRAY_H
#define GALOIS_ARRAY_H

#include "galois/ParallelSTL.h"
#include "galois/Galois.h"
#include "galois/gIO.h"
#include "galois/runtime/Mem.h"
#include "galois/substrate/NumaMem.h"

#include <iostream> // TODO remove this once cerr is removed
#include <utility>

/*
 * Headers for boost serialization
 */
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/serialization.hpp>

namespace galois {

namespace runtime {
extern unsigned activeThreads;
} // end namespace runtime

/**
 * Large array of objects with proper specialization for void type and
 * supporting various allocation and construction policies.
 *
 * @tparam T value type of container
 */
template <typename T, bool OnPmem = false>
class Array {
  substrate::LAptr m_realdata;
  T* m_data;
  size_t m_size;
  bool m_mapped;

public:
  typedef T raw_value_type;
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef value_type& reference;
  typedef const value_type& const_reference;
  typedef value_type* pointer;
  typedef const value_type* const_pointer;
  typedef pointer iterator;
  typedef const_pointer const_iterator;
  const static bool has_value = true;

  // Extra indirection to support incomplete T's
  struct size_of {
    const static size_t value = sizeof(T);
  };

public:
  enum AllocType { Blocked, Local, Interleaved, Floating };
  void allocate(size_type n) {
    assert(!m_data);
    m_size = n;
    galois::gDebug("Local-allocd");
    m_realdata = substrate::largeMallocLocal(n * sizeof(T), OnPmem);
    m_data = reinterpret_cast<T*>(m_realdata.get());
    m_mapped = false;
  }

  void map(void* d, size_type n) {
    m_data = reinterpret_cast<T*>(d);
    m_size = n;
    m_mapped = true;
  }

public:
  Array(void* d, size_t s) : m_data(reinterpret_cast<T*>(d)), m_size(s), m_mapped(false) {}
  Array() : m_data(0), m_size(0), m_mapped(false) {}

  Array(Array&& o) : m_data(0), m_size(0), m_mapped(false) {
    std::swap(this->m_realdata, o.m_realdata);
    std::swap(this->m_data, o.m_data);
    std::swap(this->m_size, o.m_size);
  }

  Array& operator=(Array&& o) {
    std::swap(this->m_realdata, o.m_realdata);
    std::swap(this->m_data, o.m_data);
    std::swap(this->m_size, o.m_size);
    std::swap(this->m_mapped, o.m_mapped);
    return *this;
  }

  Array(const Array&) = delete;
  Array& operator=(const Array&) = delete;

  ~Array() {
    if (!m_mapped) {
      destroy();
      deallocate();
    }
  }

  friend void swap(Array& lhs, Array& rhs) {
    std::swap(lhs.m_realdata, rhs.m_realdata);
    std::swap(lhs.m_data, rhs.m_data);
    std::swap(lhs.m_size, rhs.m_size);
  }

  const_reference at(difference_type x) const { return m_data[x]; }
  reference at(difference_type x) { return m_data[x]; }
  const_reference operator[](size_type x) const { return m_data[x]; }
  reference operator[](size_type x) { return m_data[x]; }
  void set(difference_type x, const_reference v) { m_data[x] = v; }
  size_type size() const { return m_size; }
  iterator begin() { return m_data; }
  const_iterator begin() const { return m_data; }
  iterator end() { return m_data + m_size; }
  const_iterator end() const { return m_data + m_size; }

  template <typename... Args>
  void construct(Args&&... args) {
    for (T *ii = m_data, *ei = m_data + m_size; ii != ei; ++ii)
      new (ii) T(std::forward<Args>(args)...);
  }

  template <typename... Args>
  void constructAt(size_type n, Args&&... args) {
    new (&m_data[n]) T(std::forward<Args>(args)...);
  }

  //! Allocate and construct
  template <typename... Args>
  void create(size_type n, Args&&... args) {
    allocate(n);
    construct(std::forward<Args>(args)...);
  }

  void deallocate() {
    m_realdata.reset();
    m_data = 0;
    m_size = 0;
  }

  void destroy() {
    if (!m_data)
      return;
    galois::ParallelSTL::destroy(m_data, m_data + m_size);
  }

  template <typename U = T>
  std::enable_if_t<!std::is_scalar<U>::value> destroyAt(size_type n) {
    (&m_data[n])->~T();
  }

  template <typename U = T>
  std::enable_if_t<std::is_scalar<U>::value> destroyAt(size_type n) {}

  // The following methods are not shared with void specialization
  const_pointer data() const { return m_data; }
  pointer data() { return m_data; }
  size_t bytes() const { return m_size * sizeof(T); }
};

//! Void specialization
template <>
class Array<void> {

public:
  Array(void* d, size_t s) {}
  Array()                  = default;
  Array(const Array&) = delete;
  Array& operator=(const Array&) = delete;
  Array(Array&&) {}
  Array& operator=(Array&& o) { return *this; }

  friend void swap(Array&, Array&) {}

  typedef void raw_value_type;
  typedef void* value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef value_type reference;
  typedef const value_type const_reference;
  typedef value_type* pointer;
  typedef const value_type* const_pointer;
  typedef pointer iterator;
  typedef const_pointer const_iterator;
  const static bool has_value = false;
  struct size_of {
    const static size_t value = 0;
  };

  const_reference at(difference_type x) const { return 0; }
  reference at(difference_type x) { return 0; }
  const_reference operator[](size_type x) const { return 0; }
  template <typename AnyTy>
  void set(difference_type x, AnyTy v) {}
  size_type size() const { return 0; }
  iterator begin() { return 0; }
  const_iterator begin() const { return 0; }
  iterator end() { return 0; }
  const_iterator end() const { return 0; }

  void allocate(size_type n) {}
  void map(void* d, size_type n) {}

  template <typename... Args>
  void construct(Args&&... args) {}
  template <typename... Args>
  void constructAt(size_type n, Args&&... args) {}
  template <typename... Args>
  void create(size_type n, Args&&... args) {}

  void deallocate() {}
  void destroy() {}
  void destroyAt(size_type n) {}

  const_pointer data() const { return 0; }
  pointer data() { return 0; }
  size_t bytes() const { return 0; }
};

} // namespace galois
#endif
