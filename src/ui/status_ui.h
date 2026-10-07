#ifndef STATUS_UI_H
#define STATUS_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ui_nav.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_UI_STATE_BOOTING = 0,
    STATUS_UI_STATE_NO_TRANSPORT,
    STATUS_UI_STATE_READY,
    STATUS_UI_STATE_ERROR,
} status_ui_state_t;

typedef struct {
    void (*key1_pressed)(void *ctx);
    void (*key2_pressed)(void *ctx);
    /* Called when the Web UI service flag changes so the owner can start or
     * stop heavyweight server resources outside the status UI module. */
    void (*service_enabled_changed)(bool enabled, void *ctx);
    void *ctx;
} status_ui_button_handlers_t;

typedef enum {
    STATUS_UI_INPUT_SELECT = 0,
    STATUS_UI_INPUT_NEXT,
    STATUS_UI_INPUT_PREV,
    STATUS_UI_INPUT_BACK,
} status_ui_input_t;

esp_err_t status_ui_init(const status_ui_button_handlers_t *handlers);
void status_ui_set_web_access_code(const char *code);
void status_ui_set_state(status_ui_state_t state);
status_ui_state_t status_ui_get_state(void);
const char *status_ui_state_name(status_ui_state_t state);
void status_ui_set_service_enabled(bool enabled);
bool status_ui_get_service_enabled(void);
bool status_ui_keyboard_read_line(const char *title, const char *initial, char *out, size_t out_len, size_t max_len, bool secret, uint32_t timeout_ms);
void status_ui_open_screen(ui_screen_id_t screen);
ui_screen_id_t status_ui_get_screen(void);
void status_ui_handle_input(status_ui_input_t input);

#ifdef __cplusplus
}
#endif

#endif // STATUS_UI_H
