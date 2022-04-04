#ifndef AGILE_SET_H
#define AGILE_SET_H

#include "tbb/concurrent_unordered_set.h"

template <typename T>
using ConcurrentSet = tbb::concurrent_unordered_set<T>;

#endif  // AGILE_SET_H
