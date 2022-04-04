#ifndef GALOIS_ULL_H
#define GALOIS_ULL_H

#include "galois/Array.h"
#include "galois/LargeArray.h"
#include "galois/util.h"

namespace galois {

template <typename T>
class UnrolledLinkedList {

  struct ULLNode {
    LargeArray<T> items;
    size_t   size;
    size_t   pos;
    ULLNode* next;

    ULLNode(size_t size) {
      this->size = size;
      this->pos = 0;
      this->items.allocateLocal(size);
      this->next = nullptr;
    }
    ~ULLNode() {}
    void append(T item) {
      items[pos++] = item;
    }
    void appendItems(T* buffer, int count) {
      ntstore_64byte(&items[pos], buffer);
      pos += count;
    }
    bool empty() const {
      return pos == 0;
    }
    bool isFull() const {
      return pos == size;
    }
  };
  typedef ULLNode Node;

  Node *m_head;
  Node *m_tail;
  size_t m_node_size;

  void appendNode() {
    Node *node = new Node(m_node_size);
    m_tail->next = node;
    m_tail = node;
  }

  const static int default_node_size = 2 * 1024 * 1024;

public:
  UnrolledLinkedList() : m_node_size(default_node_size) {
    init();
  }
  UnrolledLinkedList(size_t node_size) : m_node_size(node_size) {
    init();
  }
  void init() {
    Node *node = new Node(m_node_size);
    m_head = m_tail = node;
  }
  ~UnrolledLinkedList() {
    Node *curr, *next;
    curr = m_head;
    while (curr) {
      next = curr->next;
      delete curr;
      curr = next;
    }
  }
  void append(T& item) {
    if (m_tail->isFull()) {
      appendNode();
    }
    m_tail->append(item);
  }
  void appendBatch(T* buffer, int count) {
    if (m_tail->isFull()) {
      appendNode();
    }
    m_tail->appendItems(buffer, count);
  }
  class iterator {
    Node *node;
    size_t pos;
    T* p;

    void increment() {
      if (pos + 1 == node->size) {
        node = node->next;
        if (node) p = node->items.begin();
        else      p = nullptr;
        pos = 0;
      } else {
        p++;
        pos++;
      }
    }
  public:
    iterator(Node *n, size_t p) : node(n), pos(p) {
      if (node->size <= pos) {
        pos = 0;
        this->p = nullptr;
      } else {
        this->p = &(node->items[pos]);
      }
    }
    iterator& operator++() { increment(); return *this; } // ++iter
    iterator operator++(int) { iterator retval = *this; ++(*this); return retval; }  // iter++
    bool operator==(iterator other) const { return p == other.p; }
    bool operator!=(iterator other) const { return !(*this == other); }
    T operator*() { return *p; }
    using difference_type = long;
    using value_type = T;
    using pointer = T*;
    using reference = T&;
    using iterator_category = std::forward_iterator_tag;
  };
  iterator begin() {
    return iterator(m_head, 0);
  }
  iterator end() {
    return iterator(m_tail, m_tail->pos);
  }
  size_t bytes() const {
    size_t ret = 0;
    Node* node = m_head;
    while (node) {
      ret += m_node_size;
      node = node->next;
    }
    return ret;
  }
};

} // namespace galois
#endif
