#pragma once

#include "audio_metrics.h"
#include "rule_runtime.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

typedef enum {
    SOUND_LEVEL_SERVICE_STOPPED = 0,
    SOUND_LEVEL_SERVICE_STARTING,
    SOUND_LEVEL_SERVICE_RUNNING,
    SOUND_LEVEL_SERVICE_CALIBRATING,
    SOUND_LEVEL_SERVICE_ERROR,
} sound_level_service_state_t;

typedef struct {
    bool enabled;
    uint32_t sample_rate_hz;
    uint32_t window_ms;
    uint32_t read_timeout_ms;
    size_t read_samples;
    audio_metrics_config_t metrics_config;
    bool calibrate_on_start;
    uint32_t calibration_windows;
} sound_level_service_config_t;

typedef struct {
    _Atomic(sound_level_service_state_t) state;
    rule_runtime_t *runtime;
    SemaphoreHandle_t runtime_mutex;
    _Atomic(TaskHandle_t) task;
    SemaphoreHandle_t status_mutex;
    sound_level_service_config_t config;
    audio_metrics_accumulator_t accumulator;
    audio_calibration_t calibration;
    audio_level_metrics_t last_metrics;
    uint32_t next_metrics_sequence;
    _Atomic uint32_t emitted_windows;
    _Atomic uint32_t read_errors;
    uint32_t consecutive_read_errors;
    _Atomic uint32_t underrun_windows;
    _Atomic uint32_t dropped_runtime_lock_count;
    esp_err_t last_error;
    bool last_metrics_valid;
    _Atomic bool stop_requested;
} sound_level_service_t;

void sound_level_service_config_defaults(sound_level_service_config_t *config);

bool sound_level_service_init(sound_level_service_t *service,
                              rule_runtime_t *runtime,
                              SemaphoreHandle_t runtime_mutex,
                              const sound_level_service_config_t *config);

bool sound_level_service_start(sound_level_service_t *service);
void sound_level_service_deinit(sound_level_service_t *service);
void sound_level_service_request_stop(sound_level_service_t *service);
bool sound_level_service_is_running(const sound_level_service_t *service);

bool sound_level_service_get_last_metrics(const sound_level_service_t *service,
                                          audio_level_metrics_t *out);

sound_level_service_state_t sound_level_service_get_state(const sound_level_service_t *service);
const char *sound_level_service_state_name(sound_level_service_state_t state);

bool sound_level_service_build_status_json(const sound_level_service_t *service,
                                           char *out,
                                           size_t out_len);

#ifdef SOUND_LEVEL_SERVICE_ENABLE_TEST_HOOKS
void sound_level_service_test_apply_calibration(sound_level_service_t *service);
void sound_level_service_test_emit_metrics(sound_level_service_t *service,
                                           const audio_level_metrics_t *metrics,
                                           uint32_t uptime_ms);
#endif
