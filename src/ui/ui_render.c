#include "ui_render.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_sticks3.h"
#include "app_time.h"
#include "display_text.h"
#include "status_lcd.h"
#include "status_ui.h"
#include "ui_nav.h"
#ifdef ESP_PLATFORM
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#endif

#define STATUS_UI_LCD_BG 0x0000
#define STATUS_UI_LCD_HEADER_BG 0x001F
#define STATUS_UI_LCD_TEXT 0xFFFF
#define STATUS_UI_LCD_DIM 0x8410
#define STATUS_UI_LCD_OK 0x07E0
#define STATUS_UI_LCD_WARN 0xFFE0
#define STATUS_UI_LCD_ERR 0xF800
#define UI_LCD_W BOARD_LCD_H_RES
#define UI_LCD_H BOARD_LCD_V_RES
#define UI_STATUS_BAR_H 18
#define UI_TITLE_Y 22
#define UI_BODY_Y 42
#define UI_LINE_H 16
#define UI_BOTTOM_HINT_H 14
#define UI_LEFT_PAD 4
#define UI_RIGHT_PAD 4
#define UI_MENU_MAX_VISIBLE_ROWS 9
#define UI_COLOR_BG STATUS_UI_LCD_BG
#define UI_COLOR_BAR STATUS_UI_LCD_HEADER_BG
#define UI_COLOR_TEXT STATUS_UI_LCD_TEXT
#define UI_COLOR_DIM STATUS_UI_LCD_DIM
#define UI_COLOR_OK STATUS_UI_LCD_OK
#define UI_COLOR_WARN STATUS_UI_LCD_WARN
#define UI_COLOR_ERR STATUS_UI_LCD_ERR

static void ui_render_clear(void)
{
    status_lcd_fill_rect(0, 0, UI_LCD_W, UI_LCD_H, UI_COLOR_BG);
}


static display_text_region_id_t ui_body_row_region(size_t row)
{
    switch (row) {
    case 0: return DISPLAY_TEXT_REGION_BODY_ROW_0;
    case 1: return DISPLAY_TEXT_REGION_BODY_ROW_1;
    case 2: return DISPLAY_TEXT_REGION_BODY_ROW_2;
    case 3: return DISPLAY_TEXT_REGION_BODY_ROW_3;
    case 4: return DISPLAY_TEXT_REGION_BODY_ROW_4;
    case 5: return DISPLAY_TEXT_REGION_BODY_ROW_5;
    case 6: return DISPLAY_TEXT_REGION_BODY_ROW_6;
    case 7: return DISPLAY_TEXT_REGION_BODY_ROW_7;
    default: return DISPLAY_TEXT_REGION_BODY_DYNAMIC;
    }
}

static display_text_result_t ui_text_put_box(
    display_text_region_id_t region_id,
    int x,
    int y,
    int w,
    int h,
    const char *text,
    uint16_t color,
    display_text_fit_t fit,
    display_text_align_t align,
    display_text_priority_t priority
)
{
    return status_lcd_text_put_box(region_id, x, y, w, h, text, color, fit, align, priority);
}

static display_text_result_t ui_text_put_line(
    display_text_region_id_t region_id,
    int x,
    int y,
    int w,
    const char *text,
    uint16_t color,
    display_text_fit_t fit
)
{
    return ui_text_put_box(region_id, x, y, w, UI_LINE_H, text, color, fit,
                           DISPLAY_TEXT_ALIGN_LEFT, DISPLAY_TEXT_PRIORITY_NORMAL);
}

