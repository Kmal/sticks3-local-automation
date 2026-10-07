#include "rule_web.h"
#include "app_wifi.h"
#include "trigger_gpio.h"
#include "trigger_hat.h"
#include "ui_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(value) do { if (!(value)) { fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #value); exit(1); } } while (0)
#define ASSERT_FALSE(value) ASSERT_TRUE(!(value))
#define ASSERT_EQ_U32(expected, actual) ASSERT_TRUE((uint32_t)(expected) == (uint32_t)(actual))

static trigger_fact_t s_last_fact;
static size_t s_fact_count;
static size_t s_config_changed_count;
static automation_config_t s_last_config_changed;
static size_t s_runtime_lock_count;
static size_t s_runtime_unlock_count;

static bool capture_runtime_lock(void *ctx)
{
    (void)ctx;
    s_runtime_lock_count++;
    return true;
}

static void capture_runtime_unlock(void *ctx)
{
    (void)ctx;
    s_runtime_unlock_count++;
}

static bool capture_fact(const trigger_fact_t *fact, void *ctx)
{
    (void)ctx;
    ASSERT_TRUE(fact != NULL);
    s_last_fact = *fact;
    s_fact_count++;
    return true;
}

static void capture_config_changed(const automation_config_t *config, void *ctx)
{
    (void)ctx;
    ASSERT_TRUE(config != NULL);
    s_last_config_changed = *config;
    s_config_changed_count++;
}

/* Existing round-trip assertions explicitly reload after the new small ACK. */
static bool request_with_snapshot(rule_web_t *web, rule_web_method_t method, const char *path,
                                  const char *body, char *out, size_t out_len)
{
    bool ok = rule_web_handle_request(web, method, path, body, out, out_len);
    if (ok && method == RULE_WEB_METHOD_POST && strcmp(path, "/api/config") == 0 && strcmp(out, "{\"ok\":true}") == 0)
        return rule_web_handle_request(web, RULE_WEB_METHOD_GET, path, NULL, out, out_len);
    return ok;
}

static void test_gpio_digital_debounce_emits_safe_pin(void)
{
    trigger_gpio_t gpio;
    rule_gpio_config_t cfg = {.pin = 4, .profile = RULE_GPIO_PROFILE_DIGITAL_HIGH_LOW, .debounce_ms = 10};
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, capture_fact, NULL);
    s_fact_count = 0;

    ASSERT_TRUE(trigger_gpio_init(&gpio, RULE_SOURCE_GPIO_DIGITAL, &cfg));
    ASSERT_TRUE(trigger_gpio_probe(&gpio));
    trigger_gpio_set_host_level(&gpio, false);
    ASSERT_EQ_U32(0, trigger_gpio_poll(&gpio, &adapter, 100));
    trigger_gpio_set_host_level(&gpio, true);
    ASSERT_EQ_U32(0, trigger_gpio_poll(&gpio, &adapter, 105));
    ASSERT_EQ_U32(1, trigger_gpio_poll(&gpio, &adapter, 116));
    ASSERT_EQ_U32(1, s_fact_count);
    ASSERT_TRUE(s_last_fact.source == RULE_SOURCE_GPIO_DIGITAL);
    ASSERT_TRUE(s_last_fact.value.kind == RULE_VALUE_BOOL);
    ASSERT_TRUE(s_last_fact.value.as.bool_value);
}

static void test_disabled_hat_does_not_probe(void)
{
    trigger_hat_t hat;
    ASSERT_FALSE(trigger_hat_probe(&hat, RULE_SOURCE_HAT_PIR_MOTION));
}

