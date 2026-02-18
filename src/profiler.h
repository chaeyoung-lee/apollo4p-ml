/**
 * Profiling helpers: DWT cycle counter for high-level timing.
 * Only active when PROFILING is defined.
 */
#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PROFILING

/** Return current CPU cycle count (DWT). Call profiler_init() first. */
uint32_t profiler_get_cycles(void);

#endif /* PROFILING */

#ifdef __cplusplus
}
#endif

#endif /* PROFILER_H */
