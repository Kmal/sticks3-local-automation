#include "sound_level_service.h"

#include "board_i2s.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#ifndef CONFIG_APP_SOUND_LEVEL_SAMPLE_RATE_HZ
#define CONFIG_APP_SOUND_LEVEL_SAMPLE_RATE_HZ 16000
#endif
#ifndef CONFIG_APP_SOUND_LEVEL_WINDOW_MS
#define CONFIG_APP_SOUND_LEVEL_WINDOW_MS 100
#endif
#ifndef CONFIG_APP_SOUND_LEVEL_READ_TIMEOUT_MS
#define CONFIG_APP_SOUND_LEVEL_READ_TIMEOUT_MS 20
#endif
#ifndef CONFIG_APP_SOUND_LEVEL_CALIBRATE_ON_START
#define CONFIG_APP_SOUND_LEVEL_CALIBRATE_ON_START 0
#endif
#ifndef CONFIG_APP_SOUND_LEVEL_CALIBRATION_WINDOWS
#define CONFIG_APP_SOUND_LEVEL_CALIBRATION_WINDOWS 30
#endif

static void sound_level_task(void *ctx);
static bool sound_level_read_and_accumulate(sound_level_service_t *service);
static bool sound_level_finalize_window(sound_level_service_t *service, uint32_t uptime_ms);
static void sound_level_apply_calibration(sound_level_service_t *service);
static void sound_level_emit_metrics(sound_level_service_t *service,
                                     const audio_level_metrics_t *metrics,
                                     uint32_t uptime_ms);
static uint32_t sound_level_uptime_ms(void);

void sound_level_service_config_defaults(sound_level_service_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->enabled = true;
    config->sample_rate_hz = CONFIG_APP_SOUND_LEVEL_SAMPLE_RATE_HZ;
    config->window_ms = CONFIG_APP_SOUND_LEVEL_WINDOW_MS;
    config->read_timeout_ms = CONFIG_APP_SOUND_LEVEL_READ_TIMEOUT_MS;
    config->read_samples = 256;
    config->metrics_config.floor_dbfs_q8 = -60 * 256;
    config->metrics_config.ceiling_dbfs_q8 = 0;
    config->metrics_config.loud_threshold_dbfs_q8 = -12 * 256;
    config->calibrate_on_start = CONFIG_APP_SOUND_LEVEL_CALIBRATE_ON_START;
    config->calibration_windows = CONFIG_APP_SOUND_LEVEL_CALIBRATION_WINDOWS;
}

bool sound_level_service_init(sound_level_service_t *service,
                              rule_runtime_t *runtime,
                              SemaphoreHandle_t runtime_mutex,
                              const sound_level_service_config_t *config)
{
    if (service == NULL || runtime == NULL || runtime_mutex == NULL || config == NULL) {
        return false;
    }

    memset(service, 0, sizeof(*service));
    service->status_mutex = xSemaphoreCreateMutex();
    if (service->status_mutex == NULL) return false;
    atomic_init(&service->state, SOUND_LEVEL_SERVICE_STOPPED);
    atomic_init(&service->task, NULL);
    atomic_init(&service->stop_requested, false);
    atomic_init(&service->read_errors, 0);
    atomic_init(&service->emitted_windows, 0);
    atomic_init(&service->underrun_windows, 0);
    atomic_init(&service->dropped_runtime_lock_count, 0);
    service->runtime = runtime;
    service->runtime_mutex = runtime_mutex;
    service->config = *config;
    service->next_metrics_sequence = 1;

    audio_metrics_accumulator_init(&service->accumulator,
                                   service->config.sample_rate_hz,
                                   service->config.window_ms);
    audio_calibration_init(&service->calibration);

    if (service->config.calibrate_on_start) {
        audio_calibration_begin(&service->calibration,
                                service->config.calibration_windows);
    }

    return true;
}