static void test_rule_web_status(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    rule_runtime_t runtime;
    rule_config_store_t store;
    rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    rule_web_set_runtime_lock(&web, capture_runtime_lock, capture_runtime_unlock, NULL);
    s_runtime_lock_count = 0;
    s_runtime_unlock_count = 0;
    s_config_changed_count = 0;
    rule_web_set_config_changed_callback(&web, capture_config_changed, NULL);
    char json[16384];
    ASSERT_TRUE(rule_web_get_status_json(&web, json, sizeof(json)));
    ASSERT_EQ_U32(1, s_runtime_lock_count);
    ASSERT_EQ_U32(1, s_runtime_unlock_count);
    ASSERT_TRUE(strstr(json, "http_network_ready") != NULL);
    ASSERT_TRUE(strstr(json, "\"wifi\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"sound\"") != NULL);
    ASSERT_TRUE(strstr(json, "audio_capture_disabled") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/", NULL, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "Trigger source") != NULL);
    ASSERT_TRUE(strstr(json, "Import / export JSON") != NULL);
    ASSERT_TRUE(strstr(json, "Test GPIO safety") != NULL);
    ASSERT_TRUE(strstr(json, "Probe HAT") != NULL);
    ASSERT_TRUE(strstr(json, "Wi-Fi Mode") != NULL);
    ASSERT_TRUE(strstr(json, "AP Mode") != NULL);
    ASSERT_TRUE(strstr(json, "AP Name") != NULL);
    ASSERT_TRUE(strstr(json, "Scan Nearby Wi-Fi") != NULL);
    ASSERT_TRUE(strstr(json, "Use Wi-Fi Mode") != NULL);
    ASSERT_TRUE(strstr(json, "Use AP Mode") != NULL);
    ASSERT_TRUE(strstr(json, "Saved Wi-Fi") != NULL);
    ASSERT_TRUE(strstr(json, "Time settings") != NULL);
    ASSERT_TRUE(strstr(json, "Save Timezone") != NULL);
    ASSERT_TRUE(strstr(json, "<select id=\"timezone\">") != NULL);
    ASSERT_TRUE(strstr(json, "Pacific Time (UTC-8)") != NULL);
    ASSERT_TRUE(strstr(json, "India (UTC+5:30)") != NULL);
    ASSERT_TRUE(strstr(json, "Forget Saved Credentials") != NULL);
    ASSERT_TRUE(strstr(json, "id=\"wifi_ssid\" maxlength=\"32\" autocomplete=\"off\" autocapitalize=\"none\"") != NULL);
    ASSERT_TRUE(strstr(json, "id=\"wifi_password\" type=\"password\" maxlength=\"63\" autocomplete=\"off\" autocapitalize=\"none\"") != NULL);
    ASSERT_TRUE(strstr(json, "id=\"ap_ssid\" maxlength=\"32\" autocomplete=\"off\" autocapitalize=\"none\"") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/time", NULL, json, sizeof(json)));
    ASSERT_EQ_U32(1, s_runtime_lock_count);
    ASSERT_TRUE(strstr(json, "\"timezone\":\"UTC\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"time_24h\"") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"UTC0\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"timezone\":\"UTC0\"") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"UTC-08\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"timezone\":\"UTC-8\"") != NULL);
    ASSERT_TRUE(getenv("TZ") != NULL && strcmp(getenv("TZ"), "UTC+8") == 0);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"UTC+05:30\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"timezone\":\"UTC+5:30\"") != NULL);
    ASSERT_TRUE(getenv("TZ") != NULL && strcmp(getenv("TZ"), "UTC-5:30") == 0);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"UTC+15\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_timezone") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"bad tz\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_timezone") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/favicon.ico", NULL, json, sizeof(json)));
    ASSERT_TRUE(strcmp(json, "") == 0);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/wifi/status", NULL, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"enabled\":false") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/scan", NULL, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "wifi_unavailable") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/connect", "{}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "missing_ssid") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"MyDeviceAP\",\"password\":\"password123\",\"channel\":6}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\"") != NULL);
    ASSERT_TRUE(strstr(json, "MyDeviceAP") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"OpenAP\",\"channel\":6}",
                                        json, sizeof(json)));
    app_wifi_config_t wifi_config;
    ASSERT_TRUE(app_wifi_get_config(&wifi_config));
    ASSERT_TRUE(strstr(json, "invalid_ap_password") != NULL);
    ASSERT_TRUE(strcmp(wifi_config.ap_ssid, "MyDeviceAP") == 0);
    ASSERT_TRUE(strcmp(wifi_config.ap_password, "password123") == 0);
    ASSERT_FALSE(app_wifi_start_ap_configured("OpenAP", "", 6, true));
    ASSERT_FALSE(app_wifi_start_ap_configured("OpenAP", "short", 6, true));
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"12345678901234567890123456789012\",\"password\":\"123456789012345678901234567890123456789012345678901234567890123\",\"channel\":6}",
                                        json, sizeof(json)));
    ASSERT_TRUE(app_wifi_get_config(&wifi_config));
    ASSERT_TRUE(strcmp(wifi_config.ap_ssid, "12345678901234567890123456789012") == 0);
    ASSERT_TRUE(strlen(wifi_config.ap_password) == 63u);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"123456789012345678901234567890123\",\"channel\":6}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_ap_ssid") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"\",\"password\":\"password123\",\"channel\":6}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_ap_ssid") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"MyDeviceAP\",\"channel\":99}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_ap_channel") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/wifi/mode",
                                        "{\"mode\":\"ap\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\"") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/capabilities", NULL, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "pin_conflicts") != NULL);
    ASSERT_TRUE(strstr(json, "source_capabilities") != NULL);
    ASSERT_TRUE(strstr(json, "sound.rms_dbfs") != NULL);
    ASSERT_TRUE(strstr(json, "schema_supported") != NULL);
    ASSERT_TRUE(strstr(json, "runtime_available") != NULL);
    ASSERT_TRUE(strstr(json, "\"runtime_available\":true") != NULL);
    ASSERT_TRUE(strstr(json, "hat.thermal.avg_c") != NULL);
    ASSERT_TRUE(strstr(json, "pulse_count") != NULL);
    ASSERT_TRUE(strstr(json, "speaker_tone") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/config", NULL, json, sizeof(json)));
    ASSERT_TRUE(s_runtime_lock_count > 1);
    ASSERT_EQ_U32(s_runtime_lock_count, s_runtime_unlock_count);
    ASSERT_TRUE(strstr(json, "threshold_kind") != NULL);
    char exported[16384];
    snprintf(exported, sizeof(exported), "%s", json);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", exported, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "config rejected") == NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", "defaults", json, sizeof(json)));
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/rules/test", NULL, json, sizeof(json)));
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", "{\"preset\":\"sound_local_ui\"}", json, sizeof(json)));
    ASSERT_TRUE(s_config_changed_count > 0);
    ASSERT_TRUE(s_last_config_changed.rules[0].when.source == RULE_SOURCE_SOUND_CLIPPED);
    ASSERT_TRUE(strstr(json, "Sound clipped alert") != NULL);
    ASSERT_TRUE(strstr(json, "sound.clipped") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", "{\"preset\":\"loud_sound_local_ui\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "Loud sound alert") != NULL);
    ASSERT_TRUE(strstr(json, "sound.rms_dbfs") != NULL);
    ASSERT_TRUE(strstr(json, "\"comparator\":\"gte\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"threshold_i32\":-5120") != NULL);
    ASSERT_TRUE(strstr(json, "\"sustain_ms\":250") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"sound.peak_dbfs\",\"action\":\"local_ui\"}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "sound.peak_dbfs") != NULL);
    ASSERT_TRUE(strstr(json, "\"comparator\":\"gte\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"threshold_i32\":-5120") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/rules/test", NULL, json, sizeof(json)));
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"http_post\",\"http_url\":\"https://example.invalid/hook\",\"http_bearer_token\":\"secret-token\"}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "masked") != NULL);
    ASSERT_TRUE(strstr(json, "secret-token") == NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"speaker_tone\",\"speaker_frequency_hz\":7000,\"speaker_duration_ms\":100,\"speaker_volume_percent\":50,\"action_timeout_ms\":100}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "speaker_tone") != NULL);
    ASSERT_TRUE(strstr(json, "\"speaker_frequency_hz\":7000") != NULL);
    ASSERT_TRUE(strstr(json, "\"speaker_volume_percent\":50") != NULL);
    ASSERT_TRUE(s_last_config_changed.rules[0].actions[0].type == RULE_ACTION_SPEAKER_TONE);
    ASSERT_TRUE(s_last_config_changed.rules[0].actions[0].speaker_frequency_hz == 7000u);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"speaker_tone\",\"speaker_frequency_hz\":9000,\"speaker_duration_ms\":100,\"speaker_volume_percent\":50,\"action_timeout_ms\":100}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "config rejected") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"local_ui\",\"name\":\"Quote \\\"slash\\\\ line\\n\"}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "Quote \\\"slash\\\\ line\\n") != NULL);
    ASSERT_TRUE(strstr(json, "Quote \"slash") == NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"http_post\",\"http_url\":\"ftp://bad.invalid/hook\"}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "config rejected") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"gpio.digital\",\"action\":\"local_ui\",\"gpio_pin\":4,\"gpio_profile\":\"digital_high_low\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "gpio.digital") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"gpio_pin\":4,\"gpio_profile\":\"digital_high_low\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":true") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"gpio_pin\":4x,\"gpio_profile\":\"digital_high_low\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":false") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"gpio_pin\":4,\"gpio_profile\":\"digital_high_low\"x,\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":false") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"gpio_pin\":39,\"gpio_profile\":\"digital_high_low\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":false") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"gpio.edge\",\"action\":\"local_ui\",\"gpio_pin\":4,\"gpio_profile\":\"rising_edge\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "gpio.edge") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"source\":\"gpio.edge\",\"gpio_pin\":4,\"gpio_profile\":\"rising_edge\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":true") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"source\":\"gpio.pulse_count\",\"gpio_pin\":4,\"gpio_profile\":\"pulse_count\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":false") != NULL);
    ASSERT_TRUE(strstr(json, "\"supported\":false") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/hat/probe",
                                        "{\"source\":\"hat.thermal.avg_c\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "missing_hat_driver") != NULL);
    rule_web_stop(&web);
    rule_config_store_close(&store);
}


static void test_complete_config_roundtrip_and_strict_parser(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    config.rule_count = RULE_MAX_RULES;
    for (size_t i = 0; i < config.rule_count; ++i) {
        config.rules[i] = config.rules[0];
        config.rules[i].id = (uint32_t)i + 1;
        config.rules[i].action_count = 3;
        for (size_t j = 0; j < 3; ++j) {
            rule_action_t *action = &config.rules[i].actions[j];
            *action = config.rules[0].actions[0];
            action->type = RULE_ACTION_HTTP_POST;
            snprintf(action->http_url, sizeof(action->http_url), "https://example.invalid/hook/%zu/%zu", i, j);
            snprintf(action->http_bearer_token, sizeof(action->http_bearer_token), "secret-%zu-%zu", i, j);
            action->timeout_ms = 1000;
        }
    }
    config.rules[7].actions[2].type = RULE_ACTION_IR_SEND;
    config.rules[7].actions[2].ir_protocol = RULE_IR_PROTOCOL_NEC;
    config.rules[7].actions[2].ir_address = 0xabcd;
    config.rules[7].actions[2].ir_command = 0xef01;
    config.rules[7].actions[2].ir_carrier_hz = 38000;
    config.rules[7].actions[2].ir_repeat_count = 5;
    config.rules[7].actions[2].timeout_ms = 250;
    config.rules[6].actions[2].type = RULE_ACTION_SPEAKER_TONE;
    config.rules[6].actions[2].speaker_frequency_hz = 8000;
    config.rules[6].actions[2].speaker_duration_ms = 5000;
    config.rules[6].actions[2].speaker_volume_percent = 74;
    config.rules[6].actions[2].timeout_ms = 1000;
    config.rules[0].enabled = true;
    rule_runtime_t runtime;
    rule_config_store_t store;
    rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    char exported[32768], result[32768];
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/config", NULL, exported, sizeof(exported)));
    ASSERT_TRUE(strlen(exported) > 511);
    ASSERT_TRUE(strstr(exported, "secret-") == NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", exported, result, sizeof(result)));
    ASSERT_TRUE(strstr(result, "\"error\"") == NULL);
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    automation_config_t persisted;
    ASSERT_TRUE(rule_config_store_load(&store, &persisted));
    ASSERT_TRUE(memcmp(&config, &persisted, sizeof(config)) == 0);
    ui_runtime_t ui;
    ui_runtime_init(&ui);
    ASSERT_TRUE(ui_runtime_load_automation(&ui, 0));
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ENABLED));
    ASSERT_TRUE(rule_config_store_load(&store, &persisted));
    ASSERT_TRUE(memcmp(&config, &persisted, sizeof(config)) == 0);
    const char *bad[] = {
        "{\"source\":\"button.key1.short\",\"action\":\"speaker_tone\",\"speaker_volume_percent\":306}",
        "{\"source\":\"button.key1.short\",\"source\":\"sound.clipped\",\"action\":\"local_ui\"}",
        "{\"source\":\"button.key1.short\",\"action\":\"local_ui\",\"cooldown_ms\":100.5}",
        "{\"source\":\"button.key1.short\",\"action\":\"local_ui\",\"name\":\"bad\\u0000name\"}",
        "{\"rules\":[{\"id\":1},{\"id\":1}]}",
        "{\"source\":\"button.key1.short\",\"action\":\"local_ui\"} trailing"
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", bad[i], result, sizeof(result)));
        ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
        ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    }
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        "{\"source\":\"button.key1.short\",\"action\":\"http_post\",\"name\":\"sound_local_ui\"}", result, sizeof(result)));
    ASSERT_TRUE(runtime.engine.config.rules[0].when.source == RULE_SOURCE_KEY1_SHORT);
    ASSERT_EQ_U32(8, runtime.engine.config.rule_count);
    ASSERT_EQ_U32(3, runtime.engine.config.rules[0].action_count);
    rule_config_store_close(&store);
    automation_config_t before = runtime.engine.config;
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        "{\"source\":\"button.key1.short\",\"action\":\"local_ui\",\"name\":\"unsaved\"}", result, sizeof(result)));
    ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    ASSERT_TRUE(memcmp(&before, &runtime.engine.config, sizeof(before)) == 0);

    ASSERT_TRUE(strcmp(runtime.engine.config.rules[0].actions[0].http_bearer_token, "secret-0-0") == 0);
    ASSERT_TRUE(rule_config_store_open(&store));
    automation_config_set_defaults(&persisted);
    persisted.rule_count = 0;
    ASSERT_TRUE(rule_config_store_save(&store, &persisted));
    ui_runtime_init(&ui);
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 1, UI_AUTOMATION_EDIT_ENABLED));
    ASSERT_TRUE(rule_config_store_load(&store, &persisted));
    ASSERT_EQ_U32(2, persisted.rule_count);
    ASSERT_TRUE(automation_config_validate(&persisted, NULL, 0));
    rule_web_stop(&web);
    rule_config_store_close(&store);
}

