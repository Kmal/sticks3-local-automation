#include "app_wifi.h"
#include "action_http.h"
#include "sdkconfig.h"
#include "status_ui.h"

#include <stdio.h>
#include <string.h>

#ifndef CONFIG_APP_WIFI_ENABLE
#define CONFIG_APP_WIFI_ENABLE 0
#endif
#ifndef CONFIG_APP_WIFI_AP_SSID
#define CONFIG_APP_WIFI_AP_SSID "M5StickS3-Setup"
#endif
#ifndef CONFIG_APP_WIFI_AP_PASSWORD
#define CONFIG_APP_WIFI_AP_PASSWORD ""
#endif
#ifndef CONFIG_APP_WIFI_AP_CHANNEL
#define CONFIG_APP_WIFI_AP_CHANNEL 6
#endif
#ifndef CONFIG_APP_WIFI_AP_MAX_CONNECTIONS
#define CONFIG_APP_WIFI_AP_MAX_CONNECTIONS 4
#endif
#ifndef CONFIG_APP_WIFI_CONNECT_TIMEOUT_MS
#define CONFIG_APP_WIFI_CONNECT_TIMEOUT_MS 15000
#endif
#ifndef CONFIG_APP_WIFI_MAX_SCAN_RESULTS
#define CONFIG_APP_WIFI_MAX_SCAN_RESULTS APP_WIFI_MAX_SCAN_RESULTS
#endif
#ifndef CONFIG_APP_WIFI_KEYBOARD_PROVISIONING
#define CONFIG_APP_WIFI_KEYBOARD_PROVISIONING 0
#endif
#ifndef CONFIG_APP_WIFI_KEYBOARD_TIMEOUT_MS
#define CONFIG_APP_WIFI_KEYBOARD_TIMEOUT_MS 0
#endif
#ifndef CONFIG_APP_WIFI_NTP_SYNC_TIMEOUT_MS
#define CONFIG_APP_WIFI_NTP_SYNC_TIMEOUT_MS 15000
#endif

#define APP_WIFI_IP_LEN 16u
#define APP_WIFI_SSID_MAX_LEN 32u
#define APP_WIFI_PASSWORD_MAX_LEN 63u
#define APP_WIFI_NTP_SYNC_TASK_STACK 4096u
#define APP_WIFI_NTP_SYNC_TASK_PRIORITY 4u
#define APP_WIFI_NTP_SERVER_COUNT 4u

static size_t bounded_strlen(const char *value, size_t max_len)
{
    size_t len = 0u;
    if (value == NULL) {
        return 0u;
    }
    while (len < max_len && value[len] != '\0') {
        ++len;
    }
    return len;
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0u) {
        return;
    }
    const char *value = src != NULL ? src : "";
    size_t len = bounded_strlen(value, dst_size - 1u);
    memcpy(dst, value, len);
    dst[len] = '\0';
}

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "time.h"

#define APP_WIFI_NVS_NAMESPACE "app_wifi"
#define APP_WIFI_NVS_SSID "ssid"
#define APP_WIFI_NVS_PASSWORD "password"
#define APP_WIFI_NVS_AP_SSID "ap_ssid"
#define APP_WIFI_NVS_AP_PASSWORD "ap_password"
#define APP_WIFI_NVS_AP_CHANNEL "ap_channel"
#define APP_WIFI_NVS_AP_MAX_CONNECTIONS "ap_max_conn"
#define APP_WIFI_NVS_MODE "preferred_mode"

static const char *TAG = "APP_WIFI";

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_ap_started;
static bool s_sta_connected;
static char s_sta_ip[APP_WIFI_IP_LEN] = "0.0.0.0";
static char s_ap_ip[APP_WIFI_IP_LEN] = "0.0.0.0";
static app_wifi_config_t s_config;
static bool s_config_loaded;
static EventGroupHandle_t s_wifi_events;
static bool s_ntp_initialized;
static bool s_ntp_sync_task_running;
static bool s_last_connect_failed_due_to_password;

#define APP_WIFI_EVENT_STA_CONNECTED BIT0
#define APP_WIFI_EVENT_STA_DISCONNECTED BIT1


static bool disconnect_reason_indicates_password(uint8_t reason)
{
#ifdef WIFI_REASON_AUTH_FAIL
    if (reason == WIFI_REASON_AUTH_FAIL) return true;
#endif
#ifdef WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
    if (reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) return true;
#endif
#ifdef WIFI_REASON_HANDSHAKE_TIMEOUT
    if (reason == WIFI_REASON_HANDSHAKE_TIMEOUT) return true;
#endif
#ifdef WIFI_REASON_MIC_FAILURE
    if (reason == WIFI_REASON_MIC_FAILURE) return true;
#endif
    return false;
}

static bool system_time_is_synced(void)
{
    time_t now = time(NULL);
    struct tm local;
    return now > 0 && localtime_r(&now, &local) != NULL && local.tm_year >= 120;
}

