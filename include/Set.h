#ifndef BLAZE_SET_H
#define BLAZE_SET_H

#include "tbb/concurrent_unordered_set.h"

template <typename T>
using ConcurrentSet = tbb::concurrent_unordered_set<T>;

#endif  // BLAZE_SET_H