static void test_config_edits_and_roundtrip_preserve_unedited_state(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    config.rules[0].enabled = true;
    config.rules[0].when.source = RULE_SOURCE_KEY1_SHORT;
    config.rules[0].when.threshold = rule_value_bool(true);
    config.rules[0].when.comparator = RULE_COMPARATOR_EQ;
    config.rules[0].when.sustain_ms = 0;
    config.rules[0].cooldown_ms = 4321;
    config.rules[0].action_count = 3;
    config.rules[0].actions[0].type = RULE_ACTION_HTTP_POST;
    config.rules[0].actions[0].timeout_ms = 3456;
    snprintf(config.rules[0].actions[0].http_url, RULE_HTTP_URL_MAX, "https://example.invalid/hook");
    snprintf(config.rules[0].actions[0].http_bearer_token, RULE_HTTP_AUTH_MAX, "roundtrip-secret");
    config.rules[0].actions[1].type = RULE_ACTION_LOCAL_UI;
    config.rules[0].actions[2].type = RULE_ACTION_LOCAL_UI;
    config.rules[1].id = 99;
    config.rules[0].name[0] = '\0';
    rule_runtime_t runtime;
    rule_config_store_t store;
    rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    char exported[32768], response[32768];
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/config", NULL, exported, sizeof(exported)));
    ASSERT_TRUE(strstr(exported, "roundtrip-secret") == NULL);
    ASSERT_TRUE(strstr(exported, "\"rules\":[") != NULL);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", exported, response, sizeof(response)));
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        "{\"source\":\"button.key1.short\",\"action\":\"http_post\",\"name\":\"Edited name\"}", response, sizeof(response)));
    snprintf(config.rules[0].name, RULE_NAME_MAX, "Edited name");
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    automation_config_t saved;
    ASSERT_TRUE(rule_config_store_load(&store, &saved));
    ASSERT_TRUE(memcmp(&config, &saved, sizeof(config)) == 0);
    rule_config_store_close(&store);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        "{\"source\":\"button.key1.short\",\"action\":\"http_post\",\"name\":\"Failed save\"}", response, sizeof(response)));
    ASSERT_TRUE(strstr(response, "config rejected") != NULL);
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_config_store_load(&store, &saved));
    ASSERT_TRUE(memcmp(&config, &saved, sizeof(config)) == 0);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        "{\"source\":\"button.key1.short\",\"action\":\"http_post\",\"http_bearer_token\":\"\"}", response, sizeof(response)));
    config.rules[0].actions[0].http_bearer_token[0] = '\0';
    ASSERT_TRUE(runtime.engine.config.rules[0].actions[0].http_bearer_token[0] == '\0');
    ASSERT_TRUE(memcmp(&config.rules[1], &runtime.engine.config.rules[1], sizeof(config.rules[1])) == 0);
    rule_config_store_close(&store);
}