static void ui_render_status_bar(const ui_status_bar_state_t *status)
{
    status_lcd_fill_rect(0, 0, UI_LCD_W, UI_STATUS_BAR_H, UI_COLOR_BAR);
    ui_text_put_box(DISPLAY_TEXT_REGION_STATUS_TIME, UI_LEFT_PAD, 5, 38, UI_STATUS_BAR_H - 5,
                    (status != NULL && status->time_valid) ? status->time_hhmm : "--:--",
                    UI_COLOR_TEXT, DISPLAY_TEXT_FIT_ONE_LINE, DISPLAY_TEXT_ALIGN_LEFT,
                    DISPLAY_TEXT_PRIORITY_HIGH);
    if (status != NULL && status->wifi_connected) {
        ui_text_put_box(DISPLAY_TEXT_REGION_STATUS_WIFI, 48, 5, 42, UI_STATUS_BAR_H - 5,
                        "Wi-Fi", UI_COLOR_TEXT, DISPLAY_TEXT_FIT_ONE_LINE,
                        DISPLAY_TEXT_ALIGN_LEFT, DISPLAY_TEXT_PRIORITY_NORMAL);
    }
    char battery[8];
    if (status != NULL && status->battery_valid) snprintf(battery, sizeof(battery), "%u%%", (unsigned)status->battery_percent);
    else snprintf(battery, sizeof(battery), "--%%");
    ui_text_put_box(DISPLAY_TEXT_REGION_STATUS_BATTERY, UI_LCD_W - 36, 5, 36, UI_STATUS_BAR_H - 5,
                    battery, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_ONE_LINE,
                    DISPLAY_TEXT_ALIGN_RIGHT, DISPLAY_TEXT_PRIORITY_HIGH);
}

static void ui_render_title(const char *title)
{
    ui_text_put_line(DISPLAY_TEXT_REGION_TITLE, UI_LEFT_PAD, UI_TITLE_Y,
                     UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, title != NULL ? title : "",
                     UI_COLOR_TEXT, DISPLAY_TEXT_FIT_MARQUEE);
}

static void ui_render_bottom_hints(const char *hints)
{
    ui_text_put_box(DISPLAY_TEXT_REGION_BOTTOM_HINT, UI_LEFT_PAD, UI_LCD_H - UI_BOTTOM_HINT_H,
                    UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, UI_BOTTOM_HINT_H,
                    hints, UI_COLOR_DIM, DISPLAY_TEXT_FIT_MARQUEE,
                    DISPLAY_TEXT_ALIGN_LEFT, DISPLAY_TEXT_PRIORITY_NORMAL);
}

static void ui_render_menu(const ui_runtime_t *ui, const ui_screen_def_t *screen)
{
    int y = UI_BODY_Y;
    size_t row = 0;
    size_t start = ui->nav.scroll_offset;
    for (size_t i = start; screen != NULL && i < screen->item_count && y < UI_LCD_H - UI_BOTTOM_HINT_H; ++i, ++row) {
        const bool selected = i == ui->nav.selected_index;
        char line[DISPLAY_TEXT_MAX_TEXT];
        snprintf(line, sizeof(line), "%c %s", selected ? '>' : ' ', screen->items[i].label);
        ui_text_put_line(ui_body_row_region(row), UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                         line, selected ? UI_COLOR_OK : UI_COLOR_TEXT,
                         selected ? DISPLAY_TEXT_FIT_MARQUEE : DISPLAY_TEXT_FIT_PAGE);
        y += UI_LINE_H;
    }
}

static void ui_render_wifi_scan_results(const ui_runtime_t *ui, const ui_wifi_flow_state_t *wifi)
{
    (void)ui;
    int y = UI_BODY_Y;
    if (wifi == NULL || wifi->scan_results.count == 0u) {
        ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_0, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                         wifi != NULL && wifi->last_error[0] ? wifi->last_error : "No networks",
                         UI_COLOR_WARN, DISPLAY_TEXT_FIT_MARQUEE);
        return;
    }
    size_t visible = wifi->scan_results.count < 7u ? wifi->scan_results.count : 7u;
    size_t first = wifi->selected_scan_index >= visible ? wifi->selected_scan_index - visible + 1u : 0u;
    for (size_t row = 0; row < visible && first + row < wifi->scan_results.count; ++row) {
        size_t i = first + row;
        char line[DISPLAY_TEXT_MAX_TEXT];
        snprintf(line, sizeof(line), "%c%s %ddBm ch%u", i == wifi->selected_scan_index ? '>' : ' ', wifi->scan_results.items[i].ssid[0] ? wifi->scan_results.items[i].ssid : "(hidden)", wifi->scan_results.items[i].rssi, (unsigned)wifi->scan_results.items[i].channel);
        ui_text_put_line(ui_body_row_region(row), UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                         line, i == wifi->selected_scan_index ? UI_COLOR_OK : UI_COLOR_TEXT,
                         i == wifi->selected_scan_index ? DISPLAY_TEXT_FIT_MARQUEE : DISPLAY_TEXT_FIT_PAGE);
        y += UI_LINE_H;
    }
}