static void app_wifi_ntp_sync_task(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "starting SNTP time sync");
    if (!s_ntp_initialized) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
            APP_WIFI_NTP_SERVER_COUNT,
            ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com", "time.cloudflare.com", "time.nist.gov"));
        config.start = false;
        esp_err_t err = esp_netif_sntp_init(&config);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            s_ntp_initialized = true;
        } else {
            ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
        }
    }

    if (s_ntp_initialized) {
        esp_err_t err = esp_netif_sntp_start();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
        } else {
            err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(CONFIG_APP_WIFI_NTP_SYNC_TIMEOUT_MS));
            if (err == ESP_OK || system_time_is_synced()) {
                char time_buf[32];
                time_t now = time(NULL);
                struct tm local;
                if (localtime_r(&now, &local) != NULL && strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &local) > 0u) {
                    ESP_LOGI(TAG, "SNTP time synchronized: %s", time_buf);
                } else {
                    ESP_LOGI(TAG, "SNTP time synchronized");
                }
            } else {
                ESP_LOGW(TAG, "SNTP sync timed out: %s", esp_err_to_name(err));
            }
        }
    }

    s_ntp_sync_task_running = false;
    vTaskDelete(NULL);
}

static void app_wifi_start_ntp_sync(void)
{
    if (s_ntp_sync_task_running || system_time_is_synced()) {
        return;
    }
    s_ntp_sync_task_running = true;
    BaseType_t task_ok = xTaskCreate(app_wifi_ntp_sync_task, "wifi_ntp_sync", APP_WIFI_NTP_SYNC_TASK_STACK, NULL,
                                     APP_WIFI_NTP_SYNC_TASK_PRIORITY, NULL);
    if (task_ok != pdPASS) {
        s_ntp_sync_task_running = false;
        ESP_LOGW(TAG, "failed to create SNTP sync task");
    }
}

static bool validate_ssid(const char *ssid)
{
    return ssid != NULL && ssid[0] != '\0' &&
           bounded_strlen(ssid, APP_WIFI_SSID_MAX_LEN + 1u) <= APP_WIFI_SSID_MAX_LEN;
}

static bool validate_password(const char *password)
{
    size_t len = password != NULL ? strlen(password) : 0u;
    return len == 0u || (len >= 8u && len <= APP_WIFI_PASSWORD_MAX_LEN);
}

static bool validate_channel(uint8_t channel)
{
    return channel >= 1u && channel <= 13u;
}

const char *app_wifi_mode_name(app_wifi_mode_t mode)
{
    switch (mode) {
    case APP_WIFI_MODE_OFF: return "off";
    case APP_WIFI_MODE_STA: return "wifi";
    case APP_WIFI_MODE_AP: return "ap";
    case APP_WIFI_MODE_APSTA: return "apsta";
    default: return "unknown";
    }
}

static void config_defaults(app_wifi_config_t *config)
{
    memset(config, 0, sizeof(*config));
    copy_string(config->ap_ssid, sizeof(config->ap_ssid), CONFIG_APP_WIFI_AP_SSID);
    copy_string(config->ap_password, sizeof(config->ap_password), CONFIG_APP_WIFI_AP_PASSWORD);
    config->ap_channel = CONFIG_APP_WIFI_AP_CHANNEL;
    config->ap_max_connections = CONFIG_APP_WIFI_AP_MAX_CONNECTIONS;
    config->preferred_mode = APP_WIFI_MODE_STA;
}

static bool persist_config(const app_wifi_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open Wi-Fi NVS failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_str(handle, APP_WIFI_NVS_SSID, config->sta_ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, APP_WIFI_NVS_PASSWORD, config->sta_password);
    if (err == ESP_OK) err = nvs_set_str(handle, APP_WIFI_NVS_AP_SSID, config->ap_ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, APP_WIFI_NVS_AP_PASSWORD, config->ap_password);
    if (err == ESP_OK) err = nvs_set_u8(handle, APP_WIFI_NVS_AP_CHANNEL, config->ap_channel);
    if (err == ESP_OK) err = nvs_set_u8(handle, APP_WIFI_NVS_AP_MAX_CONNECTIONS, config->ap_max_connections);
    if (err == ESP_OK) err = nvs_set_u8(handle, APP_WIFI_NVS_MODE, (uint8_t)config->preferred_mode);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save Wi-Fi config failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void load_config_once(void)
{
    if (s_config_loaded) {
        return;
    }
    config_defaults(&s_config);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no saved Wi-Fi config: %s", esp_err_to_name(err));
        s_config_loaded = true;
        return;
    }
    size_t required = sizeof(s_config.sta_ssid);
    (void)nvs_get_str(handle, APP_WIFI_NVS_SSID, s_config.sta_ssid, &required);
    required = sizeof(s_config.sta_password);
    (void)nvs_get_str(handle, APP_WIFI_NVS_PASSWORD, s_config.sta_password, &required);
    required = sizeof(s_config.ap_ssid);
    (void)nvs_get_str(handle, APP_WIFI_NVS_AP_SSID, s_config.ap_ssid, &required);
    required = sizeof(s_config.ap_password);
    (void)nvs_get_str(handle, APP_WIFI_NVS_AP_PASSWORD, s_config.ap_password, &required);
    uint8_t channel = s_config.ap_channel;
    if (nvs_get_u8(handle, APP_WIFI_NVS_AP_CHANNEL, &channel) == ESP_OK && validate_channel(channel)) {
        s_config.ap_channel = channel;
    }
    uint8_t max_connections = s_config.ap_max_connections;
    if (nvs_get_u8(handle, APP_WIFI_NVS_AP_MAX_CONNECTIONS, &max_connections) == ESP_OK &&
        max_connections > 0u && max_connections <= 10u) {
        s_config.ap_max_connections = max_connections;
    }
    uint8_t mode = (uint8_t)s_config.preferred_mode;
    if (nvs_get_u8(handle, APP_WIFI_NVS_MODE, &mode) == ESP_OK && mode <= APP_WIFI_MODE_APSTA) {
        s_config.preferred_mode = (app_wifi_mode_t)mode;
    }
    nvs_close(handle);
    if (!validate_ssid(s_config.ap_ssid)) {
        copy_string(s_config.ap_ssid, sizeof(s_config.ap_ssid), CONFIG_APP_WIFI_AP_SSID);
    }
    if (!validate_password(s_config.ap_password)) {
        s_config.ap_password[0] = '\0';
    }
    s_config_loaded = true;
}

