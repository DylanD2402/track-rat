// WASI's <sys/time.h> plus settimeofday(), which the firmware sets the clock with (tools/sim/sim.cpp has it).
#pragma once
#include_next <sys/time.h>
#ifdef __cplusplus
extern "C" {
#endif
int settimeofday(const struct timeval* tv, const void* tz);
#ifdef __cplusplus
}
#endif