static void ui_render_wifi_result(const ui_runtime_t *ui, const ui_wifi_flow_state_t *wifi)
{
    char line[DISPLAY_TEXT_MAX_TEXT];
    int y = UI_BODY_Y;
    snprintf(line, sizeof(line), "SSID: %s", wifi != NULL && wifi->ssid[0] ? wifi->ssid : "-");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_0, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_WRAP); y += UI_LINE_H;
    snprintf(line, sizeof(line), "%s", ui->web_access_code[0] ? ui->web_access_code : (wifi != NULL && wifi->last_error[0] ? wifi->last_error : "Connected and saved"));
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_1, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, wifi != NULL && wifi->web_url[0] ? UI_COLOR_OK : UI_COLOR_WARN, DISPLAY_TEXT_FIT_WRAP);
    y += UI_LINE_H;
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_2, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                     "URL:", UI_COLOR_OK, DISPLAY_TEXT_FIT_ONE_LINE);
    y += UI_LINE_H;
    snprintf(line, sizeof(line), "%s", wifi != NULL && wifi->web_url[0] ? wifi->web_url : "-");
    ui_text_put_box(DISPLAY_TEXT_REGION_BODY_ROW_3, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                    UI_LCD_H - UI_BOTTOM_HINT_H - y, line, UI_COLOR_OK, DISPLAY_TEXT_FIT_WRAP,
                    DISPLAY_TEXT_ALIGN_LEFT, DISPLAY_TEXT_PRIORITY_NORMAL);
}

static void ui_render_ap_url(const ui_runtime_t *ui)
{
    char line[DISPLAY_TEXT_MAX_TEXT];
    int y = UI_BODY_Y;
    snprintf(line, sizeof(line), "SSID: %s", ui->ap.ap_name[0] ? ui->ap.ap_name : "-");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_0, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_WRAP); y += UI_LINE_H;
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_1, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                     "URL:", UI_COLOR_OK, DISPLAY_TEXT_FIT_ONE_LINE);
    y += UI_LINE_H;
    snprintf(line, sizeof(line), "%s", ui->ap.url[0] ? ui->ap.url : "-");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_2, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_OK, DISPLAY_TEXT_FIT_MARQUEE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Channel: %u", (unsigned)ui->ap.channel);
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_3, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Code: %s", ui->web_access_code[0] ? ui->web_access_code : "unavailable");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_4, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, ui->ap.started ? UI_COLOR_OK : UI_COLOR_WARN, DISPLAY_TEXT_FIT_MARQUEE);
}

static void ui_render_bluetooth_status(const ui_runtime_t *ui)
{
    char line[DISPLAY_TEXT_MAX_TEXT];
    int y = UI_BODY_Y;
    snprintf(line, sizeof(line), "Type: BLE GATT only");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_0, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_WARN, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Rule events only");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_1, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_WARN, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Connected: %s", ui->bluetooth.ble_connected ? "yes" : "no");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_2, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Name: %s", ui->bluetooth.device_name);
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_3, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_MARQUEE);
    if (ui->bluetooth.status_text[0] != '\0') {
        y += UI_LINE_H;
        snprintf(line, sizeof(line), "%s", ui->bluetooth.status_text);
        ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_4, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_WRAP);
    }
}

