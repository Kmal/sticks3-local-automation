#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
typedef int BaseType_t;
#define pdPASS 1
#define tskIDLE_PRIORITY 0
#define portTICK_PERIOD_MS 1
#ifdef FREERTOS_TEST_HOOKS
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *ctx, int prio, TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t task);
#else
static inline BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *ctx, int prio, TaskHandle_t *handle) { (void)task; (void)name; (void)stack; (void)ctx; (void)prio; if (handle) { *handle = (TaskHandle_t)1; } return pdPASS; }
static inline void vTaskDelete(TaskHandle_t task) { (void)task; }
#endif
static inline void vTaskDelay(int ticks) { (void)ticks; }
static inline TickType_t xTaskGetTickCount(void) { return 0; }