static void test_speaker_form_save_preserves_parameters_and_rejects_wrapped_volume(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    config.rules[0].when.source = RULE_SOURCE_KEY1_SHORT;
    config.rules[0].when.threshold = rule_value_bool(true);
    config.rules[0].when.comparator = RULE_COMPARATOR_EQ;
    config.rules[0].when.sustain_ms = 0;
    config.rules[0].actions[0].type = RULE_ACTION_SPEAKER_TONE;
    config.rules[0].actions[0].speaker_frequency_hz = 1234;
    config.rules[0].actions[0].speaker_duration_ms = 200;
    config.rules[0].actions[0].speaker_volume_percent = 23;
    config.rules[0].actions[0].timeout_ms = 300;
    rule_runtime_t runtime;
    rule_config_store_t store;
    rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    char response[16384];
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        "{\"source\":\"button.key1.short\",\"action\":\"speaker_tone\"}", response, sizeof(response)));
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        "{\"source\":\"button.key1.short\",\"action\":\"speaker_tone\",\"speaker_volume_percent\":306}", response, sizeof(response)));
    ASSERT_TRUE(strstr(response, "error") != NULL);
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    rule_config_store_close(&store);
}

static void test_export_fits_import_limit_and_oversize_requests_do_not_mutate(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    config.rules[0].when.source = RULE_SOURCE_KEY1_SHORT;
    config.rules[0].when.threshold = rule_value_bool(true);
    config.rules[0].when.comparator = RULE_COMPARATOR_EQ;
    config.rules[0].when.sustain_ms = 0;
    config.rules[0].actions[0].type = RULE_ACTION_HTTP_POST;
    config.rules[0].actions[0].timeout_ms = 1000;
    memset(config.rules[0].name, 1, RULE_NAME_MAX - 1);
    config.rules[0].name[RULE_NAME_MAX - 1] = '\0';
    snprintf(config.rules[0].actions[0].http_url, RULE_HTTP_URL_MAX, "http://example.invalid/");
    size_t used = strlen(config.rules[0].actions[0].http_url);
    memset(config.rules[0].actions[0].http_url + used, '\"', RULE_HTTP_URL_MAX - 1 - used);
    config.rules[0].actions[0].http_url[RULE_HTTP_URL_MAX - 1] = '\0';
    rule_runtime_t runtime;
    rule_config_store_t store;
    rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    char exported[32768], response[32768];
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/config", NULL, exported, sizeof(exported)));
    ASSERT_TRUE(strlen(exported) > 511);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", exported, response, sizeof(response)));
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    char oversized[32769];
    memset(oversized, ' ', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", oversized, response, sizeof(response)));
    ASSERT_TRUE(strstr(response, "body too large") != NULL);
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    rule_config_store_close(&store);
}

static automation_config_t preservation_config(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    automation_rule_t *rule = &config.rules[0];
    rule->enabled = false;
    rule->when.source = RULE_SOURCE_SOUND_RMS_DBFS;
    rule->when.threshold = rule_value_i32(-2222);
    rule->when.comparator = RULE_COMPARATOR_GTE;
    rule->when.sustain_ms = 777;
    rule->cooldown_ms = 4321;
    rule->action_count = 2;
    rule->actions[0].type = RULE_ACTION_HTTP_POST;
    rule->actions[0].timeout_ms = 3456;
    rule->name[0] = '\0';
    snprintf(rule->actions[0].http_url, RULE_HTTP_URL_MAX, "https://example.invalid/button_local_ui");
    snprintf(rule->actions[0].http_bearer_token, RULE_HTTP_AUTH_MAX, "sound_local_ui");
    rule->actions[1].type = RULE_ACTION_LOCAL_UI;
    config.rules[1].id = 99;
    snprintf(config.rules[1].name, RULE_NAME_MAX, "Keep second rule");
    return config;
}

static void test_strict_config_parsing_preserves_runtime_and_store(void)
{
    automation_config_t config = preservation_config();
    rule_runtime_t runtime;
    rule_config_store_t store;
    rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_config_store_save(&store, &config));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    rule_web_set_config_changed_callback(&web, capture_config_changed, NULL);
    char response[16384];
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"enabled\":false,\"name\":\"button_local_ui\"}", response, sizeof(response)));
    snprintf(config.rules[0].name, RULE_NAME_MAX, "button_local_ui");
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    char exported[32768];
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/config", NULL, exported, sizeof(exported)));
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", exported, response, sizeof(response)));
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    const char *invalid[] = {
        "{\"rules\":[{\"id\":1,\"cooldown_ms\":01000}]}",
        "{\"rules\":[{\"id\":1,\"cooldown_ms\":1000.}]}",
        "{\"rules\":[{\"id\":1,\"cooldown_ms\":1e}]}",
        "{\"rules\":[{\"id\":1,\"name\":\"raw\nnewline\"}]}",
        "{\"rules\":[{\"id\":1,\"name\":\"\xc0\xaf\"}]}",
        "{\"rules\":[{\"id\":1,\"name\":\"\xed\xa0\x80\"}]}",
        "{\"rules\":[{\"id\":1,\"name\":\"\xf4\x90\x80\x80\"}]}",
        "{\"rules\":[]}\v",
        "{\"rules\":[{\"id\":1,\"threshold_bool\":\"false\"}]}",
        "{\"rules\":[{\"id\":1,\"gpio_pin\":true}]}",
        "{\"rules\":[{\"id\":1,\"gpio_profile\":\"bogus\"}]}",
        "{\"rules\":[{\"id\":1,\"gpio_debounce_ms\":-1}]}",
        "{\"preset\":\"defaults\",\"rules\":[]}",
        "{\"preset\":\"unknown\",\"rules\":[]}",
        "{\"preset\":false,\"rules\":[]}",
        NULL, "", "{}", "[]", "null",
        "junk \"source\":\"sound.rms_dbfs\",\"action\":\"local_ui\" trailing junk",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"local_ui\"} trailing",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"local_ui\",}",
        "{\"source\":\"sound.rms_dbfs\" \"action\":\"local_ui\"}",
        "{\"source\":\"sound.rms_dbfs\",\"source\":\"wifi.connected\",\"action\":\"local_ui\"}",
        "{\"source\":\"sound.rms_dbfs\",\"so\\u0075rce\":\"wifi.connected\",\"action\":\"local_ui\"}",
        "{\"outer\":{\"source\":\"sound.rms_dbfs\",\"action\":\"local_ui\"}}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"enabled\":\"true\"}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"sustain_ms\":-1}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"cooldown_ms\":01}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"cooldown_ms\":1.5}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"cooldown_ms\":2147483648}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"cooldown_ms\":\"1000\"}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"http_bearer_token\":false}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"name\":\"too long a name that exceeds the configured rule name buffer by far\"}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"name\":\"\\u0000\"}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"name\":\"\\uD800\"}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"comparator\":\"bogus\"}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"threshold_kind\":\"bogus\"}",
        "{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"schema_version\":99}",
        "{\"preset\":\"unknown\"}",
        "{\"preset\":\"defaults\",\"name\":\"keep\"}",
        " {\"preset\":\"defaults\"} trailing",
        "{\"note\":\"\\\"source\\\":\\\"sound.rms_dbfs\\\"\",\"action\":\"local_ui\"}",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        const size_t callbacks = s_config_changed_count;
        ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", invalid[i], response, sizeof(response)));
        if (strstr(response, "error") == NULL) fprintf(stderr, "accepted invalid config %zu: %s\n", i, invalid[i] ? invalid[i] : "(null)");
        ASSERT_TRUE(strstr(response, "error") != NULL);
        ASSERT_EQ_U32(callbacks, s_config_changed_count);
        ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
        automation_config_t saved;
        ASSERT_TRUE(rule_config_store_load(&store, &saved));
        ASSERT_TRUE(memcmp(&config, &saved, sizeof(config)) == 0);
    }
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        " \n{\"source\":\"sound.rms_dbfs\",\"action\":\"http_post\",\"name\":\"\\u00e9\\uD83D\\uDE00\"} \n", response, sizeof(response)));
    snprintf(config.rules[0].name, RULE_NAME_MAX, "\xc3\xa9\xf0\x9f\x98\x80");
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config",
        " {\"preset\":\"defaults\"} \n", response, sizeof(response)));
    automation_config_set_defaults(&config);
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    rule_web_stop(&web);
    rule_config_store_close(&store);
}