static void ui_render_automation_detail(const ui_runtime_t *ui, const ui_screen_def_t *screen)
{
    uint8_t index = screen->id == UI_SCREEN_AUTOMATION_2 ? 1u : 0u;
    const ui_automation_state_t *slot = &ui->automations[index];
    char line[DISPLAY_TEXT_MAX_TEXT];
    int y = UI_BODY_Y;
    snprintf(line, sizeof(line), "Enabled: %s", slot->enabled ? "on" : "off");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_0, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Trigger: %s", slot->trigger_label);
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_1, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_WRAP); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Action: %s", slot->action_label);
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_2, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_WRAP); y += UI_LINE_H;
    for (size_t i = 0; screen != NULL && i < screen->item_count && y < UI_LCD_H - UI_BOTTOM_HINT_H; ++i) {
        bool selected = i == ui->nav.selected_index;
        snprintf(line, sizeof(line), "%c %s", selected ? '>' : ' ', screen->items[i].label);
        ui_text_put_line(ui_body_row_region(i + 3u), UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                         line, selected ? UI_COLOR_OK : UI_COLOR_TEXT,
                         selected ? DISPLAY_TEXT_FIT_MARQUEE : DISPLAY_TEXT_FIT_PAGE);
        y += UI_LINE_H;
    }
}

static const char *ui_settings_item_value(const ui_runtime_t *ui, const ui_menu_item_t *item)
{
    static char value[APP_TIME_TIMEZONE_MAX_LEN + 1u];
    if (item == NULL) return "";
    switch (item->action) {
    case UI_ACTION_SETTINGS_EDIT_TIMEZONE: {
        app_time_config_t config;
        if (app_time_get_config(&config)) {
            snprintf(value, sizeof(value), "%s", config.timezone);
            return value;
        }
        return "unknown";
    }
    default:
        break;
    }
    if (strcmp(item->label, "Wi-Fi Setup") == 0) {
        return (ui != NULL && ui->status_bar.wifi_connected) ? "connected" : "setup";
    }
    if (strcmp(item->label, "Power Status") == 0) {
        if (ui != NULL && ui->status_bar.battery_valid) {
            snprintf(value, sizeof(value), "%u%%", (unsigned)ui->status_bar.battery_percent);
            return value;
        }
        return "unknown";
    }
    if (strcmp(item->label, "Buttons") == 0) return "KEY1/KEY2";
    if (strcmp(item->label, "LCD") == 0) return status_lcd_is_ready() ? "ready" : "off";
    return "";
}

static void ui_render_menu_with_values(const ui_runtime_t *ui, const ui_screen_def_t *screen)
{
    int y = UI_BODY_Y;
    size_t row = 0;
    size_t start = ui->nav.scroll_offset;
    for (size_t i = start; screen != NULL && i < screen->item_count && y < UI_LCD_H - UI_BOTTOM_HINT_H; ++i, ++row) {
        const bool selected = i == ui->nav.selected_index;
        const char *value = ui_settings_item_value(ui, &screen->items[i]);
        char line[DISPLAY_TEXT_MAX_TEXT];
        if (value != NULL && value[0] != '\0') {
            snprintf(line, sizeof(line), "%c %s: %s", selected ? '>' : ' ', screen->items[i].label, value);
        } else {
            snprintf(line, sizeof(line), "%c %s", selected ? '>' : ' ', screen->items[i].label);
        }
        ui_text_put_line(ui_body_row_region(row), UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                         line, selected ? UI_COLOR_OK : UI_COLOR_TEXT,
                         selected ? DISPLAY_TEXT_FIT_MARQUEE : DISPLAY_TEXT_FIT_PAGE);
        y += UI_LINE_H;
    }
}

static void ui_render_device_status(const ui_runtime_t *ui)
{
    char line[DISPLAY_TEXT_MAX_TEXT];
    int y = UI_BODY_Y;
    snprintf(line, sizeof(line), "UI: %s", ui != NULL && ui->menu_active ? "menu" : "idle");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_0, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Wi-Fi: %s", ui != NULL && ui->status_bar.wifi_connected ? "connected" : "disconnected");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_1, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
#ifdef ESP_PLATFORM
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    snprintf(line, sizeof(line), "Uptime: %luh %02lum", (unsigned long)(uptime_s / 3600u), (unsigned long)((uptime_s / 60u) % 60u));
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_2, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Heap: %lu", (unsigned long)esp_get_free_heap_size());
#else
    snprintf(line, sizeof(line), "Uptime: host");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_2, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_ONE_LINE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Heap: n/a");
#endif
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_3, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_ONE_LINE);
}

