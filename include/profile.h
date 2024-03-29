#ifndef BLAZE_PROFILE_H
#define BLAZE_PROFILE_H

#include <cstring>

#ifdef BLAZE_USE_PAPI
extern "C" {
#include <papi.h>
#include <papiStdEventDefs.h>
}
#endif

#ifdef BLAZE_USE_PAPI

namespace internal {

unsigned long papiGetTID(void);

template <typename __T = void>
void papiInit() {

    /* Initialize the PAPI library */
    int retval = PAPI_library_init(PAPI_VER_CURRENT);

    if (retval != PAPI_VER_CURRENT && retval > 0) {
        BLAZE_DIE("PAPI library version mismatch!");
    }

    if (retval < 0) {
        BLAZE_DIE("Initialization error!");
    }

    if ((retval = PAPI_thread_init(&papiGetTID)) != PAPI_OK) {
        BLAZE_DIE("PAPI thread init failed");
    }
}

template <typename V1, typename V2>
void decodePapiEvents(const V1& eventNames, V2& papiEvents) {
    for (size_t i = 0; i < eventNames.size(); ++i) {
        char buf[256];
        std::strcpy(buf, eventNames[i].c_str());
        if (PAPI_event_name_to_code(buf, &papiEvents[i]) != PAPI_OK) {
            BLAZE_DIE("Failed to recognize eventName = ", eventNames[i],
                                 ", event code: ", papiEvents[i]);
        }
    }
}

template <typename V1, typename V2, typename V3>
void papiStart(V1& eventSets, V2& papiResults, V3& papiEvents) {
    galois::on_each([&](const unsigned tid, const unsigned numT) {
        if (PAPI_register_thread() != PAPI_OK) {
            BLAZE_DIE("Failed to register thread with PAPI");
        }

        int& eventSet = *eventSets.getLocal();

        eventSet = PAPI_NULL;
        papiResults.getLocal()->resize(papiEvents.size());

        if (PAPI_create_eventset(&eventSet) != PAPI_OK) {
            BLAZE_DIE("Failed to init event set");
        }
        if (PAPI_add_events(eventSet, papiEvents.data(), papiEvents.size()) !=
                PAPI_OK) {
            BLAZE_DIE("Failed to add events");
        }

        if (PAPI_start(eventSet) != PAPI_OK) {
            BLAZE_DIE("failed to start PAPI");
        }
    });
}

template <typename V1, typename V2, typename V3>
void papiStop(V1& eventSets, V2& papiResults, V3& eventNames,
                            const char* region) {
    galois::on_each([&](const unsigned tid, const unsigned numT) {
        int& eventSet = *eventSets.getLocal();

        if (PAPI_stop(eventSet, papiResults.getLocal()->data()) != PAPI_OK) {
            BLAZE_DIE("PAPI_stop failed");
        }

        if (PAPI_cleanup_eventset(eventSet) != PAPI_OK) {
            BLAZE_DIE("PAPI_cleanup_eventset failed");
        }

        if (PAPI_destroy_eventset(&eventSet) != PAPI_OK) {
            BLAZE_DIE("PAPI_destroy_eventset failed");
        }

        assert(eventNames.size() == papiResults.getLocal()->size() &&
                     "Both vectors should be of equal length");
        for (size_t i = 0; i < eventNames.size(); ++i) {
            galois::runtime::reportStat_Tsum(region, eventNames[i],
                                                                             (*papiResults.getLocal())[i]);
        }

        if (PAPI_unregister_thread() != PAPI_OK) {
            BLAZE_DIE("Failed to un-register thread with PAPI");
        }
    });
}

} // end namespace internal

template <typename F>
void profilePapi(const F& func, const char* region) {

    const char* const PAPI_VAR_NAME = "GALOIS_PAPI_EVENTS";
    region                                                  = region ? region : "(NULL)";

    std::string eventNamesCSV;

    if (!galois::substrate::EnvCheck(PAPI_VAR_NAME, eventNamesCSV) ||
            eventNamesCSV.empty()) {
        galois::gWarn(
                "No Events specified. Set environment variable GALOIS_PAPI_EVENTS");
        galois::timeThis(func, region);
        return;
    }

    internal::papiInit();

    std::vector<std::string> eventNames;

    galois::splitCSVstr(eventNamesCSV, eventNames);

    std::vector<int> papiEvents(eventNames.size());

    internal::decodePapiEvents(eventNames, papiEvents);

    galois::substrate::PerThreadStorage<int> eventSets;
    galois::substrate::PerThreadStorage<std::vector<long_long>> papiResults;

    internal::papiStart(eventSets, papiResults, papiEvents);

    galois::timeThis(func, region);

    internal::papiStop(eventSets, papiResults, eventNames, region);
}

#else

template <typename F>
void profilePapi(const F& func, const char* region) {

    region = region ? region : "(NULL)";
    galois::gWarn("PAPI not enabled or found");

    timeThis(func, region);
}

#endif

#endif // BLAZE_PROFILE_H
