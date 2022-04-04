#ifndef BLAZE_RING_BUFFER_H
#define BLAZE_RING_BUFFER_H

#include <xmmintrin.h>

namespace blaze {

/*
 * Single writer, single reader ring buffer
 */
template <typename T>
class RingBuffer {
 public:
    RingBuffer(uint64_t capacity)
    :   _capacity(capacity),
        _head(0),
        _tail(0)
    {
        _data = (T *)calloc(capacity, sizeof(*_data));
    }

    ~RingBuffer() {
        free(_data);
    }

    void push(T& item) {
        _data[_head % _capacity] = item;
        _head = (_head + 1) & (_capacity - 1);
        _mm_sfence();
    }

    T pop() {
        T item = _data[_tail];
        _tail = (_tail + 1) & (_capacity - 1);
        _mm_sfence();
        return item;
    }

    bool isFull() const {
        _mm_lfence();
        uint32_t tail = _tail;
        return (((_head + 1) & (_capacity - 1)) == tail);
    }

    bool isEmpty() const {
        _mm_lfence();
        uint32_t head = _head;
        return head == _tail;
    }

 private:
    T*          _data;
    uint64_t    _capacity;
    uint32_t    _head;
    uint32_t    _tail;
};

}   // namespace blaze

#endif // BLAZE_RING_BUFFER_H
