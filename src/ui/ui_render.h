#ifndef UI_RENDER_H
#define UI_RENDER_H

#include "ui_keyboard.h"
#include "ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_render_screen(const ui_runtime_t *ui, const ui_screen_def_t *screen);
void ui_render_web_auth_popup(uint32_t request_id);
void ui_render_toast(const ui_toast_t *toast);
void ui_render_keyboard_overlay(const ui_keyboard_state_t *kb);

#ifdef __cplusplus
}
#endif

#endif // UI_RENDER_H
