/**
 * Profiling: DWT cycle counter for high-level timing.
 * Only built with PROFILING.
 */
#include "profiler.h"

#ifdef PROFILING

#include "am_mcu_apollo.h"

uint32_t profiler_get_cycles(void)
{
    return DWT->CYCCNT;
}

#endif /* PROFILING */
