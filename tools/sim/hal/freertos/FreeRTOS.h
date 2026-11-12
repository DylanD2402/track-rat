// Stand-in for FreeRTOS (tools/build_sim.py): one thread. A task is queued, and tools/sim/sim.cpp runs it
// after the main loop's pass, to the end. Semaphores, mutexes and critical sections do nothing.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef uint32_t TickType_t;

#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFu
#define tskIDLE_PRIORITY 0
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) (ms)

typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))

#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio,
                                   TaskHandle_t* handle, BaseType_t core);
void vTaskDelete(TaskHandle_t);
inline void vTaskDelay(TickType_t) {}
inline BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdPASS; }
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (SemaphoreHandle_t)1; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
#ifdef __cplusplus
}
#endif
