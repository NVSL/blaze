#ifndef BLAZE_QUEUE_H
#define BLAZE_QUEUE_H

#include "concurrentqueue/concurrentqueue.h"
#include "concurrentqueue/blockingconcurrentqueue.h"

#include "RingBuffer.h"

template <typename T>
using MPMCQueue = moodycamel::ConcurrentQueue<T>;

template <typename T>
using SPSCQueue = blaze::RingBuffer<T>;

#endif  // BLAZE_QUEUE_H