static void copy_ip(char out[APP_WIFI_IP_LEN], esp_ip4_addr_t ip)
{
    (void)snprintf(out, APP_WIFI_IP_LEN, IPSTR, IP2STR(&ip));
}

static app_wifi_mode_t active_mode(void)
{
    if (s_sta_connected && s_ap_started) return APP_WIFI_MODE_APSTA;
    if (s_sta_connected) return APP_WIFI_MODE_STA;
    if (s_ap_started) return APP_WIFI_MODE_AP;
    return APP_WIFI_MODE_OFF;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)data;
            uint8_t reason = event != NULL ? event->reason : 0u;
            s_sta_connected = false;
            copy_string(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
            s_last_connect_failed_due_to_password = disconnect_reason_indicates_password(reason);
            action_http_set_network_ready(false);
            if (s_wifi_events != NULL) {
                xEventGroupSetBits(s_wifi_events, APP_WIFI_EVENT_STA_DISCONNECTED);
            }
            ESP_LOGI(TAG, "station disconnected: reason=%u password_related=%s", (unsigned)reason, s_last_connect_failed_due_to_password ? "yes" : "no");
        } else if (id == WIFI_EVENT_AP_START) {
            s_ap_started = true;
            esp_netif_ip_info_t ip_info;
            if (s_ap_netif != NULL && esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
                copy_ip(s_ap_ip, ip_info.ip);
            }
            ESP_LOGI(TAG, "setup AP started: ssid=%s ip=%s", s_config.ap_ssid, s_ap_ip);
        } else if (id == WIFI_EVENT_AP_STOP) {
            s_ap_started = false;
            copy_string(s_ap_ip, sizeof(s_ap_ip), "0.0.0.0");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_sta_connected = true;
        copy_ip(s_sta_ip, event->ip_info.ip);
        action_http_set_network_ready(true);
        if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, APP_WIFI_EVENT_STA_CONNECTED);
        }
        ESP_LOGI(TAG, "station connected: ssid=%s ip=%s", s_config.sta_ssid, s_sta_ip);
        app_wifi_start_ntp_sync();
    }
}

static bool app_wifi_init_once(void)
{
    load_config_once();
    if (s_wifi_initialized) {
        return true;
    }
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "failed to create default Wi-Fi netifs");
        return false;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        ESP_LOGE(TAG, "failed to create Wi-Fi event group");
        return false;
    }
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    s_wifi_initialized = true;
    return true;
}

static bool ensure_wifi_started(void)
{
    if (!s_wifi_initialized && !app_wifi_init_once()) {
        return false;
    }
    if (s_wifi_started) {
        return true;
    }
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return false;
    }
    s_wifi_started = true;
    return true;
}

