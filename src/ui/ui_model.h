#pragma once

#include "app_wifi.h"
#include "rule_types.h"
#include "ui_nav.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UI_TEXT_SSID_MAX 32
#define UI_TEXT_WIFI_PASSWORD_MAX 64
#define UI_TEXT_AP_NAME_MAX 32
#define UI_TEXT_AP_PASSWORD_MAX 64
#define UI_AP_CHANNEL_MIN 1
#define UI_AP_CHANNEL_MAX 13
#define UI_AUTOMATION_VISIBLE_COUNT 2

typedef rule_action_type_t rule_action_kind_t;

typedef enum {
    UI_TOAST_NONE = 0,
    UI_TOAST_INFO,
    UI_TOAST_SUCCESS,
    UI_TOAST_WARNING,
    UI_TOAST_ERROR
} ui_toast_kind_t;

typedef struct {
    ui_toast_kind_t kind;
    char text[96];
    uint32_t until_tick_ms;
} ui_toast_t;

typedef struct {
    app_wifi_scan_results_t scan_results;
    size_t selected_scan_index;
    char ssid[UI_TEXT_SSID_MAX + 1];
    char password[UI_TEXT_WIFI_PASSWORD_MAX + 1];
    bool has_selected_ssid;
    bool has_password;
    bool scan_valid;
    char last_error[96];
    char web_url[96];
} ui_wifi_flow_state_t;

typedef struct {
    char ap_name[UI_TEXT_AP_NAME_MAX + 1];
    char ap_password[UI_TEXT_AP_PASSWORD_MAX + 1];
    uint8_t channel;
    char url[96];
    bool loaded_from_config;
    bool started;
    char last_error[96];
} ui_ap_flow_state_t;

typedef struct {
    bool ble_connected;
    char device_name[64];
    char status_text[96];
} ui_bluetooth_state_t;

typedef struct {
    uint8_t rule_index;
    bool loaded;
    bool enabled;
    rule_source_t trigger_source;
    rule_action_kind_t action_kind;
    char trigger_label[64];
    char action_label[64];
    char last_error[96];
} ui_automation_state_t;

typedef struct {
    bool time_valid;
    char time_hhmm[6];
    bool battery_valid;
    uint8_t battery_percent;
    bool wifi_connected;
} ui_status_bar_state_t;

typedef struct {
    ui_nav_state_t nav;
    ui_wifi_flow_state_t config_wifi;
    ui_wifi_flow_state_t connect_wifi;
    ui_ap_flow_state_t ap;
    ui_bluetooth_state_t bluetooth;
    ui_automation_state_t automations[UI_AUTOMATION_VISIBLE_COUNT];
    ui_status_bar_state_t status_bar;
    ui_toast_t toast;
    bool menu_active;
    bool dirty;
} ui_runtime_t;

void ui_runtime_init(ui_runtime_t *ui);
ui_wifi_flow_state_t *ui_runtime_wifi_flow(ui_runtime_t *ui, ui_flow_id_t flow);
const ui_wifi_flow_state_t *ui_runtime_wifi_flow_const(const ui_runtime_t *ui, ui_flow_id_t flow);
void ui_runtime_set_toast(ui_runtime_t *ui, ui_toast_kind_t kind, const char *text, uint32_t ttl_ms);
void ui_runtime_clear_toast(ui_runtime_t *ui);
void ui_runtime_refresh_status_bar(ui_runtime_t *ui);
void ui_runtime_refresh_bluetooth(ui_runtime_t *ui);
bool ui_runtime_load_ap_config(ui_runtime_t *ui);
bool ui_runtime_load_automation(ui_runtime_t *ui, uint8_t automation_index);
typedef enum {
    UI_AUTOMATION_EDIT_ENABLED = 0,
    UI_AUTOMATION_EDIT_TRIGGER,
    UI_AUTOMATION_EDIT_ACTION,
} ui_automation_edit_t;

/* The application backend owns the runtime lock and persistence transaction.
 * Register before starting UI tasks; never replace it while those tasks run. */
typedef bool (*ui_automation_load_cb_t)(automation_config_t *out, void *ctx);
typedef bool (*ui_automation_save_cb_t)(automation_config_t *out, uint8_t index,
                                      const ui_automation_state_t *slot, ui_automation_edit_t edit, void *ctx);
void ui_model_set_automation_backend(ui_automation_load_cb_t load, ui_automation_save_cb_t save, void *ctx);
bool ui_automation_apply_edit(automation_config_t *config, uint8_t index,
                              const ui_automation_state_t *slot, ui_automation_edit_t edit);
/* Apply only the selected field to the current runtime configuration. */
bool ui_runtime_save_automation(ui_runtime_t *ui, uint8_t automation_index, ui_automation_edit_t edit);