static void test_browser_lcd_roundtrip_preserves_unedited_settings(void)
{
    automation_config_t config = preservation_config();
    rule_runtime_t runtime;
    rule_config_store_t store;
    rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_config_store_save(&store, &config));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    char exported[32768], response[16384];
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_GET, "/api/config", NULL, exported, sizeof(exported)));
    ui_runtime_t ui;
    ui_runtime_init(&ui);
    ASSERT_TRUE(ui_runtime_load_automation(&ui, 0));
    ui.automations[0].enabled = true;
    /* An enable edit must not replay stale trigger/action selections. */
    ui.automations[0].trigger_source = RULE_SOURCE_KEY2_SHORT;
    ui.automations[0].enabled = true;
    ui.automations[0].trigger_source = RULE_SOURCE_WIFI_CONNECTED;
    ui.automations[0].action_kind = RULE_ACTION_LOCAL_UI;
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ENABLED));
    config.rules[0].enabled = true;
    automation_config_t saved;
    ASSERT_TRUE(rule_config_store_load(&store, &saved));
    ASSERT_TRUE(memcmp(&config, &saved, sizeof(config)) == 0);
    /* Production's config-changed callback reloads the committed LCD edit. */
    ASSERT_TRUE(rule_runtime_replace_config(&runtime, &saved));
    ASSERT_TRUE(request_with_snapshot(&web, RULE_WEB_METHOD_POST, "/api/config", exported, response, sizeof(response)));
    config.rules[0].enabled = false;
    ASSERT_TRUE(memcmp(&config, &runtime.engine.config, sizeof(config)) == 0);
    ASSERT_TRUE(ui_runtime_load_automation(&ui, 0));
    /* Reselecting an existing type must retain its custom parameters. */
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_TRIGGER));
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ACTION));
    ASSERT_TRUE(rule_config_store_load(&store, &saved));
    ASSERT_TRUE(memcmp(&config, &saved, sizeof(config)) == 0);
    ui.automations[0].enabled = true; /* stale unrelated field */
    ui.automations[0].action_kind = RULE_ACTION_LOCAL_UI;
    ui.automations[0].trigger_source = RULE_SOURCE_KEY2_SHORT;
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_TRIGGER));
    ASSERT_TRUE(rule_config_store_load(&store, &saved));
    ASSERT_TRUE(saved.rules[0].enabled == config.rules[0].enabled);
    ASSERT_TRUE(saved.rules[0].when.source == RULE_SOURCE_KEY2_SHORT);
    ASSERT_EQ_U32(config.rules[0].cooldown_ms, saved.rules[0].cooldown_ms);
    ASSERT_TRUE(memcmp(config.rules[0].actions, saved.rules[0].actions, sizeof(config.rules[0].actions)) == 0);
    ASSERT_TRUE(memcmp(&config.rules[1], &saved.rules[1], sizeof(config.rules[1])) == 0);
    config = saved;
    ASSERT_TRUE(ui_runtime_load_automation(&ui, 0));
    ui.automations[0].enabled = true;
    ui.automations[0].trigger_source = RULE_SOURCE_WIFI_CONNECTED;
    ui.automations[0].action_kind = RULE_ACTION_LOCAL_UI;
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ACTION));
    ASSERT_TRUE(rule_config_store_load(&store, &saved));
    ASSERT_TRUE(saved.rules[0].enabled == config.rules[0].enabled);
    ASSERT_EQ_U32(2, saved.rules[0].action_count);
    ASSERT_TRUE(memcmp(&config.rules[0].when, &saved.rules[0].when, sizeof(config.rules[0].when)) == 0);
    ASSERT_TRUE(memcmp(&config.rules[0].actions[1], &saved.rules[0].actions[1], sizeof(config.rules[0].actions[1])) == 0);
    ASSERT_TRUE(memcmp(&config.rules[1], &saved.rules[1], sizeof(config.rules[1])) == 0);
    rule_web_stop(&web);
    rule_config_store_close(&store);
}