static bool set_driver_mode(wifi_mode_t mode)
{
    esp_err_t err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(%d) failed: %s", (int)mode, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool app_wifi_get_config(app_wifi_config_t *out)
{
    if (out == NULL) {
        return false;
    }
    load_config_once();
    *out = s_config;
    return true;
}

bool app_wifi_set_config(const app_wifi_config_t *config, bool persist)
{
    if (config == NULL || !validate_ssid(config->ap_ssid) || !validate_password(config->ap_password) ||
        !validate_channel(config->ap_channel) || config->preferred_mode > APP_WIFI_MODE_APSTA) {
        return false;
    }
    if (config->sta_ssid[0] != '\0' && strlen(config->sta_ssid) > 32u) {
        return false;
    }
    if (strlen(config->sta_password) > 63u || config->ap_max_connections == 0u || config->ap_max_connections > 10u) {
        return false;
    }
    s_config = *config;
    s_config_loaded = true;
    return !persist || persist_config(&s_config);
}

bool app_wifi_get_status(app_wifi_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    load_config_once();
    memset(out, 0, sizeof(*out));
    out->enabled = CONFIG_APP_WIFI_ENABLE;
    out->active_mode = active_mode();
    out->sta_connected = s_sta_connected;
    copy_string(out->sta_ssid, sizeof(out->sta_ssid), s_config.sta_ssid);
    copy_string(out->sta_ip, sizeof(out->sta_ip), s_sta_ip);
    out->ap_started = s_ap_started;
    copy_string(out->ap_ssid, sizeof(out->ap_ssid), s_config.ap_ssid);
    copy_string(out->ap_ip, sizeof(out->ap_ip), s_ap_ip);
    out->ap_channel = s_config.ap_channel;
    out->ap_max_connections = s_config.ap_max_connections;
    (void)snprintf(out->web_url, sizeof(out->web_url), "http://%s/", s_sta_connected ? s_sta_ip : s_ap_ip);
    return true;
}

bool app_wifi_set_mode(app_wifi_mode_t mode)
{
#if !CONFIG_APP_WIFI_ENABLE
    (void)mode;
    return false;
#else
    if (!app_wifi_init_once() || mode > APP_WIFI_MODE_APSTA) {
        return false;
    }
    s_config.preferred_mode = mode;
    if (mode == APP_WIFI_MODE_OFF) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
            ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));
            return false;
        }
        s_wifi_started = false;
        s_ap_started = false;
        s_sta_connected = false;
        action_http_set_network_ready(false);
        return persist_config(&s_config);
    }
    if (mode == APP_WIFI_MODE_AP) {
        if (s_sta_connected) {
            (void)esp_wifi_disconnect();
            s_sta_connected = false;
            action_http_set_network_ready(false);
        }
        return app_wifi_start_ap_configured(s_config.ap_ssid, s_config.ap_password, s_config.ap_channel, true);
    }
    if (mode == APP_WIFI_MODE_APSTA) {
        if (!s_sta_connected && s_config.sta_ssid[0] != '\0') {
            (void)app_wifi_connect(s_config.sta_ssid, s_config.sta_password, false);
        }
        return app_wifi_start_ap_configured(s_config.ap_ssid, s_config.ap_password, s_config.ap_channel, true);
    }
    if (s_ap_started) {
        s_ap_started = false;
        if (!set_driver_mode(WIFI_MODE_STA)) {
            return false;
        }
    }
    if (s_config.sta_ssid[0] != '\0') {
        bool connected = app_wifi_connect(s_config.sta_ssid, s_config.sta_password, false);
        if (connected) {
            s_config.preferred_mode = APP_WIFI_MODE_STA;
            (void)persist_config(&s_config);
        }
        return connected;
    }
    if (!set_driver_mode(WIFI_MODE_STA) || !ensure_wifi_started()) {
        return false;
    }
    return persist_config(&s_config);
#endif
}

static bool copy_wifi_ssid(uint8_t dst[32], const char *ssid, uint8_t *ssid_len)
{
    const size_t len = ssid != NULL ? bounded_strlen(ssid, APP_WIFI_SSID_MAX_LEN + 1u) : 0u;
    if (len == 0u || len > APP_WIFI_SSID_MAX_LEN) {
        return false;
    }
    memset(dst, 0, 32u);
    memcpy(dst, ssid, len);
    if (ssid_len != NULL) {
        *ssid_len = (uint8_t)len;
    }
    return true;
}

static bool copy_wifi_password(uint8_t dst[64], const char *password)
{
    const char *value = password != NULL ? password : "";
    const size_t len = bounded_strlen(value, APP_WIFI_PASSWORD_MAX_LEN + 1u);
    if (len > APP_WIFI_PASSWORD_MAX_LEN) {
        return false;
    }
    memset(dst, 0, 64u);
    memcpy(dst, value, len);
    return true;
}

