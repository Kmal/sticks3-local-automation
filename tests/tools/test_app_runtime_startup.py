#!/usr/bin/env python3
"""Execute the firmware's startup function with injected allocation failures."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/app/main.c').read_text()
start = source.index('static bool app_rule_runtime_init(void)')
end = source.index('\n\n#if CONFIG_APP_SOUND_LEVEL_TRIGGERS\nstatic bool app_sound_level_status_json', start)
startup = source[start:end]
prefix = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define CONFIG_APP_SOUND_LEVEL_TRIGGERS 0
#define CONFIG_APP_SPEAKER_ACTION 1
#define CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define portMAX_DELAY 0
#define pdPASS 1
#define tskIDLE_PRIORITY 0
#define TAG "test"
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef int BaseType_t;
typedef struct { int dispatcher, hardware_facts; } fake_runtime_t;
static fake_runtime_t s_rule_runtime;
static struct { bool opened; } s_rule_store = {true};
static int s_rule_config;
static bool s_rule_runtime_ready;
static SemaphoreHandle_t s_rule_mutex;
static TaskHandle_t s_rule_gpio_task, s_rule_network_task, s_rule_ble_task;
static bool fail_mutex, fail_core;
static int fail_task, task_calls, deleted_tasks, stopped_workers, core_calls;
static bool rule_config_store_open(void *store) { return true; }
static bool rule_config_store_load(void *store, int *config) { return true; }
static void automation_config_set_defaults(int *config) {}
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return fail_mutex ? NULL : (void *)1; }
static int xSemaphoreTake(SemaphoreHandle_t mutex, int timeout) { assert(mutex); return 1; }
static void xSemaphoreGive(SemaphoreHandle_t mutex) { assert(mutex); }
static bool rule_runtime_init(fake_runtime_t *runtime, int *config) { core_calls++; return !fail_core; }
static void app_send_http_rule_action(void) {}
static void app_send_ir_rule_action(void) {}
static void app_send_local_ui_rule_action(void) {}
static void app_send_speaker_rule_action(void) {}
static void app_send_ble_rule_action(void) {}
#define SENDER(name) static void rule_runtime_set_##name##_sender(fake_runtime_t *runtime, void (*cb)(void), void *ctx) {}
SENDER(http)
SENDER(ir)
SENDER(local_ui)
SENDER(speaker)
SENDER(ble)
static void app_rule_gpio_poll_task(void *ctx) {}
static void app_rule_network_state_task(void *ctx) {}
static void app_rule_ble_state_task(void *ctx) {}
static BaseType_t xTaskCreate(void (*fn)(void *), const char *name, int stack, void *ctx, int priority, TaskHandle_t *out) {
    task_calls++;
    if (task_calls == fail_task) { *out = NULL; return 0; }
    *out = (void *)(uintptr_t)task_calls;
    return pdPASS;
}
static void vTaskDelete(TaskHandle_t task) { assert(task); deleted_tasks++; }
static void action_dispatcher_stop(int *dispatcher) { stopped_workers++; }
static void hardware_fact_service_deinit(int *hardware) {}
'''
suffix = r'''
static void reset(void) {
    fail_mutex = false; fail_core = false; fail_task = 0;
    task_calls = deleted_tasks = stopped_workers = core_calls = 0;
    s_rule_runtime_ready = false;
    s_rule_mutex = NULL;
    s_rule_gpio_task = s_rule_network_task = s_rule_ble_task = NULL;
}
int main(void) {
    reset(); fail_mutex = true;
    assert(!app_rule_runtime_init());
    assert(!s_rule_runtime_ready && core_calls == 0 && task_calls == 0);
    reset(); fail_core = true;
    assert(!app_rule_runtime_init());
    assert(!s_rule_runtime_ready && core_calls == 1 && task_calls == 0);
    for (int i = 1; i <= 3; i++) {
        reset(); fail_task = i;
        assert(!app_rule_runtime_init());
        assert(!s_rule_runtime_ready && task_calls == i);
        assert(deleted_tasks == i - 1 && stopped_workers == 1);
        assert(!s_rule_gpio_task && !s_rule_network_task && !s_rule_ble_task);
    }
    reset();
    assert(app_rule_runtime_init());
    assert(s_rule_runtime_ready && task_calls == 3 && stopped_workers == 0);
    puts("app runtime startup failure tests passed");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'startup.c').write_text(prefix + startup + suffix)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', str(path / 'startup.c'), '-o', str(path / 'startup')], check=True)
    subprocess.run([str(path / 'startup')], check=True)