static void ui_render_about(void)
{
    char line[DISPLAY_TEXT_MAX_TEXT];
    int y = UI_BODY_Y;
#ifdef ESP_PLATFORM
    const esp_app_desc_t *desc = esp_app_get_description();
    snprintf(line, sizeof(line), "%s", desc != NULL ? desc->project_name : "StickS3 Control");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_0, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_MARQUEE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Version: %s", desc != NULL ? desc->version : "unknown");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_1, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_MARQUEE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "IDF: %s", esp_get_idf_version());
#else
    snprintf(line, sizeof(line), "StickS3 Control");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_0, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_TEXT, DISPLAY_TEXT_FIT_MARQUEE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "Version: host");
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_1, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_MARQUEE); y += UI_LINE_H;
    snprintf(line, sizeof(line), "IDF: n/a");
#endif
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_2, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, line, UI_COLOR_DIM, DISPLAY_TEXT_FIT_MARQUEE); y += UI_LINE_H;
    ui_text_put_line(DISPLAY_TEXT_REGION_BODY_ROW_3, UI_LEFT_PAD, y, UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD,
                     "Local automation UI", UI_COLOR_DIM, DISPLAY_TEXT_FIT_WRAP);
}

static void ui_render_settings(const ui_runtime_t *ui, const ui_screen_def_t *screen)
{
    switch (screen != NULL ? screen->id : UI_SCREEN_COUNT) {
    case UI_SCREEN_SETTINGS_DISPLAY:
    case UI_SCREEN_SETTINGS_CONNECTIVITY:
    case UI_SCREEN_SETTINGS_HARDWARE:
        ui_render_menu_with_values(ui, screen);
        break;
    default:
        ui_render_menu(ui, screen);
        break;
    }
}

void ui_render_toast(const ui_toast_t *toast)
{
    if (toast == NULL || toast->kind == UI_TOAST_NONE || toast->text[0] == '\0') return;
    uint16_t color = toast->kind == UI_TOAST_ERROR ? UI_COLOR_ERR : (toast->kind == UI_TOAST_WARNING ? UI_COLOR_WARN : UI_COLOR_OK);
    status_lcd_fill_rect(0, UI_LCD_H - 34, UI_LCD_W, 20, 0x2104);
    ui_text_put_box(DISPLAY_TEXT_REGION_TOAST, UI_LEFT_PAD, UI_LCD_H - 31,
                    UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, 16, toast->text, color,
                    DISPLAY_TEXT_FIT_WRAP, DISPLAY_TEXT_ALIGN_LEFT, DISPLAY_TEXT_PRIORITY_OVERLAY);
}

void ui_render_screen(const ui_runtime_t *ui, const ui_screen_def_t *screen)
{
    ui_render_clear();
    ui_render_status_bar(&ui->status_bar);
    ui_render_title(screen != NULL ? screen->title : "Main");
    if (screen == NULL) return;
    switch (screen->id) {
    case UI_SCREEN_CONFIG_WIFI_SCAN:
        ui_render_wifi_scan_results(ui, &ui->config_wifi);
        break;
    case UI_SCREEN_CONNECT_WIFI_SCAN:
        ui_render_wifi_scan_results(ui, &ui->connect_wifi);
        break;
    case UI_SCREEN_CONFIG_WIFI_CONNECT_SAVE:
    case UI_SCREEN_CONFIG_WIFI_MANUAL_CONNECT_SAVE:
        ui_render_wifi_result(ui, &ui->config_wifi);
        break;
    case UI_SCREEN_CONNECT_WIFI_CONNECT_SAVE:
    case UI_SCREEN_CONNECT_WIFI_MANUAL_CONNECT_SAVE:
        ui_render_wifi_result(ui, &ui->connect_wifi);
        break;
    case UI_SCREEN_CONFIG_AP_SHOW_URL:
        ui_render_ap_url(ui);
        break;
    case UI_SCREEN_CONNECT_BLUETOOTH:
        ui_render_bluetooth_status(ui);
        break;
    case UI_SCREEN_AUTOMATION_1:
    case UI_SCREEN_AUTOMATION_2:
        ui_render_automation_detail(ui, screen);
        break;
    case UI_SCREEN_SETTINGS:
    case UI_SCREEN_SETTINGS_DEVICE:
    case UI_SCREEN_SETTINGS_AUTOMATION:
    case UI_SCREEN_SETTINGS_MAINTENANCE:
        ui_render_settings(ui, screen);
        break;
    case UI_SCREEN_SETTINGS_DISPLAY:
    case UI_SCREEN_SETTINGS_CONNECTIVITY:
    case UI_SCREEN_SETTINGS_HARDWARE:
        ui_render_settings(ui, screen);
        break;
    case UI_SCREEN_SETTINGS_DEVICE_STATUS:
        ui_render_device_status(ui);
        break;
    case UI_SCREEN_SETTINGS_ABOUT:
        ui_render_about();
        break;
    default:
        ui_render_menu(ui, screen);
        break;
    }
    ui_render_bottom_hints("K1 SEL K2 NEXT 2x PREV HOLD BACK");
}