bool sound_level_service_start(sound_level_service_t *service)
{
    if (service == NULL || service->task != NULL || !service->config.enabled) {
        return false;
    }

    service->state = SOUND_LEVEL_SERVICE_STARTING;
    TaskHandle_t task = NULL;
    BaseType_t ok = xTaskCreate(sound_level_task,
                                "sound_level",
                                4096,
                                service,
                                tskIDLE_PRIORITY + 1,
                                &task);
    if (ok != pdPASS) {
        service->state = SOUND_LEVEL_SERVICE_ERROR;
        service->task = NULL;
        return false;
    }
    service->task = task;
    return true;
}

void sound_level_service_deinit(sound_level_service_t *service)
{
    if (service == NULL || service->task != NULL) return;
    if (service->status_mutex != NULL) vSemaphoreDelete(service->status_mutex);
    service->status_mutex = NULL;
}

void sound_level_service_request_stop(sound_level_service_t *service)
{
    if (service != NULL) {
        service->stop_requested = true;
    }
}

bool sound_level_service_is_running(const sound_level_service_t *service)
{
    return service != NULL && service->state == SOUND_LEVEL_SERVICE_RUNNING;
}

bool sound_level_service_get_last_metrics(const sound_level_service_t *service, audio_level_metrics_t *out)
{
    if (service == NULL || out == NULL || service->status_mutex == NULL ||
        xSemaphoreTake(service->status_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    const bool valid = service->last_metrics_valid;
    if (valid) *out = service->last_metrics;
    xSemaphoreGive(service->status_mutex);
    return valid;
}

sound_level_service_state_t sound_level_service_get_state(const sound_level_service_t *service)
{
    return service != NULL ? service->state : SOUND_LEVEL_SERVICE_STOPPED;
}

const char *sound_level_service_state_name(sound_level_service_state_t state)
{
    switch (state) {
    case SOUND_LEVEL_SERVICE_STOPPED: return "stopped";
    case SOUND_LEVEL_SERVICE_STARTING: return "starting";
    case SOUND_LEVEL_SERVICE_RUNNING: return "running";
    case SOUND_LEVEL_SERVICE_CALIBRATING: return "calibrating";
    case SOUND_LEVEL_SERVICE_ERROR: return "error";
    default: return "unknown";
    }
}

bool sound_level_service_build_status_json(const sound_level_service_t *service, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }

    if (service == NULL) {
        int written = snprintf(out, out_len,
                               "{\"enabled\":false,\"running\":false,"
                               "\"state\":\"disabled\","
                               "\"reason\":\"audio_capture_disabled\"}");
        return written > 0 && (size_t)written < out_len;
    }

    if (service->status_mutex == NULL || xSemaphoreTake(service->status_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const audio_level_metrics_t *m = &service->last_metrics;
    int written = snprintf(out, out_len,
                           "{\"enabled\":true,"
                           "\"running\":%s,"
                           "\"state\":\"%s\","
                           "\"last_metrics_valid\":%s,"
                           "\"rms_dbfs_q8\":%ld,"
                           "\"peak_dbfs_q8\":%ld,"
                           "\"rms_percent\":%u,"
                           "\"peak_percent\":%u,"
                           "\"clipped_samples\":%u,"
                           "\"emitted_windows\":%lu,"
                           "\"read_errors\":%lu,"
                           "\"underrun_windows\":%lu,"
                           "\"dropped_runtime_lock_count\":%lu}",
                           service->state == SOUND_LEVEL_SERVICE_RUNNING ? "true" : "false",
                           sound_level_service_state_name(service->state),
                           service->last_metrics_valid ? "true" : "false",
                           (long)m->rms_dbfs_q8,
                           (long)m->peak_dbfs_q8,
                           (unsigned)m->rms_percent,
                           (unsigned)m->peak_percent,
                           (unsigned)m->clipped_samples,
                           (unsigned long)service->emitted_windows,
                           (unsigned long)service->read_errors,
                           (unsigned long)service->underrun_windows,
                           (unsigned long)service->dropped_runtime_lock_count);
    xSemaphoreGive(service->status_mutex);
    return written > 0 && (size_t)written < out_len;
}

static void sound_level_task(void *ctx)
{
    sound_level_service_t *service = (sound_level_service_t *)ctx;
    service->state = service->calibration.active ? SOUND_LEVEL_SERVICE_CALIBRATING : SOUND_LEVEL_SERVICE_RUNNING;

    while (!service->stop_requested) {
        if (!sound_level_read_and_accumulate(service)) {
            continue;
        }

        if (audio_metrics_accumulator_ready(&service->accumulator)) {
            uint32_t uptime_ms = sound_level_uptime_ms();
            (void)sound_level_finalize_window(service, uptime_ms);
        }
    }

    service->state = SOUND_LEVEL_SERVICE_STOPPED;
    service->task = NULL;
    vTaskDelete(NULL);
}

static bool sound_level_read_and_accumulate(sound_level_service_t *service)
{
    int16_t samples[256];
    size_t samples_read = 0;
    const size_t max_read_samples = service->config.read_samples < (sizeof(samples) / sizeof(samples[0]))
                                        ? service->config.read_samples
                                        : (sizeof(samples) / sizeof(samples[0]));
    esp_err_t err = board_i2s_read_mono_i16(samples,
                                            max_read_samples,
                                            &samples_read,
                                            service->config.read_timeout_ms);
    if (err != ESP_OK || samples_read == 0) {
        service->last_error = err;
        service->read_errors++;
        service->consecutive_read_errors++;
        if (service->consecutive_read_errors > 50) {
            service->state = SOUND_LEVEL_SERVICE_ERROR;
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return false;
    }

    service->consecutive_read_errors = 0;
    service->state = service->calibration.active ? SOUND_LEVEL_SERVICE_CALIBRATING : SOUND_LEVEL_SERVICE_RUNNING;

    audio_metrics_accumulator_add_i16(&service->accumulator, samples, samples_read);
    return true;
}

static bool sound_level_finalize_window(sound_level_service_t *service, uint32_t uptime_ms)
{
    audio_level_metrics_t metrics;
    if (!audio_metrics_accumulator_finalize_with_config(&service->accumulator,
                                                       service->next_metrics_sequence++,
                                                       &service->config.metrics_config,
                                                       &metrics)) {
        return false;
    }

    if (xSemaphoreTake(service->status_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        service->last_metrics = metrics;
        service->last_metrics_valid = true;
        xSemaphoreGive(service->status_mutex);
    }

    if (service->calibration.active) {
        audio_calibration_add_window(&service->calibration, &metrics);
        if (audio_calibration_ready(&service->calibration) && audio_calibration_finalize(&service->calibration)) {
            sound_level_apply_calibration(service);
            service->state = SOUND_LEVEL_SERVICE_RUNNING;
        }
        return true;
    }

    sound_level_emit_metrics(service, &metrics, uptime_ms);
    service->emitted_windows++;
    return true;
}

static void sound_level_apply_calibration(sound_level_service_t *service)
{
    if (!service->calibration.valid) {
        return;
    }

    service->config.metrics_config.floor_dbfs_q8 = service->calibration.noise_floor_dbfs_q8;
    service->config.metrics_config.loud_threshold_dbfs_q8 = service->calibration.loud_threshold_dbfs_q8;
    service->config.metrics_config.ceiling_dbfs_q8 = 0;
}

#ifdef SOUND_LEVEL_SERVICE_ENABLE_TEST_HOOKS
void sound_level_service_test_apply_calibration(sound_level_service_t *service)
{
    sound_level_apply_calibration(service);
}

void sound_level_service_test_emit_metrics(sound_level_service_t *service,
                                           const audio_level_metrics_t *metrics,
                                           uint32_t uptime_ms)
{
    sound_level_emit_metrics(service, metrics, uptime_ms);
}
#endif

static void sound_level_emit_metrics(sound_level_service_t *service,
                                     const audio_level_metrics_t *metrics,
                                     uint32_t uptime_ms)
{
    if (xSemaphoreTake(service->runtime_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        service->dropped_runtime_lock_count++;
        return;
    }

    (void)rule_runtime_process_metrics(service->runtime, metrics, uptime_ms);
    xSemaphoreGive(service->runtime_mutex);
}

static uint32_t sound_level_uptime_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
