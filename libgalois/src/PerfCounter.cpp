#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "galois/PerfCounter.h"

#ifdef GALOIS_USE_PAPI

using namespace galois;

void PerfCounter::init()
{
    if (PAPI_VER_CURRENT != PAPI_library_init(PAPI_VER_CURRENT)) {
      printf("Error: PAPI_library_init\n");
      exit(-1);
    }

    if (PAPI_OK != PAPI_thread_init(pthread_self)) {
      printf("Error: PAPI_thread_init\n");
      exit(-1);
    }
    for (int i = 0; i < numThreads; i++) {
        perfStats.push_back(new PerfStat);
    }
}

void PerfCounter::deinit()
{
    PAPI_shutdown();
    for (int i = 0; i < numThreads; i++) {
        delete perfStats[i];
    }
}

void PerfCounter::start(PerfStat *stat)
{
    int native = 0x0;
    int event_set = PAPI_NULL;
    int retcode;

    retcode = PAPI_create_eventset(&event_set);
    if (retcode != PAPI_OK) { printf("Error: PAPI_create_eventset\n"); exit(-1); }

    retcode = PAPI_add_event(event_set, PAPI_TOT_INS);  // 0
    if (retcode != PAPI_OK) { printf("Error: PAPI_add_event\n"); exit(-1); }

    retcode = PAPI_add_event(event_set, PAPI_TOT_CYC);  // 1
    if (retcode != PAPI_OK) { printf("Error: PAPI_add_event\n"); exit(-1); }

    retcode = PAPI_add_event(event_set, PAPI_LD_INS);   // 2
    if (retcode != PAPI_OK) { printf("Error: PAPI_add_event\n"); exit(-1); }

    retcode = PAPI_add_event(event_set, PAPI_SR_INS);   // 3
    if (retcode != PAPI_OK) { printf("Error: PAPI_add_event\n"); exit(-1); }

    retcode = PAPI_event_name_to_code("MEM_LOAD_RETIRED:L3_HIT", &native);
    if (retcode != PAPI_OK) { printf("Error: PAPI_event_name_to_code\n"); exit(-1); }
    retcode = PAPI_add_event(event_set, native);        // 4
    if (retcode != PAPI_OK) { printf("Error: PAPI_add_event\n"); exit(-1); }

    retcode = PAPI_event_name_to_code("MEM_LOAD_RETIRED:L3_MISS", &native);
    if (retcode != PAPI_OK) { printf("Error: PAPI_event_name_to_code\n"); exit(-1); }
    retcode = PAPI_add_event(event_set, native);        // 5
    if (retcode != PAPI_OK) { printf("Error: PAPI_add_event\n"); exit(-1); }

    retcode = PAPI_event_name_to_code("SW_PREFETCH:T0", &native);
    if (retcode != PAPI_OK) { printf("Error: PAPI_event_name_to_code\n"); exit(-1); }
    retcode = PAPI_add_event(event_set, native);        // 6
    if (retcode != PAPI_OK) { printf("Error: PAPI_add_event\n"); exit(-1); }

    retcode = PAPI_event_name_to_code("CYCLE_ACTIVITY:CYCLES_L3_MISS", &native);
    if (retcode != PAPI_OK) { printf("Error: PAPI_event_name_to_code\n"); exit(-1); }
    retcode = PAPI_add_event(event_set, native);        // 7
    if (retcode != PAPI_OK) { printf("Error: PAPI_add_event\n"); exit(-1); }

    stat->event_set = event_set;

    retcode = PAPI_register_thread();
    if (retcode != PAPI_OK) { printf("Error: PAPI_register_thread\n"); exit(-1); }

    retcode = PAPI_start(stat->event_set);
    if (retcode != PAPI_OK) {
        printf("Error: PAPI_start: %d (%s)\n", retcode, PAPI_strerror(retcode));
        exit(-1);
    }
    stat->real_time[0] = PAPI_get_real_usec();
}