static void test_first_action_contract_and_rejection_sequence(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    config.rule_count = 1;
    config.rules[0].enabled = true;
    config.rules[0].action_count = 3;
    for (size_t i = 0; i < 3; ++i) config.rules[0].actions[i].type = RULE_ACTION_LOCAL_UI;
    rule_runtime_t runtime;
    rule_config_store_t store;
    rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    char response[256];
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/rules/test", NULL, response, sizeof(response)));
    ASSERT_TRUE(strstr(response, "\"mode\":\"first_action\"") != NULL);
    ASSERT_TRUE(strstr(response, "\"evaluates_rule\":false") != NULL);
    ASSERT_EQ_U32(1, rule_runtime_process_actions(&runtime));
    ASSERT_EQ_U32(0, runtime.engine.state[0].fire_count);
    rule_event_t old = {.action = RULE_ACTION_LOCAL_UI};
    for (size_t i = 0; i < ACTION_DISPATCHER_QUEUE_LEN; ++i) ASSERT_TRUE(action_enqueue(&runtime.dispatcher, &old));
    const uint32_t sequence = runtime.engine.next_event_sequence;
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/rules/test", NULL, response, sizeof(response)));
    ASSERT_TRUE(strstr(response, "\"queued\":false") != NULL);
    ASSERT_EQ_U32(sequence, runtime.engine.next_event_sequence);
    rule_web_stop(&web);
    rule_config_store_close(&store);
}