bool app_wifi_start_ap_configured(const char *ap_ssid, const char *ap_password, uint8_t channel, bool persist)
{
#if !CONFIG_APP_WIFI_ENABLE
    (void)ap_ssid; (void)ap_password; (void)channel; (void)persist;
    return false;
#else
    if (!validate_ssid(ap_ssid) || !validate_password(ap_password) || !validate_channel(channel)) {
        ESP_LOGW(TAG, "invalid AP configuration");
        return false;
    }
    if (!app_wifi_init_once()) {
        return false;
    }
    copy_string(s_config.ap_ssid, sizeof(s_config.ap_ssid), ap_ssid);
    copy_string(s_config.ap_password, sizeof(s_config.ap_password), ap_password);
    s_config.ap_channel = channel;
    s_config.preferred_mode = s_sta_connected ? APP_WIFI_MODE_APSTA : APP_WIFI_MODE_AP;
    if (persist && !persist_config(&s_config)) {
        ESP_LOGW(TAG, "failed to persist AP configuration");
    }
    if (!set_driver_mode(s_sta_connected ? WIFI_MODE_APSTA : WIFI_MODE_AP)) {
        return false;
    }
    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    if (!copy_wifi_ssid(ap_config.ap.ssid, s_config.ap_ssid, &ap_config.ap.ssid_len) ||
        !copy_wifi_password(ap_config.ap.password, s_config.ap_password)) {
        ESP_LOGE(TAG, "validated AP configuration did not fit ESP Wi-Fi fields");
        return false;
    }
    ap_config.ap.channel = s_config.ap_channel;
    ap_config.ap.max_connection = s_config.ap_max_connections;
    ap_config.ap.authmode = strlen(s_config.ap_password) >= 8u ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (ap_config.ap.authmode == WIFI_AUTH_OPEN) {
        ap_config.ap.password[0] = '\0';
    }
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set AP config failed: %s", esp_err_to_name(err));
        return false;
    }
    if (!ensure_wifi_started()) {
        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (s_ap_netif != NULL && esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
        copy_ip(s_ap_ip, ip_info.ip);
    }
    s_ap_started = true;
    ESP_LOGI(TAG, "setup AP ready: ssid=%s security=%s url=http://%s/",
             s_config.ap_ssid, strlen(s_config.ap_password) >= 8u ? "WPA2" : "open", s_ap_ip);
    return true;
#endif
}

bool app_wifi_connect(const char *ssid, const char *password, bool persist)
{
#if !CONFIG_APP_WIFI_ENABLE
    (void)ssid; (void)password; (void)persist;
    return false;
#else
    if (!validate_ssid(ssid)) {
        ESP_LOGW(TAG, "invalid SSID");
        return false;
    }
    if (password != NULL && strlen(password) > APP_WIFI_PASSWORD_MAX_LEN) {
        ESP_LOGW(TAG, "invalid password length");
        return false;
    }
    if (!app_wifi_init_once()) {
        return false;
    }
    if (!set_driver_mode(s_ap_started ? WIFI_MODE_APSTA : WIFI_MODE_STA)) {
        return false;
    }
    wifi_config_t sta_config;
    memset(&sta_config, 0, sizeof(sta_config));
    if (!copy_wifi_ssid(sta_config.sta.ssid, ssid, NULL) ||
        !copy_wifi_password(sta_config.sta.password, password)) {
        ESP_LOGE(TAG, "validated STA configuration did not fit ESP Wi-Fi fields");
        return false;
    }
    sta_config.sta.threshold.authmode = (password != NULL && strlen(password) >= 8u) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set STA config failed: %s", esp_err_to_name(err));
        return false;
    }
    if (!ensure_wifi_started()) {
        return false;
    }
    copy_string(s_config.sta_ssid, sizeof(s_config.sta_ssid), ssid);
    copy_string(s_config.sta_password, sizeof(s_config.sta_password), password);
    s_config.sta_persist = persist;
    s_config.preferred_mode = s_ap_started ? APP_WIFI_MODE_APSTA : APP_WIFI_MODE_STA;
    s_sta_connected = false;
    s_last_connect_failed_due_to_password = false;
    action_http_set_network_ready(false);
    if (s_wifi_events != NULL) {
        xEventGroupClearBits(s_wifi_events, APP_WIFI_EVENT_STA_CONNECTED | APP_WIFI_EVENT_STA_DISCONNECTED);
    }
    (void)esp_wifi_disconnect();
    if (s_wifi_events != NULL) {
        xEventGroupClearBits(s_wifi_events, APP_WIFI_EVENT_STA_CONNECTED | APP_WIFI_EVENT_STA_DISCONNECTED);
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect start failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "connecting to Wi-Fi SSID %s", ssid);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(CONFIG_APP_WIFI_CONNECT_TIMEOUT_MS);
    if (s_wifi_events != NULL) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_events, APP_WIFI_EVENT_STA_CONNECTED | APP_WIFI_EVENT_STA_DISCONNECTED, pdFALSE, pdFALSE, timeout_ticks);
        if ((bits & APP_WIFI_EVENT_STA_CONNECTED) == 0u && !s_sta_connected) {
            if ((bits & APP_WIFI_EVENT_STA_DISCONNECTED) != 0u) {
                ESP_LOGW(TAG, "connection failed for SSID %s (password_related=%s)", ssid, s_last_connect_failed_due_to_password ? "yes" : "no");
            } else {
                ESP_LOGW(TAG, "connection timed out waiting for IP for SSID %s", ssid);
            }
            return false;
        }
    } else {
        const TickType_t deadline = xTaskGetTickCount() + timeout_ticks;
        while (!s_sta_connected && xTaskGetTickCount() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!s_sta_connected) {
            ESP_LOGW(TAG, "connection timed out waiting for IP for SSID %s", ssid);
            return false;
        }
    }
    s_last_connect_failed_due_to_password = false;
    if (persist && !persist_config(&s_config)) {
        ESP_LOGW(TAG, "connected but failed to persist Wi-Fi credentials");
    }
    if (status_ui_get_service_enabled()) {
        ESP_LOGI(TAG, "web UI available on station network: http://%s/", s_sta_ip);
    } else {
        ESP_LOGI(TAG, "station network ready: ip=%s; web UI service is off", s_sta_ip);
    }
    return true;
