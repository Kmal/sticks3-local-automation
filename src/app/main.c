#include <stdatomic.h>
/*
 * Main firmware entry point for the M5Stack StickS3 configuration and local
 * automation application.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "action_http.h"
#include "action_ir.h"
#if CONFIG_APP_SPEAKER_ACTION
#include "action_speaker.h"
#endif
#include "app_mode.h"
#include "app_time.h"
#include "app_wifi.h"
#include "app_sound_level_demand.h"
#include "sdkconfig.h"
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
#include "board_audio.h"
#include "sound_level_service.h"
#endif
#include "board_sticks3.h"
#include "rule_config_store.h"
#include "rule_runtime.h"
#include "rule_web.h"
#include "status_ui.h"
#include "ui_model.h"
#if CONFIG_APP_USB_UAC_DEVICE
#include "uac_service.h"
#endif


#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
#include "transport_ble_gatt.h"
#endif

static const char *TAG = "STICKS3_APP";
static app_runtime_state_t s_runtime_state;
static rule_runtime_t s_rule_runtime;
static rule_config_store_t s_rule_store;
static rule_web_t s_rule_web;
static automation_config_t s_rule_config;
static atomic_bool s_rule_runtime_ready;
static SemaphoreHandle_t s_rule_mutex;
static TaskHandle_t s_rule_gpio_task;
static TaskHandle_t s_rule_network_task;
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
static sound_level_service_t *s_sound_level_service;
static SemaphoreHandle_t s_audio_mutex;
static TaskHandle_t s_sound_manager_task;
static bool s_sound_level_ready;
static bool s_sound_level_audio_initialized;
static app_sound_level_demand_t s_sound_level_demand;
#endif
#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
static TaskHandle_t s_rule_ble_task;
#endif

#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
static bool app_sound_level_status_json(char *out, size_t out_len, void *ctx);
static void app_invalidate_sound_facts(void);
static void app_sound_level_release_audio(void);
static void app_sound_level_release_if_stopped(void);
static bool app_sound_level_stop(void);
static void app_sound_level_sync(bool needed);
static void app_sound_manager(void *ctx);
#endif

static esp_err_t app_network_stack_init(void)
{
    ESP_LOGI(TAG, "network stack init: starting");
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network stack init: esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "network stack init: esp_netif ready");

    err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "network stack init: default event loop already exists");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network stack init: default event loop failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "network stack init: default event loop ready");
    return ESP_OK;
}

static action_result_t app_send_http_rule_action(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    action_result_t result = {
        .code = ACTION_RESULT_OK,
    };
    if (event != NULL) {
        result.sequence = event->sequence;
        result.rule_id = event->rule_id;
        result.action = event->action;
    }
    action_http_result_t http = action_http_post_event(event != NULL ? &event->action_config : NULL, event);
    if (http == ACTION_HTTP_RESULT_NOT_READY) {
        result.code = ACTION_RESULT_NOT_STARTED;
    } else if (http != ACTION_HTTP_RESULT_OK) {
        result.code = ACTION_RESULT_UNSUPPORTED;
    }
    return result;
}


static action_result_t app_send_ir_rule_action(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    action_result_t result = {
        .code = ACTION_RESULT_OK,
    };
    if (event != NULL) {
        result.sequence = event->sequence;
        result.rule_id = event->rule_id;
        result.action = event->action;
    }
    if (!action_ir_send_event(event)) {
        result.code = ACTION_RESULT_UNSUPPORTED;
    }
    return result;
}


#if CONFIG_APP_SPEAKER_ACTION
static action_result_t app_send_speaker_rule_action(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    action_result_t result = {
        .code = ACTION_RESULT_OK,
    };
    if (event != NULL) {
        result.sequence = event->sequence;
        result.rule_id = event->rule_id;
        result.action = event->action;
    }
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
    app_invalidate_sound_facts();
    /* All codec transitions have the same owner lock. Never take the rule
     * mutex while holding this lock; capture may need it while stopping. */
    if (s_audio_mutex == NULL || xSemaphoreTake(s_audio_mutex, portMAX_DELAY) != pdTRUE) {
        result.code = ACTION_RESULT_NOT_STARTED;
        return result;
    }
    const bool capture_stopped = app_sound_level_stop();
