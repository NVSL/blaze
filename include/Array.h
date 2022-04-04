#ifndef BLAZE_ARRAY_H
#define BLAZE_ARRAY_H

#include "galois/ParallelSTL.h"
#include "mem.h"

namespace blaze {

template <typename T, bool OnPmem = false>
class Array {
    LAptr   m_realdata;
    T*      m_data;
    size_t  m_size;

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
    void allocate(size_type n) {
        assert(!m_data);
        m_size = n;
        m_realdata = largeMalloc(n * sizeof(T), OnPmem);
        m_data = reinterpret_cast<T*>(m_realdata.get());
    }

    void deallocate() {
        m_realdata.reset();
        m_data = nullptr;
        m_size = 0;
    }

    template <typename... Args>
    void constructAt(size_type n, Args&&... args) {
        new (&m_data[n]) T(std::forward<Args>(args)...);
    }

    void destroy() {
        if (!m_data)
            return;
        galois::ParallelSTL::destroy(m_data, m_data + m_size);
    }

public:
    Array() : m_data(nullptr), m_size(0) {}

    Array(Array&& o) : m_data(nullptr), m_size(0) {
        std::swap(this->m_data, o.m_data);
        std::swap(this->m_size, o.m_size);
    }

    Array& operator=(Array&& o) {
        std::swap(this->m_data, o.m_data);
        std::swap(this->m_size, o.m_size);
        return *this;
    }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    ~Array() {
        deallocate(); 
    }

    friend void swap(Array& lhs, Array& rhs) {
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

    // The following methods are not shared with void specialization
    const_pointer data() const { return m_data; }
    pointer data() { return m_data; }
    size_t bytes() const { return m_size * sizeof(T); }
};

} // namespace blaze

#endif // BLAZE_ARRAY_H