void PerfCounter::stop(PerfStat *stat)
{
    int retcode;
    stat->real_time[1] = PAPI_get_real_usec();
    retcode = PAPI_stop(stat->event_set, stat->event_values);
    if (retcode != PAPI_OK) {
        printf("Error: PAPI_stop: %d (%s)\n", retcode, PAPI_strerror(retcode));
        exit(-1);
    }
    long_long *event_values = stat->event_values;

    stat->time         = stat->real_time[1] - stat->real_time[0];
    stat->instructions = event_values[0];
    stat->total_cycles = event_values[1];
    stat->load_instructions  = event_values[2];
    stat->store_instructions = event_values[3];
    stat->ipc          = (float)stat->instructions / stat->total_cycles;

    stat->l3_hit       = event_values[4];
    stat->l3_miss      = event_values[5];
    stat->prefetch_t0 = event_values[6];
    stat->l3_miss_rate = (float)stat->l3_miss / (stat->l3_hit + stat->l3_miss) * 100;
    stat->l3_miss_cycles  = event_values[7];
    stat->l3_miss_time = (long_long)((float)stat->l3_miss_cycles / stat->total_cycles * stat->time);

    //memory_latency_in_ns = 64;
    //memory_latency_in_ns = (float)l3_miss_time / l3_miss * 1000;

    // DRAM
    stat->memory_latency_in_ns = 109;
    // PMEM
    //stat->memory_latency_in_ns = 396;
    //memory_latency_in_ns = (float)time / l3_miss * 1000;
    stat->memory_requests_per_sec = (float)stat->l3_miss / stat->time * 1000000;
    // Little's Law: memory requests/sec x memory latency (305ns)
    stat->mlp = stat->memory_requests_per_sec / 1000000000 * stat->memory_latency_in_ns;

    retcode = PAPI_unregister_thread();
    if (retcode != PAPI_OK) { printf("Error: PAPI_unregister_thread\n"); exit(-1); }
}

void PerfCounter::print(int tid) const
{
    PerfStat  *stat = perfStats[tid];
    printf("===== Thread %d =========\n", tid);
    printf("PERF, Time,           %lld\n", stat->time);
    printf("PERF, Instructions,   %lld\n", stat->instructions);
    printf("PERF, Load inst,      %lld\n", stat->load_instructions);
    printf("PERF, Store inst,     %lld\n", stat->store_instructions);
    printf("PERF, Cycles,         %lld\n", stat->total_cycles);
    printf("PERF, IPC,            %f\n",   stat->ipc);

    printf("PERF, Prefetch T0,    %lld\n", stat->prefetch_t0);
    printf("PERF, L3_Hits,        %lld\n", stat->l3_hit);
    printf("PERF, L3_Misses,      %lld\n", stat->l3_miss);
    printf("PERF, L3_Miss_Rate,   %.2f\n", stat->l3_miss_rate);
    printf("PERF, L3_Miss_Cycles, %lld\n", stat->l3_miss_cycles);
    printf("PERF, L3_Miss_Time,   %lld\n", stat->l3_miss_time);

    printf("PERF, Memory_Latency_In_Nanosec, %.0f\n", stat->memory_latency_in_ns);
    printf("PERF, Memory_Requests_Per_Sec, %.2f\n", stat->memory_requests_per_sec);
    printf("PERF, Effective_MLP,  %.2f\n", stat->mlp);
    printf("\n");
}

void PerfCounter::print_all() const
{
    for (int i = 0; i < numThreads; ++i) {
        print(i);
    }
    printf("PERF, Avg_IPC,  %.2f\n", globalPerfStat.ipc);
    printf("PERF, Total_Memory_Requests_Per_Sec, %.2f\n", globalPerfStat.memory_requests_per_sec);
    printf("PERF, Total_Effective_MLP,  %.2f\n", globalPerfStat.mlp);
}

void PerfCounter::gather_ipc()
{
    int nonZeroIPC = 0;
    for (int i = 0; i < numThreads; ++i) {
        if (perfStats[i]->ipc > 0) {
            globalPerfStat.ipc += perfStats[i]->ipc;
            nonZeroIPC++;
        }
    }
    globalPerfStat.ipc /= nonZeroIPC;
}

void PerfCounter::gather_mlp()
{
    for (int i = 0; i < numThreads; ++i) {
        globalPerfStat.memory_requests_per_sec += perfStats[i]->memory_requests_per_sec;
    }
    globalPerfStat.memory_latency_in_ns = perfStats[0]->memory_latency_in_ns;
    globalPerfStat.mlp = globalPerfStat.memory_requests_per_sec / 1000000000 * globalPerfStat.memory_latency_in_ns;
}

#endif  // GALOIS_USE_PAPI