#endif
}

static bool prompt_credentials_and_connect(void)
{
#if CONFIG_APP_WIFI_KEYBOARD_PROVISIONING
    char ssid[33];
    char password[65];
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    ESP_LOGI(TAG, "starting LCD virtual keyboard Wi-Fi provisioning");
    if (!status_ui_keyboard_read_line("WiFi SSID", "", ssid, sizeof(ssid), 32,
                                      false, CONFIG_APP_WIFI_KEYBOARD_TIMEOUT_MS) || ssid[0] == '\0') {
        ESP_LOGI(TAG, "LCD Wi-Fi SSID entry cancelled or timed out");
        return false;
    }
    if (!status_ui_keyboard_read_line("WiFi PASSWORD", "", password, sizeof(password), 63,
                                      true, CONFIG_APP_WIFI_KEYBOARD_TIMEOUT_MS)) {
        ESP_LOGI(TAG, "LCD Wi-Fi password entry cancelled or timed out");
        return false;
    }
    return app_wifi_connect(ssid, password, true);
#else
    return false;
#endif
}

bool app_wifi_start(void)
{
#if !CONFIG_APP_WIFI_ENABLE
    ESP_LOGI(TAG, "Wi-Fi manager disabled");
    action_http_set_network_ready(false);
    return false;
#else
    if (!app_wifi_init_once()) {
        return false;
    }
    if (s_config.sta_ssid[0] != '\0') {
        ESP_LOGI(TAG, "trying saved Wi-Fi SSID %s", s_config.sta_ssid);
        if (app_wifi_connect(s_config.sta_ssid, s_config.sta_password, false)) {
            return true;
        }
    }
    if (prompt_credentials_and_connect()) {
        return true;
    }
    ESP_LOGI(TAG, "starting setup AP for Wi-Fi selection and web UI");
    return app_wifi_start_ap_configured(s_config.ap_ssid, s_config.ap_password, s_config.ap_channel, false);
#endif
}

bool app_wifi_forget_sta_credentials(void)
{
    load_config_once();
    s_config.sta_ssid[0] = '\0';
    s_config.sta_password[0] = '\0';
    s_config.sta_persist = false;
    return persist_config(&s_config);
}

bool app_wifi_last_connect_failed_due_to_password(void)
{
    return s_last_connect_failed_due_to_password;
}

bool app_wifi_scan(app_wifi_scan_results_t *out)
{
#if !CONFIG_APP_WIFI_ENABLE
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        (void)snprintf(out->error, sizeof(out->error), "wifi_disabled");
    }
    return false;
#else
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!app_wifi_init_once() || !set_driver_mode(s_ap_started ? WIFI_MODE_APSTA : WIFI_MODE_STA) || !ensure_wifi_started()) {
        (void)snprintf(out->error, sizeof(out->error), "wifi_start_failed");
        return false;
    }
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        (void)snprintf(out->error, sizeof(out->error), "scan_failed");
        return false;
    }
    uint16_t count = CONFIG_APP_WIFI_MAX_SCAN_RESULTS;
    if (count > APP_WIFI_MAX_SCAN_RESULTS) {
        count = APP_WIFI_MAX_SCAN_RESULTS;
    }
    wifi_ap_record_t records[APP_WIFI_MAX_SCAN_RESULTS];
    memset(records, 0, sizeof(records));
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan records failed: %s", esp_err_to_name(err));
        (void)snprintf(out->error, sizeof(out->error), "scan_records_failed");
        return false;
    }
    out->ok = true;
    out->count = count;
    for (uint16_t i = 0; i < count; ++i) {
        memcpy(out->items[i].ssid, records[i].ssid, 32);
        out->items[i].ssid[32] = '\0';
        out->items[i].rssi = (int8_t)records[i].rssi;
        out->items[i].authmode = (uint8_t)records[i].authmode;
        out->items[i].channel = records[i].primary;
        out->items[i].secure = records[i].authmode != WIFI_AUTH_OPEN;
    }
    return true;
#endif
}

