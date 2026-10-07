#include "uac_service.h"

#include "sdkconfig.h"

#ifndef CONFIG_APP_USB_UAC_MIC
#define CONFIG_APP_USB_UAC_MIC 0
#endif
#ifndef CONFIG_APP_USB_UAC_SPEAKER
#define CONFIG_APP_USB_UAC_SPEAKER 0
#endif
#ifndef CONFIG_APP_USB_UAC_COMBINED_EXPERIMENTAL
#define CONFIG_APP_USB_UAC_COMBINED_EXPERIMENTAL 0
#endif
#ifndef CONFIG_APP_USB_UAC_SIMULTANEOUS_MIC_SPEAKER
#define CONFIG_APP_USB_UAC_SIMULTANEOUS_MIC_SPEAKER 0
#endif
#if CONFIG_APP_USB_UAC_DEVICE
#include "board_audio.h"
#include "board_i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uac_audio_buffer.h"
#include "uac_config.h"
#include "uac_device_adapter.h"
#include "uac_esp_device.h"
#include "uac_mic_source.h"
#include "uac_speaker_sink.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

static const char *TAG = "UAC_SERVICE";

typedef struct {
    uac_audio_runtime_config_t runtime;
    uac_audio_buffer_t mic_buffer;
    uac_audio_buffer_t speaker_buffer;
    uint8_t *mic_storage;
    uint8_t *speaker_storage;
    uac_device_adapter_t adapter;
    TaskHandle_t mic_task;
    TaskHandle_t speaker_task;
    atomic_bool running;
    bool start_attempted;
    bool simultaneous_owner;
    bool mic_bridge_task_enabled;
    bool speaker_bridge_task_enabled;
} uac_service_t;

static uac_service_t s_uac;

static bool uac_service_has_mic(uac_audio_mode_t mode)
{
    return mode == UAC_AUDIO_MODE_MIC_ONLY || mode == UAC_AUDIO_MODE_COMBINED_SERIALIZED ||
           mode == UAC_AUDIO_MODE_SIMULTANEOUS_MIC_SPEAKER;
}

static bool uac_service_has_speaker(uac_audio_mode_t mode)
{
    return mode == UAC_AUDIO_MODE_SPEAKER_ONLY || mode == UAC_AUDIO_MODE_COMBINED_SERIALIZED ||
           mode == UAC_AUDIO_MODE_SIMULTANEOUS_MIC_SPEAKER;
}

static void uac_service_release_buffers(uac_service_t *svc)
{
    free(svc->mic_storage);
    free(svc->speaker_storage);
    svc->mic_storage = NULL;
    svc->speaker_storage = NULL;
}