#else
    const bool capture_stopped = true;
#endif
    if (!capture_stopped || !action_speaker_send_event(event)) result.code = ACTION_RESULT_UNSUPPORTED;
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
    xSemaphoreGive(s_audio_mutex);
    app_invalidate_sound_facts();
    /* The demand manager restores capture after releasing the playback owner. */
#endif
    return result;
}
#endif


static action_result_t app_send_local_ui_rule_action(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    action_result_t result = {
        .code = ACTION_RESULT_OK,
    };
    if (event != NULL) {
        result.sequence = event->sequence;
        result.rule_id = event->rule_id;
        result.action = event->action;
    }
    status_ui_set_state(STATUS_UI_STATE_READY);
    return result;
}

#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
static action_result_t app_send_ble_rule_action(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    action_result_t result = {
        .code = ACTION_RESULT_OK,
    };
    if (event != NULL) {
        result.sequence = event->sequence;
        result.rule_id = event->rule_id;
        result.action = event->action;
    }
    esp_err_t err = transport_ble_send_rule_event(event);
    if (err == ESP_ERR_INVALID_STATE) {
        result.code = ACTION_RESULT_NOT_STARTED;
    } else if (err != ESP_OK) {
        result.code = ACTION_RESULT_UNSUPPORTED;
    }
    return result;
}
#endif