static bool append_json_string(char **cursor, size_t *remaining, const char *value)
{
    int written = snprintf(*cursor, *remaining, "\"");
    if (written < 0 || (size_t)written >= *remaining) return false;
    *cursor += written;
    *remaining -= (size_t)written;
    for (const unsigned char *ch = (const unsigned char *)(value != NULL ? value : ""); *ch != '\0'; ++ch) {
        const char *escape = NULL;
        char escape_buf[7];
        switch (*ch) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default:
            if (*ch < 0x20u) {
                (void)snprintf(escape_buf, sizeof(escape_buf), "\\u%04x", (unsigned)*ch);
                escape = escape_buf;
            }
            break;
        }
        written = snprintf(*cursor, *remaining, "%s", escape != NULL ? escape : (char[]){(char)*ch, '\0'});
        if (written < 0 || (size_t)written >= *remaining) return false;
        *cursor += written;
        *remaining -= (size_t)written;
    }
    written = snprintf(*cursor, *remaining, "\"");
    if (written < 0 || (size_t)written >= *remaining) return false;
    *cursor += written;
    *remaining -= (size_t)written;
    return true;
}

bool app_wifi_scan_json(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }
    app_wifi_scan_results_t scan = {0};
    if (!app_wifi_scan(&scan)) {
        const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"%s\"}", scan.error[0] != '\0' ? scan.error : "scan_failed");
        return written > 0 && (size_t)written < out_len;
    }
    char *cursor = out;
    size_t remaining = out_len;
    int written = snprintf(cursor, remaining, "{\"ok\":true,\"networks\":[");
    if (written < 0 || (size_t)written >= remaining) return false;
    cursor += written;
    remaining -= (size_t)written;
    for (size_t i = 0; i < scan.count; ++i) {
        written = snprintf(cursor, remaining, "%s{\"ssid\":", i == 0u ? "" : ",");
        if (written < 0 || (size_t)written >= remaining) return false;
        cursor += written;
        remaining -= (size_t)written;
        if (!append_json_string(&cursor, &remaining, scan.items[i].ssid)) return false;
        written = snprintf(cursor, remaining, ",\"rssi\":%d,\"auth\":%u,\"channel\":%u,\"secure\":%s}",
                           scan.items[i].rssi, (unsigned)scan.items[i].authmode,
                           (unsigned)scan.items[i].channel, scan.items[i].secure ? "true" : "false");
        if (written < 0 || (size_t)written >= remaining) return false;
        cursor += written;
        remaining -= (size_t)written;
    }
    written = snprintf(cursor, remaining, "]}");
    return written > 0 && (size_t)written < remaining;
}

bool app_wifi_status_json(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }
    app_wifi_status_t status;
    if (!app_wifi_get_status(&status)) {
        return false;
    }
    char *cursor = out;
    size_t remaining = out_len;
    int written = snprintf(cursor, remaining,
                           "{\"enabled\":true,\"mode\":\"%s\",\"sta_connected\":%s,\"sta_ssid\":",
                           app_wifi_mode_name(status.active_mode), status.sta_connected ? "true" : "false");
    if (written < 0 || (size_t)written >= remaining) return false;
    cursor += written;
    remaining -= (size_t)written;
    if (!append_json_string(&cursor, &remaining, status.sta_ssid)) return false;
    written = snprintf(cursor, remaining, ",\"sta_ip\":\"%s\",\"ap_started\":%s,\"ap_ssid\":",
                       status.sta_ip, status.ap_started ? "true" : "false");
    if (written < 0 || (size_t)written >= remaining) return false;
    cursor += written;
    remaining -= (size_t)written;
    if (!append_json_string(&cursor, &remaining, status.ap_ssid)) return false;
    written = snprintf(cursor, remaining,
                       ",\"ap_ip\":\"%s\",\"ap_channel\":%u,\"ap_max_connections\":%u,\"web_url\":\"%s\"}",
                       status.ap_ip, (unsigned)status.ap_channel,
                       (unsigned)status.ap_max_connections, status.web_url);
    return written > 0 && (size_t)written < remaining;
}

bool app_wifi_is_sta_connected(void)
{
    return s_sta_connected;
}

#else

static app_wifi_config_t s_config;
static bool s_config_loaded;
static bool s_sta_connected;
static bool s_ap_started;

static void config_defaults(app_wifi_config_t *config)
{
    memset(config, 0, sizeof(*config));
    copy_string(config->ap_ssid, sizeof(config->ap_ssid), CONFIG_APP_WIFI_AP_SSID);
    copy_string(config->ap_password, sizeof(config->ap_password), CONFIG_APP_WIFI_AP_PASSWORD);
    config->ap_channel = CONFIG_APP_WIFI_AP_CHANNEL;
    config->ap_max_connections = CONFIG_APP_WIFI_AP_MAX_CONNECTIONS;
    config->preferred_mode = APP_WIFI_MODE_STA;
}

static void load_config_once(void)
{
    if (!s_config_loaded) {
        config_defaults(&s_config);
        s_config_loaded = true;
    }
}

static bool validate_ssid(const char *ssid)
{
    return ssid != NULL && ssid[0] != '\0' &&
           bounded_strlen(ssid, APP_WIFI_SSID_MAX_LEN + 1u) <= APP_WIFI_SSID_MAX_LEN;
}

static bool validate_password(const char *password)
{
    size_t len = password != NULL ? strlen(password) : 0u;
    return len == 0u || (len >= 8u && len <= APP_WIFI_PASSWORD_MAX_LEN);
}

