#ifndef AGILE_QUEUE_H
#define AGILE_QUEUE_H

#include "concurrentqueue/concurrentqueue.h"
#include "concurrentqueue/blockingconcurrentqueue.h"

#include "RingBuffer.h"

template <typename T>
using MPMCQueue = moodycamel::ConcurrentQueue<T>;

template <typename T>
using SPSCQueue = agile::RingBuffer<T>;

#endif  // AGILE_QUEUE_H
