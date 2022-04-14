#ifndef BLAZE_WORKLIST_H
#define BLAZE_WORKLIST_H

#include "galois/Galois.h"
#include "Bitmap.h"

namespace blaze {

template <typename T>
class CountableBag : public galois::InsertBag<T> {
 public:
    using parent = galois::InsertBag<T>;

    size_t count() {
        return _count.reduce();
    }

    void clear() {
        parent::clear();
        _count.reset();
    }

    void clear_serial() {
        parent::clear_serial();
        _count.reset();
    }

    //void pop() {
    //  parent::pop();
    //  _count -= 1;
    //}

    template <typename ItemTy>
    typename parent::reference push(ItemTy&& val) {
        _count += 1;
        return parent::push(val);
    }

    template <typename ItemTy>
    typename parent::reference push_back(ItemTy&& val) {
        _count += 1;
        return parent::push_back(val);
    }

 private:
    galois::GAccumulator<size_t> _count;
};

template <typename T>
class Worklist {
 public:
    // An empty worklist
    Worklist(size_t n): _n(n), _dense(nullptr), _is_dense(false) {
        _sparse = new CountableBag<T>();
    }

    // A worklist from dense
    Worklist(Bitmap *b)
        : _n(b->get_size()), _sparse(nullptr), _dense(b), _is_dense(true) {}

    // A worklist from sparse
    Worklist(size_t n, CountableBag<T> *s)
        : _n(n), _sparse(s), _dense(nullptr), _is_dense(false) {}

    ~Worklist() {
        if (_sparse)
            delete _sparse;
        if (_dense)
            delete _dense;
    }

    void activate(T val) {
        if (_is_dense)
            _dense->set_bit_atomic((size_t)val);
        else
            _sparse->push(val);
    }

    void activate_all() {
        if (!_is_dense)
            to_dense();
        _dense->set_all_parallel();
    }

    // use dense format for membership check
    bool activated(T val) const {
        assert(_dense);
        return _dense->get_bit((size_t)val);
    }

    void fill_dense() {
        if (_dense)
            _dense->reset_parallel();
        else
            _dense = new Bitmap(_n);

        galois::do_all(galois::iterate(*_sparse),
                        [&](const T& node) {
                            _dense->set_bit_atomic((size_t)node);
                        }, galois::steal());
    }

    void to_dense() {
        fill_dense();
        _is_dense = true;
    }

    void to_sparse() {
        if (_sparse)
            _sparse->clear();
        else
            _sparse = new CountableBag<T>();

        assert(_sparse->empty());

        uint64_t size = _dense->get_size();
        galois::do_all(galois::iterate(Bitmap::iterator(0), Bitmap::iterator(size)),
                        [&](uint64_t pos) {
                            if (_dense->get_bit(pos))
                                _sparse->push(pos);
                        }, galois::steal());
        _is_dense = false;
    }

    void set_dense(bool dense) {
        _is_dense = dense;
    }

    bool is_dense() const {
        return _is_dense;
    }

    size_t count() {
        if (_is_dense) {
            return _dense->count();
        } else {
            return _sparse->count();
        }
    }

    bool empty() {
        if (_is_dense) {
            return _dense->empty();
        } else {
            return _sparse->empty();
        }
    }

    void clear() {
        _dense->reset_parallel();
        _sparse->clear();
    }

    uint64_t num_vertices() const {
        return _dense->get_size();
    }

    Bitmap* get_dense() {
        return _dense;
    }

    CountableBag<T>* get_sparse() {
        return _sparse;
    }

    void set_sparse(CountableBag<T>* s) {
        _sparse = s;
    }

 private:
    size_t              _n;
    CountableBag<T>*    _sparse;
    Bitmap*             _dense;
    bool                _is_dense;
};

//template <typename T>
//using Worklist = galois::InsertBag<T>;
//template <typename T>
//using Worklist = blaze::CountableBag<T>;

} // namespace blaze

#endif // BLAZE_WORKLIST_H
