// WASI's <time.h> plus tzset(), which the firmware calls after setting the time zone (tools/sim/sim.cpp has it:
// the browser build keeps UTC, and the firmware does its own local-time maths).
#pragma once
#include_next <time.h>
#ifdef __cplusplus
extern "C" {
#endif
void tzset(void);
#ifdef __cplusplus
}
#endif