static void app_emit_wifi_connected_rule_fact(bool connected, uint32_t uptime_ms)
{
    trigger_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.source = RULE_SOURCE_WIFI_CONNECTED;
    fact.value = rule_value_bool(connected);
    fact.uptime_ms = uptime_ms;
    if (s_rule_mutex != NULL && xSemaphoreTake(s_rule_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        (void)trigger_emit_fact(&s_rule_runtime.trigger_adapter, &fact);
        xSemaphoreGive(s_rule_mutex);
    }
}

static void app_rule_network_state_task(void *ctx)
{
    (void)ctx;
    while (true) {
        if (!s_rule_runtime_ready) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        const bool ready = action_http_network_ready();
        const uint32_t uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        app_emit_wifi_connected_rule_fact(ready, uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
static void app_emit_ble_connected_rule_fact(bool connected, uint32_t uptime_ms)
{
    trigger_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.source = RULE_SOURCE_BLE_CONNECTED;
    fact.value = rule_value_bool(connected);
    fact.uptime_ms = uptime_ms;
    if (s_rule_mutex != NULL && xSemaphoreTake(s_rule_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        (void)trigger_emit_fact(&s_rule_runtime.trigger_adapter, &fact);
        xSemaphoreGive(s_rule_mutex);
    }
}

static void app_rule_ble_state_task(void *ctx)
{
    (void)ctx;
    while (true) {
        if (!s_rule_runtime_ready) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        const bool connected = transport_ble_gatt_is_connected();
        const uint32_t uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        app_emit_ble_connected_rule_fact(connected, uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

static void app_emit_button_rule_fact(button_state_event_t event)
{
    if (!s_rule_runtime_ready) return;
    const uint32_t uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (s_rule_mutex != NULL && xSemaphoreTake(s_rule_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        (void)rule_runtime_process_button_event(&s_rule_runtime, event, uptime_ms);
        xSemaphoreGive(s_rule_mutex);
    }
}

static void app_rule_gpio_poll_task(void *ctx)
{
    (void)ctx;
    while (true) {
        if (!s_rule_runtime_ready) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        const uint32_t uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (s_rule_mutex != NULL && xSemaphoreTake(s_rule_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            (void)rule_runtime_poll_gpio(&s_rule_runtime, uptime_ms);
            (void)rule_runtime_poll_hardware(&s_rule_runtime, uptime_ms);
            (void)rule_runtime_tick(&s_rule_runtime, uptime_ms);
            xSemaphoreGive(s_rule_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool app_rule_runtime_init(void)
{
    ESP_LOGI(TAG, "rule runtime init: loading automation config");
    if (!rule_config_store_open(&s_rule_store) || !rule_config_store_load(&s_rule_store, &s_rule_config)) {
        automation_config_set_defaults(&s_rule_config);
        ESP_LOGW(TAG, "rule runtime init: using default automation config");
    } else {
        ESP_LOGI(TAG, "rule runtime init: automation config loaded from NVS");
    }
    if (s_rule_mutex == NULL) {
        s_rule_mutex = xSemaphoreCreateMutex();
        ESP_LOGI(TAG, "rule runtime init: mutex %s", s_rule_mutex != NULL ? "created" : "create failed");
    }
    if (s_rule_mutex == NULL || !s_rule_store.opened) return false;
    (void)xSemaphoreTake(s_rule_mutex, portMAX_DELAY);
    if (!rule_runtime_init(&s_rule_runtime, &s_rule_config)) {
        xSemaphoreGive(s_rule_mutex);
        return false;
    }
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
    s_audio_mutex = xSemaphoreCreateMutex();
    if (s_audio_mutex == NULL) goto failed;
    rule_web_set_sound_status_builder(app_sound_level_status_json, NULL);
#endif
    ESP_LOGI(TAG, "rule runtime init: core runtime ready");
    rule_runtime_set_http_sender(&s_rule_runtime, app_send_http_rule_action, NULL);
    rule_runtime_set_ir_sender(&s_rule_runtime, app_send_ir_rule_action, NULL);
    rule_runtime_set_local_ui_sender(&s_rule_runtime, app_send_local_ui_rule_action, NULL);
#if CONFIG_APP_SPEAKER_ACTION
    rule_runtime_set_speaker_sender(&s_rule_runtime, app_send_speaker_rule_action, NULL);
#endif
#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
    rule_runtime_set_ble_sender(&s_rule_runtime, app_send_ble_rule_action, NULL);
#endif
    ESP_LOGI(TAG, "rule runtime init: web server deferred until Web UI is enabled");
    if (s_rule_gpio_task == NULL) {
        BaseType_t created = xTaskCreate(app_rule_gpio_poll_task, "rule_gpio_poll", 3072, NULL, tskIDLE_PRIORITY + 1, &s_rule_gpio_task);
        ESP_LOGI(TAG, "rule runtime init: gpio poll task %s", created == pdPASS ? "created" : "create failed");
        if (created != pdPASS) goto failed;
    }
    if (s_rule_network_task == NULL) {
        BaseType_t created = xTaskCreate(app_rule_network_state_task, "rule_net_state", 3072, NULL, tskIDLE_PRIORITY + 1, &s_rule_network_task);
        ESP_LOGI(TAG, "rule runtime init: network state task %s", created == pdPASS ? "created" : "create failed");
        if (created != pdPASS) goto failed;
    }
#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
    if (s_rule_ble_task == NULL) {
        BaseType_t created = xTaskCreate(app_rule_ble_state_task, "rule_ble_state", 3072, NULL, tskIDLE_PRIORITY + 1, &s_rule_ble_task);
        ESP_LOGI(TAG, "rule runtime init: BLE state task %s", created == pdPASS ? "created" : "create failed");
        if (created != pdPASS) goto failed;
    }
#endif
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
    if (xTaskCreate(app_sound_manager, "sound_owner", 4096, NULL, tskIDLE_PRIORITY + 1, &s_sound_manager_task) != pdPASS) goto failed;
#endif
    s_rule_runtime_ready = true;
    xSemaphoreGive(s_rule_mutex);
    return true;
failed:
    if (s_rule_gpio_task != NULL) { vTaskDelete(s_rule_gpio_task); s_rule_gpio_task = NULL; }
    if (s_rule_network_task != NULL) { vTaskDelete(s_rule_network_task); s_rule_network_task = NULL; }
#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
    if (s_rule_ble_task != NULL) { vTaskDelete(s_rule_ble_task); s_rule_ble_task = NULL; }
#endif
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
    if (s_audio_mutex != NULL) { vSemaphoreDelete(s_audio_mutex); s_audio_mutex = NULL; }
#endif
    action_dispatcher_stop(&s_rule_runtime.dispatcher);
    hardware_fact_service_deinit(&s_rule_runtime.hardware_facts);
    xSemaphoreGive(s_rule_mutex);
    ESP_LOGE(TAG, "rule runtime initialization failed; dependent services stay disabled");
    return false;
}


#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
static bool app_sound_level_status_json(char *out, size_t out_len, void *ctx)
{
    (void)ctx;
    if (out == NULL || out_len == 0) {
        return false;
    }

    if (s_audio_mutex == NULL || xSemaphoreTake(s_audio_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        const int n = snprintf(out, out_len, "{\"enabled\":true,\"running\":false,\"state\":\"playback_or_transition\"}");
        return n > 0 && (size_t)n < out_len;
    }
    bool ok;
    if (s_sound_level_service != NULL) {
        ok = sound_level_service_build_status_json(s_sound_level_service, out, out_len);
    } else {
        const int n = snprintf(out, out_len, "{\"enabled\":true,\"running\":false,\"state\":\"idle\"}");
        ok = n > 0 && (size_t)n < out_len;
    }
    xSemaphoreGive(s_audio_mutex);
    return ok;
}

static void app_invalidate_sound_facts(void)
{
    if (s_rule_mutex == NULL) return;
    (void)xSemaphoreTake(s_rule_mutex, portMAX_DELAY);
    rule_engine_invalidate_source(&s_rule_runtime.engine, RULE_SOURCE_SOUND_RMS_DBFS);
    rule_engine_invalidate_source(&s_rule_runtime.engine, RULE_SOURCE_SOUND_PEAK_DBFS);
    rule_engine_invalidate_source(&s_rule_runtime.engine, RULE_SOURCE_SOUND_CLIPPED);
    xSemaphoreGive(s_rule_mutex);
}

static void app_sound_manager(void *ctx)
{
    (void)ctx;
    while (true) {
        if (!s_rule_runtime_ready) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        (void)xSemaphoreTake(s_rule_mutex, portMAX_DELAY);
        app_sound_level_demand_update_trigger(&s_sound_level_demand, &s_rule_config);
        const bool needed = app_sound_level_demand_capture_needed(&s_sound_level_demand);
        xSemaphoreGive(s_rule_mutex);
        if (!needed) app_invalidate_sound_facts();
        (void)xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
        app_sound_level_sync(needed);
        xSemaphoreGive(s_audio_mutex);
        if (!needed) app_invalidate_sound_facts();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void app_sound_level_release_audio(void)
{
    if (!s_sound_level_audio_initialized) {
        return;
    }
    (void)board_audio_deinit();
    s_sound_level_audio_initialized = false;
}

static void app_sound_level_release_if_stopped(void)
{
    if (s_sound_level_service == NULL || s_sound_level_service->task != NULL) {
        return;
    }
    sound_level_service_deinit(s_sound_level_service);
    free(s_sound_level_service);
    s_sound_level_service = NULL;
    app_sound_level_release_audio();
    ESP_LOGI(TAG, "sound triggers: capture resources released");
}

static bool app_sound_level_stop(void)
{
    if (s_sound_level_service == NULL) {
        return true;
    }
    if (s_sound_level_ready) {
        sound_level_service_request_stop(s_sound_level_service);
        s_sound_level_ready = false;
        ESP_LOGI(TAG, "sound capture: capture task stop requested (no active demand)");
    }
    for (int i = 0; i < 60 && s_sound_level_service->task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    app_sound_level_release_if_stopped();
    return s_sound_level_service == NULL;
}

/* Start microphone capture only while at least one enabled rule consumes a sound source.
 * This keeps sensor monitoring demand-driven even though the sound feature is
 * compiled into the default build. Web UI telemetry keeps capture active through
 * the same shared service so there is still only one I2S reader. */
static void app_sound_level_sync(bool needed)
{
    if (!needed) {
        app_sound_level_stop();
        return;
    }

    if (s_sound_level_ready) {
        return;
    }
    app_sound_level_release_if_stopped();
    if (s_sound_level_service != NULL && s_sound_level_service->task != NULL) {
        ESP_LOGW(TAG, "sound triggers: capture task is still stopping; start deferred");
        return;
    }

    if (!s_sound_level_audio_initialized) {
        board_audio_config_t audio_config = {
            .profile = BOARD_AUDIO_PROFILE_CAPTURE_ONLY,
            .probe_m5pm1 = true,
            .require_audio_power_enable = true,
        };

        esp_err_t err = board_audio_init(&audio_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "sound triggers: audio init failed: %s", esp_err_to_name(err));
#if CONFIG_APP_SOUND_LEVEL_FAIL_BOOT_ON_AUDIO_ERROR
            ESP_ERROR_CHECK(err);
#else
            return;
#endif
        }
        s_sound_level_audio_initialized = true;
    }

    s_sound_level_service = calloc(1, sizeof(*s_sound_level_service));
    if (s_sound_level_service == NULL) {
        ESP_LOGE(TAG, "sound triggers: service allocation failed");
        app_sound_level_release_audio();
        return;
    }

    sound_level_service_config_t service_config;
    sound_level_service_config_defaults(&service_config);

    if (!sound_level_service_init(s_sound_level_service,
                                  &s_rule_runtime,
                                  s_rule_mutex,
                                  &service_config)) {
        ESP_LOGE(TAG, "sound triggers: service init failed");
        free(s_sound_level_service);
        s_sound_level_service = NULL;
        app_sound_level_release_audio();
        return;
    }

    if (!sound_level_service_start(s_sound_level_service)) {
        ESP_LOGE(TAG, "sound triggers: service task create failed");
        sound_level_service_deinit(s_sound_level_service);
        free(s_sound_level_service);
        s_sound_level_service = NULL;
        app_sound_level_release_audio();
        return;
    }

    s_sound_level_ready = true;
    ESP_LOGI(TAG, "sound capture: capture task started for active demand");
}
#endif

static void app_publish_ble_status(void)
{
#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
    transport_ble_status_snapshot_t status = {
        .app_mode = s_runtime_state.app_mode,
    };
    transport_ble_gatt_update_status(&status);
#endif
}

static void key1_pressed_cb(void *ctx)
{
    (void)ctx;
    app_emit_button_rule_fact(BUTTON_STATE_EVENT_KEY1_SHORT);
}

static void key2_pressed_cb(void *ctx)
{
    (void)ctx;
    app_emit_button_rule_fact(BUTTON_STATE_EVENT_KEY2_SHORT);
}

static bool app_ui_load_automation(automation_config_t *out, void *ctx)
{
    (void)ctx;
    if (!s_rule_runtime_ready || s_rule_mutex == NULL) return false;
    if (xSemaphoreTake(s_rule_mutex, portMAX_DELAY) != pdTRUE) return false;
    *out = s_rule_runtime.engine.config;
    xSemaphoreGive(s_rule_mutex);
    return true;
}

static bool app_commit_automation(const automation_config_t *config, void *ctx)
{
    return rule_config_store_save((rule_config_store_t *)ctx, config);
}

static bool app_ui_save_automation(automation_config_t *out, uint8_t index,
                                    const ui_automation_state_t *slot, ui_automation_edit_t edit, void *ctx)
{
    (void)ctx;
    if (!s_rule_runtime_ready || s_rule_mutex == NULL) return false;
    if (xSemaphoreTake(s_rule_mutex, portMAX_DELAY) != pdTRUE) return false;
    *out = s_rule_runtime.engine.config;
    const bool ok = ui_automation_apply_edit(out, index, slot, edit) &&
        rule_runtime_replace_config_with_commit(&s_rule_runtime, out, app_commit_automation, &s_rule_store);
    if (ok) s_rule_config = *out;
    xSemaphoreGive(s_rule_mutex);
    return ok;
}

static void app_rule_web_config_changed_cb(const automation_config_t *config, void *ctx)
{
    (void)ctx;
    if (config == NULL) {
        return;
    }
    s_rule_config = *config;

}

static bool app_rule_web_runtime_lock(void *ctx)
{
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)ctx;
    return mutex != NULL && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void app_rule_web_runtime_unlock(void *ctx)
{
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)ctx;
    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}

/* The Web UI HTTP server owns a task, URI handler table, stack, and heap
 * buffers inside esp_http_server. Keep those resources allocated only while
 * the on-device Web UI flow has explicitly enabled the service. */
static void app_web_ui_service_changed(bool enabled, void *ctx)
{
    (void)ctx;
    if (enabled && !s_rule_runtime_ready) {
        status_ui_set_state(STATUS_UI_STATE_ERROR);
        ESP_LOGE(TAG, "web UI unavailable: rule runtime failed to initialize");
        return;
    }
    if (!s_rule_runtime_ready) {
        ESP_LOGW(TAG, "web UI service change ignored before rule runtime is ready: %s", enabled ? "enable" : "disable");
        return;
    }

    if (s_rule_mutex != NULL) {
        (void)xSemaphoreTake(s_rule_mutex, portMAX_DELAY);
    }
    if (enabled) {
        if (!s_rule_web.started && rule_web_start_locked(&s_rule_web, &s_rule_runtime, &s_rule_store,
                                                       app_rule_web_runtime_lock, app_rule_web_runtime_unlock, s_rule_mutex)) {
            rule_web_set_config_changed_callback(&s_rule_web, app_rule_web_config_changed_cb, NULL);
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
            app_sound_level_demand_set_telemetry(&s_sound_level_demand, true);
#endif
            app_wifi_status_t wifi_status;
            if (app_wifi_get_status(&wifi_status)) {
                ESP_LOGI(TAG, "web UI server started: %s", wifi_status.web_url);
            } else {
                ESP_LOGI(TAG, "web UI server started");
            }
        } else if (!s_rule_web.started) {
            ESP_LOGE(TAG, "web UI server failed to start");
        }
    } else {
        if (s_rule_web.started) {
            rule_web_stop(&s_rule_web);
#if CONFIG_APP_SOUND_LEVEL_TRIGGERS
            app_sound_level_demand_set_telemetry(&s_sound_level_demand, false);
#endif
            ESP_LOGI(TAG, "web UI server stopped and resources released");
        }
    }
    if (s_rule_mutex != NULL) {
        xSemaphoreGive(s_rule_mutex);
    }

}

static void app_idle_forever(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main: starting, reset_reason=%d, free_heap=%lu",
             (int)esp_reset_reason(), (unsigned long)esp_get_free_heap_size());
    app_runtime_state_init(&s_runtime_state);
    ESP_LOGI(TAG, "app_main: runtime defaults mode=%s",
             app_mode_name(s_runtime_state.app_mode));

    const status_ui_button_handlers_t status_handlers = {
        .key1_pressed = key1_pressed_cb,
        .key2_pressed = key2_pressed_cb,
        .service_enabled_changed = app_web_ui_service_changed,
    };

    ESP_LOGI(TAG, "app_main: NVS init start");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "app_main: NVS reinitializing after %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "app_main: NVS ready");
    (void)app_time_init();
    ESP_ERROR_CHECK(app_network_stack_init());

    ESP_LOGI(TAG, "app_main: status UI init start");
    ui_model_set_automation_backend(app_ui_load_automation, app_ui_save_automation, NULL);
    ESP_ERROR_CHECK(status_ui_init(&status_handlers));
    status_ui_set_state(STATUS_UI_STATE_BOOTING);

    if (app_wifi_start()) {
        ESP_LOGI(TAG, "app_main: Wi-Fi/web UI network ready or setup AP started");
    } else {
        ESP_LOGW(TAG, "app_main: Wi-Fi/web UI network startup unavailable");
    }
    if (!app_rule_runtime_init()) {
        status_ui_set_state(STATUS_UI_STATE_ERROR);
        app_idle_forever();
    }

#if CONFIG_APP_USB_UAC_DEVICE
    ESP_LOGI(TAG, "app_main: USB Audio Class init start");
    ret = uac_service_start_from_kconfig();
    if (ret != ESP_OK) {
        status_ui_set_state(STATUS_UI_STATE_ERROR);
        ESP_LOGE(TAG, "USB Audio Class service failed to start: %s; staying alive", esp_err_to_name(ret));
        app_idle_forever();
    }
#endif

#if CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
    ESP_LOGI(TAG, "app_main: BLE GATT rule-event init start");
    ret = transport_ble_gatt_start();
    if (ret != ESP_OK) {
        status_ui_set_state(STATUS_UI_STATE_ERROR);
        ESP_LOGE(TAG, "BLE GATT rule-event transport failed to start: %s; staying alive", esp_err_to_name(ret));
        app_idle_forever();
    }
    app_publish_ble_status();
    status_ui_set_state(STATUS_UI_STATE_READY);
    ESP_LOGI(TAG, "Bluetooth LE rule-event transport and configuration UI are running");
#else
    status_ui_set_state(STATUS_UI_STATE_NO_TRANSPORT);
    ESP_LOGW(TAG, "No StickS3 BLE rule-event transport is selected; local UI remains available");
#endif

    app_idle_forever();
}