static bool validate_channel(uint8_t channel)
{
    return channel >= 1u && channel <= 13u;
}

const char *app_wifi_mode_name(app_wifi_mode_t mode)
{
    switch (mode) {
    case APP_WIFI_MODE_OFF: return "off";
    case APP_WIFI_MODE_STA: return "wifi";
    case APP_WIFI_MODE_AP: return "ap";
    case APP_WIFI_MODE_APSTA: return "apsta";
    default: return "unknown";
    }
}

bool app_wifi_start(void)
{
    return false;
}

bool app_wifi_get_config(app_wifi_config_t *out)
{
    if (out == NULL) return false;
    load_config_once();
    *out = s_config;
    return true;
}

bool app_wifi_set_config(const app_wifi_config_t *config, bool persist)
{
    (void)persist;
    if (config == NULL || !validate_ssid(config->ap_ssid) || !validate_password(config->ap_password) || !validate_channel(config->ap_channel)) {
        return false;
    }
    s_config = *config;
    s_config_loaded = true;
    return true;
}

bool app_wifi_get_status(app_wifi_status_t *out)
{
    if (out == NULL) return false;
    load_config_once();
    memset(out, 0, sizeof(*out));
    out->enabled = false;
    out->active_mode = s_config.preferred_mode;
    out->sta_connected = s_sta_connected;
    copy_string(out->sta_ssid, sizeof(out->sta_ssid), s_config.sta_ssid);
    copy_string(out->sta_ip, sizeof(out->sta_ip), "0.0.0.0");
    out->ap_started = s_ap_started;
    copy_string(out->ap_ssid, sizeof(out->ap_ssid), s_config.ap_ssid);
    copy_string(out->ap_ip, sizeof(out->ap_ip), "0.0.0.0");
    out->ap_channel = s_config.ap_channel;
    out->ap_max_connections = s_config.ap_max_connections;
    (void)snprintf(out->web_url, sizeof(out->web_url), "http://0.0.0.0/");
    return true;
}

bool app_wifi_set_mode(app_wifi_mode_t mode)
{
    if (mode > APP_WIFI_MODE_APSTA) return false;
    load_config_once();
    s_config.preferred_mode = mode;
    s_ap_started = mode == APP_WIFI_MODE_AP || mode == APP_WIFI_MODE_APSTA;
    s_sta_connected = mode == APP_WIFI_MODE_STA || mode == APP_WIFI_MODE_APSTA;
    return true;
}

bool app_wifi_connect(const char *ssid, const char *password, bool persist)
{
    (void)persist;
    if (!validate_ssid(ssid) || (password != NULL && strlen(password) > APP_WIFI_PASSWORD_MAX_LEN)) {
        return false;
    }
    load_config_once();
    copy_string(s_config.sta_ssid, sizeof(s_config.sta_ssid), ssid);
    copy_string(s_config.sta_password, sizeof(s_config.sta_password), password);
    return false;
}

bool app_wifi_last_connect_failed_due_to_password(void)
{
    return false;
}

bool app_wifi_scan(app_wifi_scan_results_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    (void)snprintf(out->error, sizeof(out->error), "wifi_unavailable");
    return false;
}

bool app_wifi_start_ap_configured(const char *ap_ssid, const char *ap_password, uint8_t channel, bool persist)
{
    (void)persist;
    if (!validate_ssid(ap_ssid) || !validate_password(ap_password) || !validate_channel(channel)) {
        return false;
    }
    load_config_once();
    copy_string(s_config.ap_ssid, sizeof(s_config.ap_ssid), ap_ssid);
    copy_string(s_config.ap_password, sizeof(s_config.ap_password), ap_password);
    s_config.ap_channel = channel;
    return false;
}

bool app_wifi_forget_sta_credentials(void)
{
    load_config_once();
    s_config.sta_ssid[0] = '\0';
    s_config.sta_password[0] = '\0';
    return true;
}

bool app_wifi_scan_json(char *out, size_t out_len)
{
    app_wifi_scan_results_t scan;
    if (out == NULL || out_len == 0) return false;
    (void)app_wifi_scan(&scan);
    return snprintf(out, out_len, "{\"ok\":false,\"error\":\"%s\"}", scan.error) > 0;
}

bool app_wifi_status_json(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) return false;
    app_wifi_status_t status;
    (void)app_wifi_get_status(&status);
    return snprintf(out, out_len,
                    "{\"enabled\":false,\"mode\":\"%s\",\"sta_connected\":false,\"sta_ssid\":\"%s\",\"sta_ip\":\"%s\",\"ap_started\":false,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"web_url\":\"%s\"}",
                    app_wifi_mode_name(status.active_mode), status.sta_ssid, status.sta_ip,
                    status.ap_ssid, status.ap_ip, status.web_url) > 0;
}

bool app_wifi_is_sta_connected(void)
{
    return false;
}

#endif
