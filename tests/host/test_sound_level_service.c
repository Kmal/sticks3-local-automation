#include "sound_level_service.h"
#include "board_i2s.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(value) do { if (!(value)) { fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #value); exit(1); } } while (0)
#define ASSERT_EQ(expected, actual) do { if ((expected) != (actual)) { fprintf(stderr, "%s:%d expected %d got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); exit(1); } } while (0)

int g_fake_semaphore_take_result = pdTRUE;
static size_t s_process_metrics_calls;

size_t rule_runtime_process_metrics(rule_runtime_t *runtime, const audio_level_metrics_t *metrics, uint32_t uptime_ms)
{
    (void)runtime;
    (void)metrics;
    (void)uptime_ms;
    s_process_metrics_calls++;
    return 3;
}

esp_err_t board_i2s_read_mono_i16(int16_t *dest, size_t max_samples, size_t *samples_read, uint32_t timeout_ms)
{
    (void)dest;
    (void)max_samples;
    (void)samples_read;
    (void)timeout_ms;
    return ESP_ERR_TIMEOUT;
}

static void test_sound_level_service_config_defaults(void)
{
    sound_level_service_config_t config;
    sound_level_service_config_defaults(&config);
    ASSERT_TRUE(config.enabled);
    ASSERT_EQ(16000, config.sample_rate_hz);
    ASSERT_EQ(100, config.window_ms);
    ASSERT_EQ(20, config.read_timeout_ms);
    ASSERT_EQ(256, config.read_samples);
    ASSERT_EQ(-60 * 256, config.metrics_config.floor_dbfs_q8);
    ASSERT_EQ(-12 * 256, config.metrics_config.loud_threshold_dbfs_q8);
    ASSERT_TRUE(config.calibrate_on_start);
    ASSERT_EQ(30, config.calibration_windows);
}

static void test_sound_level_service_calibration_updates_config(void)
{
    sound_level_service_t service;
    sound_level_service_config_t config;
    rule_runtime_t runtime;
    int fake_mutex = 1;
    sound_level_service_config_defaults(&config);
    ASSERT_TRUE(sound_level_service_init(&service, &runtime, &fake_mutex, &config));
    service.calibration.valid = true;
    service.calibration.noise_floor_dbfs_q8 = -50 * 256;
    service.calibration.loud_threshold_dbfs_q8 = -25 * 256;
    sound_level_service_test_apply_calibration(&service);
    ASSERT_EQ(-50 * 256, service.config.metrics_config.floor_dbfs_q8);
    ASSERT_EQ(-25 * 256, service.config.metrics_config.loud_threshold_dbfs_q8);
    ASSERT_EQ(0, service.config.metrics_config.ceiling_dbfs_q8);
}

static void test_sound_level_service_status_json(void)
{
    char json[512];
    ASSERT_TRUE(sound_level_service_build_status_json(NULL, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"enabled\":false") != NULL);
    ASSERT_TRUE(strstr(json, "audio_capture_disabled") != NULL);

    sound_level_service_t service;
    sound_level_service_config_t config;
    rule_runtime_t runtime;
    sound_level_service_config_defaults(&config);
    ASSERT_TRUE(sound_level_service_init(&service, &runtime, (void *)1, &config));
    service.state = SOUND_LEVEL_SERVICE_RUNNING;
    service.last_metrics_valid = true;
    service.last_metrics.rms_dbfs_q8 = -4096;
    service.last_metrics.peak_dbfs_q8 = -2048;
    service.last_metrics.rms_percent = 50;
    service.emitted_windows = 7;
    ASSERT_TRUE(sound_level_service_build_status_json(&service, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"running\":true") != NULL);
    ASSERT_TRUE(strstr(json, "\"state\":\"running\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"rms_dbfs_q8\":-4096") != NULL);

    service.state = SOUND_LEVEL_SERVICE_ERROR;
    ASSERT_TRUE(sound_level_service_build_status_json(&service, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"state\":\"error\"") != NULL);
    sound_level_service_deinit(&service);
}

static void test_sound_level_service_dropped_runtime_mutex_counter(void)
{
    sound_level_service_t service;
    sound_level_service_config_t config;
    rule_runtime_t runtime;
    int fake_mutex = 1;
    audio_level_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));

    sound_level_service_config_defaults(&config);
    ASSERT_TRUE(sound_level_service_init(&service, &runtime, &fake_mutex, &config));
    s_process_metrics_calls = 0;
    g_fake_semaphore_take_result = pdFALSE;
    sound_level_service_test_emit_metrics(&service, &metrics, 123);
    ASSERT_EQ(1, service.dropped_runtime_lock_count);
    ASSERT_EQ(0, s_process_metrics_calls);

    g_fake_semaphore_take_result = pdTRUE;
    sound_level_service_test_emit_metrics(&service, &metrics, 124);
    ASSERT_EQ(1, service.dropped_runtime_lock_count);
    ASSERT_EQ(1, s_process_metrics_calls);
}

int main(void)
{
    test_sound_level_service_config_defaults();
    test_sound_level_service_calibration_updates_config();
    test_sound_level_service_status_json();
    test_sound_level_service_dropped_runtime_mutex_counter();
    puts("sound_level_service tests passed");
    return 0;
}