void ui_render_keyboard_overlay(const ui_keyboard_state_t *kb)
{
    if (!ui_keyboard_is_active(kb)) return;
    int y0 = BOARD_LCD_V_RES - UI_KEYBOARD_OVERLAY_H;
    if (y0 < 0) y0 = 0;
    status_lcd_fill_rect(0, y0, BOARD_LCD_H_RES, UI_KEYBOARD_OVERLAY_H, 0x1082);
    ui_text_put_box(DISPLAY_TEXT_REGION_KEYBOARD_TITLE, UI_LEFT_PAD, y0 + 4,
                    UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, UI_LINE_H,
                    kb->title, STATUS_UI_LCD_TEXT, DISPLAY_TEXT_FIT_MARQUEE,
                    DISPLAY_TEXT_ALIGN_LEFT, DISPLAY_TEXT_PRIORITY_OVERLAY);
    char display[STATUS_UI_KEYBOARD_MAX_TEXT + 1];
    size_t len = strlen(kb->text);
    const size_t display_len = len < sizeof(display) ? len : sizeof(display) - 1u;
    const size_t pending_index = kb->has_pending_cycle && len > 0u ? len - 1u : SIZE_MAX;
    for (size_t i = 0; i < display_len; ++i) {
        display[i] = (kb->secret && i != pending_index) ? '*' : kb->text[i];
    }
    display[display_len] = '\0';
    ui_text_put_box(DISPLAY_TEXT_REGION_KEYBOARD_INPUT, UI_LEFT_PAD, y0 + 18,
                    UI_LCD_W - UI_LEFT_PAD - UI_RIGHT_PAD, UI_LINE_H,
                    display, STATUS_UI_LCD_OK, DISPLAY_TEXT_FIT_MARQUEE,
                    DISPLAY_TEXT_ALIGN_RIGHT, DISPLAY_TEXT_PRIORITY_OVERLAY);
    const int cols = 4;
    const int key_w = BOARD_LCD_H_RES / cols;
    for (uint8_t i = 0; i < UI_KEYBOARD_KEY_COUNT; ++i) {
        const bool selected = kb->selected_key == i;
        const int col = i % cols;
        const int row = i / cols;
        const int x = col * key_w;
        const int y = y0 + 34 + row * 18;
        const char *label = ui_keyboard_9key_defs[i].label;
        status_lcd_fill_rect(x + 1, y, key_w - 2, 17, selected ? STATUS_UI_LCD_OK : 0x2104);
        ui_text_put_box(DISPLAY_TEXT_REGION_KEYBOARD_KEY, x + 3, y + 5, key_w - 6, 12,
                        label, selected ? STATUS_UI_LCD_BG : STATUS_UI_LCD_TEXT,
                        DISPLAY_TEXT_FIT_ONE_LINE, DISPLAY_TEXT_ALIGN_LEFT,
                        DISPLAY_TEXT_PRIORITY_OVERLAY);
    }
}