static void test_session_authorization_and_small_config_ack(void)
{
    automation_config_t config; automation_config_set_defaults(&config);
    rule_runtime_t runtime; rule_config_store_t store; rule_web_t web;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&store));
    ASSERT_TRUE(rule_web_start(&web, &runtime, &store));
    ASSERT_TRUE(!rule_web_authorize(&web, "0123456789abcdef"));
    rule_web_set_access_token(&web, "0123456789abcdef");
    ASSERT_TRUE(!rule_web_authorize(&web, NULL));
    ASSERT_TRUE(rule_web_authorize(&web, "0123456789abcdef"));
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(!rule_web_authorize(&web, "wrong"));
    ASSERT_TRUE(!rule_web_authorize(&web, "0123456789abcdef"));
    rule_web_set_access_token(&web, "fedcba9876543210");
    ASSERT_TRUE(!rule_web_authorize(&web, "0123456789abcdef"));
    ASSERT_TRUE(rule_web_authorize(&web, "fedcba9876543210"));
    char ack[32];
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config", "defaults", ack, sizeof(ack)));
    ASSERT_TRUE(strcmp(ack, "{\"ok\":true}") == 0);
    rule_web_stop(&web);
    ASSERT_TRUE(!rule_web_authorize(&web, "fedcba9876543210"));
    ASSERT_TRUE(web.access_token[0] == '\0');
    rule_config_store_close(&store);
}

int main(void)
{
    test_first_action_contract_and_rejection_sequence();
    test_session_authorization_and_small_config_ack();
    test_strict_config_parsing_preserves_runtime_and_store();
    test_browser_lcd_roundtrip_preserves_unedited_settings();
    test_config_edits_and_roundtrip_preserve_unedited_state();
    test_speaker_form_save_preserves_parameters_and_rejects_wrapped_volume();
    test_export_fits_import_limit_and_oversize_requests_do_not_mutate();
    test_gpio_digital_debounce_emits_safe_pin();
    test_disabled_hat_does_not_probe();
    test_rule_web_status();
    test_complete_config_roundtrip_and_strict_parser();
    puts("external_triggers_and_web tests passed");
    return 0;
}
