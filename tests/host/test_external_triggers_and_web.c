#include "rule_web.h"
#include "app_wifi.h"
#include "trigger_gpio.h"
#include "trigger_hat.h"

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
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_GET, "/", NULL, json, sizeof(json)));
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
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_GET, "/api/time", NULL, json, sizeof(json)));
    ASSERT_EQ_U32(1, s_runtime_lock_count);
    ASSERT_TRUE(strstr(json, "\"timezone\":\"UTC\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"time_24h\"") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"UTC0\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"timezone\":\"UTC0\"") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"UTC-08\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"timezone\":\"UTC-8\"") != NULL);
    ASSERT_TRUE(getenv("TZ") != NULL && strcmp(getenv("TZ"), "UTC+8") == 0);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"UTC+05:30\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"timezone\":\"UTC+5:30\"") != NULL);
    ASSERT_TRUE(getenv("TZ") != NULL && strcmp(getenv("TZ"), "UTC-5:30") == 0);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"UTC+15\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_timezone") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/time", "{\"timezone\":\"bad tz\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_timezone") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_GET, "/favicon.ico", NULL, json, sizeof(json)));
    ASSERT_TRUE(strcmp(json, "") == 0);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_GET, "/api/wifi/status", NULL, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"enabled\":false") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/scan", NULL, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "wifi_unavailable") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/connect", "{}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "missing_ssid") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"MyDeviceAP\",\"password\":\"password123\",\"channel\":6}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\"") != NULL);
    ASSERT_TRUE(strstr(json, "MyDeviceAP") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"OpenAP\",\"channel\":6}",
                                        json, sizeof(json)));
    app_wifi_config_t wifi_config;
    ASSERT_TRUE(app_wifi_get_config(&wifi_config));
    ASSERT_TRUE(strcmp(wifi_config.ap_ssid, "OpenAP") == 0);
    ASSERT_TRUE(wifi_config.ap_password[0] == '\0');
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"12345678901234567890123456789012\",\"password\":\"123456789012345678901234567890123456789012345678901234567890123\",\"channel\":6}",
                                        json, sizeof(json)));
    ASSERT_TRUE(app_wifi_get_config(&wifi_config));
    ASSERT_TRUE(strcmp(wifi_config.ap_ssid, "12345678901234567890123456789012") == 0);
    ASSERT_TRUE(strlen(wifi_config.ap_password) == 63u);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"123456789012345678901234567890123\",\"channel\":6}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_ap_ssid") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"\",\"password\":\"password123\",\"channel\":6}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_ap_ssid") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/ap",
                                        "{\"ssid\":\"MyDeviceAP\",\"channel\":99}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "invalid_ap_channel") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/wifi/mode",
                                        "{\"mode\":\"ap\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\"") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_GET, "/api/capabilities", NULL, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "pin_conflicts") != NULL);
    ASSERT_TRUE(strstr(json, "source_capabilities") != NULL);
    ASSERT_TRUE(strstr(json, "sound.rms_dbfs") != NULL);
    ASSERT_TRUE(strstr(json, "schema_supported") != NULL);
    ASSERT_TRUE(strstr(json, "runtime_available") != NULL);
    ASSERT_TRUE(strstr(json, "\"runtime_available\":true") != NULL);
    ASSERT_TRUE(strstr(json, "hat.thermal.avg_c") != NULL);
    ASSERT_TRUE(strstr(json, "pulse_count") != NULL);
    ASSERT_TRUE(strstr(json, "speaker_tone") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_GET, "/api/config", NULL, json, sizeof(json)));
    ASSERT_TRUE(s_runtime_lock_count > 1);
    ASSERT_EQ_U32(s_runtime_lock_count, s_runtime_unlock_count);
    ASSERT_TRUE(strstr(json, "threshold_kind") != NULL);
    char exported[16384];
    snprintf(exported, sizeof(exported), "%s", json);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config", exported, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "config rejected") == NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config", "defaults", json, sizeof(json)));
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/rules/test", NULL, json, sizeof(json)));
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config", "{\"preset\":\"sound_local_ui\"}", json, sizeof(json)));
    ASSERT_TRUE(s_config_changed_count > 0);
    ASSERT_TRUE(s_last_config_changed.rules[0].when.source == RULE_SOURCE_SOUND_CLIPPED);
    ASSERT_TRUE(strstr(json, "Sound clipped alert") != NULL);
    ASSERT_TRUE(strstr(json, "sound.clipped") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config", "{\"preset\":\"loud_sound_local_ui\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "Loud sound alert") != NULL);
    ASSERT_TRUE(strstr(json, "sound.rms_dbfs") != NULL);
    ASSERT_TRUE(strstr(json, "\"comparator\":\"gte\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"threshold_i32\":-5120") != NULL);
    ASSERT_TRUE(strstr(json, "\"sustain_ms\":250") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"sound.peak_dbfs\",\"action\":\"local_ui\"}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "sound.peak_dbfs") != NULL);
    ASSERT_TRUE(strstr(json, "\"comparator\":\"gte\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"threshold_i32\":-5120") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/rules/test", NULL, json, sizeof(json)));
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"http_post\",\"http_url\":\"https://example.invalid/hook\",\"http_bearer_token\":\"secret-token\"}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "masked") != NULL);
    ASSERT_TRUE(strstr(json, "secret-token") == NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"speaker_tone\",\"speaker_frequency_hz\":7000,\"speaker_duration_ms\":100,\"speaker_volume_percent\":50,\"action_timeout_ms\":100}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "speaker_tone") != NULL);
    ASSERT_TRUE(strstr(json, "\"speaker_frequency_hz\":7000") != NULL);
    ASSERT_TRUE(strstr(json, "\"speaker_volume_percent\":50") != NULL);
    ASSERT_TRUE(s_last_config_changed.rules[0].actions[0].type == RULE_ACTION_SPEAKER_TONE);
    ASSERT_TRUE(s_last_config_changed.rules[0].actions[0].speaker_frequency_hz == 7000u);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"speaker_tone\",\"speaker_frequency_hz\":9000,\"speaker_duration_ms\":100,\"speaker_volume_percent\":50,\"action_timeout_ms\":100}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "config rejected") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"local_ui\",\"name\":\"Quote \\\"slash\\\\ line\\n\"}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "Quote \\\"slash\\\\ line\\n") != NULL);
    ASSERT_TRUE(strstr(json, "Quote \"slash") == NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"button.key1.short\",\"action\":\"http_post\",\"http_url\":\"ftp://bad.invalid/hook\"}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "config rejected") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"gpio.digital\",\"action\":\"local_ui\",\"gpio_pin\":4,\"gpio_profile\":\"digital_high_low\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "gpio.digital") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"gpio_pin\":4,\"gpio_profile\":\"digital_high_low\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":true") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"gpio_pin\":4x,\"gpio_profile\":\"digital_high_low\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":false") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"gpio_pin\":4,\"gpio_profile\":\"digital_high_low\"x,\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":false") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"gpio_pin\":39,\"gpio_profile\":\"digital_high_low\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":false") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/config",
                                        "{\"source\":\"gpio.edge\",\"action\":\"local_ui\",\"gpio_pin\":4,\"gpio_profile\":\"rising_edge\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "gpio.edge") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"source\":\"gpio.edge\",\"gpio_pin\":4,\"gpio_profile\":\"rising_edge\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":true") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/gpio/test",
                                        "{\"source\":\"gpio.pulse_count\",\"gpio_pin\":4,\"gpio_profile\":\"pulse_count\",\"gpio_debounce_ms\":20}",
                                        json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"ok\":false") != NULL);
    ASSERT_TRUE(strstr(json, "\"supported\":false") != NULL);
    ASSERT_TRUE(rule_web_handle_request(&web, RULE_WEB_METHOD_POST, "/api/hat/probe",
                                        "{\"source\":\"hat.thermal.avg_c\"}", json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "missing_hat_driver") != NULL);
    rule_web_stop(&web);
    rule_config_store_close(&store);
}

int main(void)
{
    test_gpio_digital_debounce_emits_safe_pin();
    test_disabled_hat_does_not_probe();
    test_rule_web_status();
    puts("external_triggers_and_web tests passed");
    return 0;
}
