#ifndef GALOIS_PERFCOUNTER_H
#define GALOIS_PERFCOUNTER_H

#include <vector>
#include <string.h>

#ifdef GALOIS_USE_PAPI
#include <papi.h>

namespace galois {

class PerfStat {
public:
    PerfStat() {
        event_set = PAPI_NULL;
        memset(event_values, 0, sizeof(long_long)*8);
        memset(real_time, 0, sizeof(long_long)*2);
        time = 0;
        instructions = 0;
        load_instructions  = 0;
        store_instructions = 0;
        total_cycles = 0;
        prefetch_t0  = 0;
        ipc          = 0.0;
        l3_hit       = 0;
        l3_miss      = 0;
        l3_miss_rate = 0.0;
        l3_miss_cycles = 0;
        l3_miss_time   = 0;
        memory_latency_in_ns    = 0.0;
        memory_requests_per_sec = 0.0;
        mlp = 0.0;
    }
    int         event_set;
    long_long   event_values[8];
    long_long   real_time[2];
    long_long   time;
    long_long   instructions;
    long_long   load_instructions;
    long_long   store_instructions;
    long_long   total_cycles;
    long_long   prefetch_t0;
    float       ipc;
    long_long   l3_hit;
    long_long   l3_miss;
    float       l3_miss_rate;
    long_long   l3_miss_cycles;
    long_long   l3_miss_time;
    float       memory_latency_in_ns;
    float       memory_requests_per_sec;
    float       mlp;
};

class PerfCounter {
public:
    PerfCounter(int threads): numThreads(threads) { init(); }
    ~PerfCounter() { deinit(); }
    void init();
    void deinit();
    static void start(PerfStat *stat);
    static void stop(PerfStat *stat);
    void print(int tid) const;
    void print_all() const;
    void gather_ipc();
    void gather_mlp();
public:
    int             numThreads;
    std::vector<PerfStat*>  perfStats;
    PerfStat        globalPerfStat;
};

class PerfCounterGuard : public PerfCounter {
public:
    PerfCounterGuard(): PerfCounter(1) {
        PerfCounter::start(this->perfStats[0]);
    }
    ~PerfCounterGuard() {
        PerfCounter::stop(this->perfStats[0]);
        print(0);
    }
};

} // end namespace galois

#endif  // GALOIS_USE_PAPI
#endif  // GALOIS_PERFCOUNTER_H