static void uac_service_mic_task(void *ctx)
{
    uac_service_t *svc = (uac_service_t *)ctx;
    int16_t samples[128];
    while (true) {
        if (!atomic_load_explicit(&svc->running, memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t samples_read = 0;
        esp_err_t err = svc->simultaneous_owner
                            ? board_i2s_read_mono_i16(samples, sizeof(samples) / sizeof(samples[0]), &samples_read, 20)
                            : uac_mic_source_read_i16(samples, sizeof(samples) / sizeof(samples[0]), &samples_read, 20);
        if (err == ESP_OK && samples_read > 0) {
            (void)uac_audio_buffer_write(&svc->mic_buffer, (const uint8_t *)samples, samples_read * sizeof(samples[0]));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static void uac_service_speaker_task(void *ctx)
{
    uac_service_t *svc = (uac_service_t *)ctx;
    int16_t samples[128];
    while (true) {
        if (!atomic_load_explicit(&svc->running, memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        (void)uac_audio_buffer_read_or_silence(&svc->speaker_buffer, (uint8_t *)samples, sizeof(samples));
        size_t samples_written = 0;
        if (svc->simultaneous_owner) {
            size_t bytes_written = 0;
            (void)board_i2s_write(samples, sizeof(samples), &bytes_written, 20);
            samples_written = bytes_written / sizeof(samples[0]);
        } else {
            (void)uac_speaker_sink_write_i16(samples, sizeof(samples) / sizeof(samples[0]), &samples_written, 20);
        }
        if (samples_written == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static esp_err_t uac_service_init_audio(uac_service_t *svc)
{
    if (svc->runtime.mode == UAC_AUDIO_MODE_SIMULTANEOUS_MIC_SPEAKER) {
        const board_audio_config_t config = {
            .profile = BOARD_AUDIO_PROFILE_SIMULTANEOUS_MIC_SPEAKER,
            .probe_m5pm1 = true,
            .require_audio_power_enable = true,
        };
        svc->simultaneous_owner = true;
        svc->mic_bridge_task_enabled = true;
        svc->speaker_bridge_task_enabled = true;
        return board_audio_init(&config);
    }

    if (svc->runtime.mode == UAC_AUDIO_MODE_COMBINED_SERIALIZED) {
        ESP_LOGW(TAG, "combined UAC descriptors are enabled without audio bridge tasks; mic returns silence and speaker data is buffered only");
        return ESP_OK;
    }

    if (svc->runtime.mode == UAC_AUDIO_MODE_MIC_ONLY) {
        esp_err_t err = uac_mic_source_init(NULL);
        if (err == ESP_OK) {
            err = uac_mic_source_start();
        }
        if (err == ESP_OK) {
            svc->mic_bridge_task_enabled = true;
        }
        return err;
    }
    if (svc->runtime.mode == UAC_AUDIO_MODE_SPEAKER_ONLY) {
        esp_err_t err = uac_speaker_sink_init(NULL);
        if (err != ESP_OK) {
            return err;
        }
        err = uac_speaker_sink_start();
        if (err == ESP_OK) {
            err = uac_speaker_sink_set_volume_percent(UAC_AUDIO_SAFE_MAX_VOLUME_PERCENT);
        }
        if (err == ESP_OK) {
            svc->speaker_bridge_task_enabled = true;
        }
        return err;
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t uac_service_start_from_kconfig(void)
{
    if (s_uac.start_attempted) return ESP_ERR_INVALID_STATE;
    s_uac.start_attempted = true;
    uac_audio_config_t config = {
        .uac_enabled = CONFIG_APP_USB_UAC_DEVICE,
        .mic_enabled = CONFIG_APP_USB_UAC_MIC,
        .speaker_enabled = CONFIG_APP_USB_UAC_SPEAKER,
        .combined_experimental = CONFIG_APP_USB_UAC_COMBINED_EXPERIMENTAL,
        .simultaneous_mic_speaker_enabled = CONFIG_APP_USB_UAC_SIMULTANEOUS_MIC_SPEAKER,
        .sample_rate_hz = CONFIG_APP_USB_UAC_SAMPLE_RATE_HZ,
        .ring_buffer_bytes = CONFIG_APP_USB_UAC_RING_BUFFER_BYTES,
    };
    esp_err_t err = uac_audio_config_resolve(&config, &s_uac.runtime);
    if (err != ESP_OK) {
        return err;
    }
    if (s_uac.runtime.mode == UAC_AUDIO_MODE_DISABLED) {
        return ESP_OK;
    }

    const bool has_mic = uac_service_has_mic(s_uac.runtime.mode);
    const bool has_speaker = uac_service_has_speaker(s_uac.runtime.mode);
    if (has_mic) {
        s_uac.mic_storage = calloc(1, s_uac.runtime.ring_buffer_bytes);
        if (s_uac.mic_storage == NULL) {
            uac_service_release_buffers(&s_uac);
            return ESP_ERR_NO_MEM;
        }
        err = uac_audio_buffer_init(&s_uac.mic_buffer, s_uac.mic_storage, s_uac.runtime.ring_buffer_bytes);
        if (err != ESP_OK) {
            uac_service_release_buffers(&s_uac);
            return err;
        }
    }
    if (has_speaker) {
        s_uac.speaker_storage = calloc(1, s_uac.runtime.ring_buffer_bytes);
        if (s_uac.speaker_storage == NULL) {
            uac_service_release_buffers(&s_uac);
            return ESP_ERR_NO_MEM;
        }
        err = uac_audio_buffer_init(&s_uac.speaker_buffer, s_uac.speaker_storage, s_uac.runtime.ring_buffer_bytes);
        if (err != ESP_OK) {
            uac_service_release_buffers(&s_uac);
            return err;
        }
    }

    err = uac_service_init_audio(&s_uac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UAC audio init failed: %s", esp_err_to_name(err));
        (void)uac_mic_source_stop();
        (void)uac_speaker_sink_stop();
        (void)board_audio_deinit();
        uac_service_release_buffers(&s_uac);
        return err;
    }

    err = uac_device_adapter_init(&s_uac.adapter, &s_uac.runtime, has_mic ? &s_uac.mic_buffer : NULL,
                                  has_speaker ? &s_uac.speaker_buffer : NULL);
    if (err != ESP_OK) {
        (void)uac_mic_source_stop();
        (void)uac_speaker_sink_stop();
        (void)board_audio_deinit();
        uac_service_release_buffers(&s_uac);
        return err;
    }
    /* Allocate bridge tasks before registering callbacks. They wait for running. */
    if (s_uac.mic_bridge_task_enabled && xTaskCreate(uac_service_mic_task, "uac_mic", 4096, &s_uac, tskIDLE_PRIORITY + 5, &s_uac.mic_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto task_failure;
    }
    if (s_uac.speaker_bridge_task_enabled && xTaskCreate(uac_service_speaker_task, "uac_spk", 4096, &s_uac, tskIDLE_PRIORITY + 5, &s_uac.speaker_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto task_failure;
    }
    err = uac_esp_device_start(&s_uac.adapter);
    if (err != ESP_OK) {
        /* usb_device_uac 1.2.3 exposes no stop/deinit API. Its failure may
         * leave callbacks registered: retain callback storage until reboot. */
        if (s_uac.mic_task != NULL) vTaskDelete(s_uac.mic_task);
        if (s_uac.speaker_task != NULL) vTaskDelete(s_uac.speaker_task);
        s_uac.mic_task = s_uac.speaker_task = NULL;
        (void)uac_mic_source_stop();
        (void)uac_speaker_sink_stop();
        (void)board_audio_deinit();
        return err;
    }
    atomic_store_explicit(&s_uac.running, true, memory_order_release);
    ESP_LOGI(TAG, "UAC service started: mode=%s rate=%lu", uac_audio_mode_name(s_uac.runtime.mode),
             (unsigned long)s_uac.runtime.sample_rate_hz);
    return ESP_OK;
task_failure:
    if (s_uac.mic_task != NULL) vTaskDelete(s_uac.mic_task);
    if (s_uac.speaker_task != NULL) vTaskDelete(s_uac.speaker_task);
    s_uac.mic_task = s_uac.speaker_task = NULL;
    (void)uac_mic_source_stop();
    (void)uac_speaker_sink_stop();
    (void)board_audio_deinit();
    uac_service_release_buffers(&s_uac);
    return err;
}
#else
esp_err_t uac_service_start_from_kconfig(void)
{
    return ESP_OK;
}
#endif
