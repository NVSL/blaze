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

#ifndef GALOIS_MAPPEDLARGEARRAY_H
#define GALOIS_MAPPEDLARGEARRAY_H

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
template <typename T>
class MappedLargeArray {
  T* m_data;
  size_t m_size;

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
  /**
   * Wraps existing buffer in MappedLargeArray interface.
   */
  MappedLargeArray(void* d, size_t s) : m_data(reinterpret_cast<T*>(d)), m_size(s) {}
  MappedLargeArray() : m_data(0), m_size(0) {}

  MappedLargeArray(MappedLargeArray&& o) : m_data(0), m_size(0) {
    std::swap(this->m_data, o.m_data);
    std::swap(this->m_size, o.m_size);
  }

  MappedLargeArray& operator=(MappedLargeArray&& o) {
    std::swap(this->m_data, o.m_data);
    std::swap(this->m_size, o.m_size);
    return *this;
  }

  MappedLargeArray(const MappedLargeArray&) = delete;
  MappedLargeArray& operator=(const MappedLargeArray&) = delete;

  ~MappedLargeArray() {}

  friend void swap(MappedLargeArray& lhs, MappedLargeArray& rhs) {
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

  const_pointer data() const { return m_data; }
  pointer data() { return m_data; }
  size_t bytes() const { return m_size * sizeof(T); }
};

//! Void specialization
template <>
class MappedLargeArray<void> {

public:
  MappedLargeArray(void* d, size_t s) {}
  MappedLargeArray()                  = default;
  MappedLargeArray(const MappedLargeArray&) = delete;
  MappedLargeArray& operator=(const MappedLargeArray&) = delete;
  MappedLargeArray(MappedLargeArray&&) {}
  MappedLargeArray& operator=(MappedLargeArray&& o) { return *this; }

  friend void swap(MappedLargeArray&, MappedLargeArray&) {}

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

  const_pointer data() const { return 0; }
  pointer data() { return 0; }
  size_t bytes() const { return 0; }
};

} // namespace galois
#endif
