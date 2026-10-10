#!/usr/bin/env python3
"""Compile the real foreground input dispatcher with owner/menu spies."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/ui/status_ui.c').read_text()

def function(name, content=source):
    start = content.index(name)
    start = content.rfind('\n', 0, start) + 1
    opening = content.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (content[end] == '{') - (content[end] == '}')
        end += 1
    return content[start:end]

# The declarations precede the definitions; target the definition signatures.
effects_end = source.index('} status_ui_input_effects_t;') + len('} status_ui_input_effects_t;')
effects_start = source.rfind('typedef struct {', 0, effects_end)
effects = source[effects_start:effects_end]
apply = function('static void status_ui_apply_input_effects(const status_ui_input_effects_t *effects)\n{')
handle = function('void status_ui_handle_input(status_ui_input_t input)\n{')
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include "status_ui_input_map.h"
#define CONFIG_APP_STATUS_UI_LCD 1
#define portENTER_CRITICAL(mux) ((void)0)
#define portEXIT_CRITICAL(mux) ((void)0)
static struct { uint32_t web_auth_request_id; } s_ui;
static status_ui_button_handlers_t s_handlers;
static uint32_t s_web_auth_displayed_id;
static unsigned decisions, focused, menu_actions, scans, syncs;
static uint32_t decided_id;
static bool approved;
static void decide(uint32_t id, bool approve, void *ctx) { assert(ctx == &s_ui); decisions++; decided_id=id; approved=approve; }
static void status_ui_sync_web_auth(void) { syncs++; }
void status_ui_set_service_enabled(bool enabled) { assert(!enabled); menu_actions++; }
static bool status_ui_select_current_scan(void *ui, ui_flow_id_t flow) { scans++; return true; }
static void status_ui_activate_selected_item(void) { menu_actions++; }
'''
spies = r'''
static void status_ui_dispatch_focused_input(status_ui_input_t input, status_ui_input_effects_t *effects) { focused++; }
static bool status_ui_idle_consume_input_to_effects(status_ui_input_t input, status_ui_input_effects_t *effects) { assert(0); return false; }
'''
suffix = r'''
int main(void) {
    s_handlers.web_auth_decide=decide; s_handlers.ctx=&s_ui;
    s_ui.web_auth_request_id=42;
    status_ui_handle_input(STATUS_UI_INPUT_SELECT);
    assert(decisions==0 && focused==0); /* Pending but not yet rendered. */
    s_web_auth_displayed_id=42;
    status_ui_handle_input(STATUS_UI_INPUT_SELECT);
    assert(decisions==1 && decided_id==42 && approved && syncs==1);
    for (int input=STATUS_UI_INPUT_NEXT; input<=STATUS_UI_INPUT_BACK; ++input) {
        status_ui_handle_input((status_ui_input_t)input);
        assert(!approved && decided_id==42);
    }
    assert(decisions==4 && focused==0 && menu_actions==0 && scans==0);
    status_ui_handle_input((status_ui_input_t)99);
    assert(decisions==4 && focused==0);
    s_ui.web_auth_request_id=0; s_web_auth_displayed_id=0;
    status_ui_handle_input(STATUS_UI_INPUT_SELECT);
    assert(focused==1 && decisions==4);
    puts("Device approval prompt owns focus; KEY1 approves and KEY2 gestures reject");
}
'''
with tempfile.TemporaryDirectory() as directory:
    test = Path(directory) / 'input.c'
    exe = Path(directory) / 'input'
    test.write_text(prefix + effects + '\n' + spies + apply + '\n' + handle + '\n' + suffix)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function',
                    '-I'+str(ROOT/'src/ui'), '-I'+str(ROOT/'tests/host/fakes/esp_idf_stubs'),
                    str(test), str(ROOT/'src/ui/status_ui_input_map.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


# Exercise the real popup layout with the actual 135x240 board dimensions and
# firmware font renderer, including the longest possible request ID.
import re
render_source = (ROOT / 'src/ui/ui_render.c').read_text()
board = (ROOT / 'src/board/board_sticks3.h').read_text()
width = int(re.search(r'#define BOARD_LCD_H_RES\s+(\d+)', board).group(1))
height = int(re.search(r'#define BOARD_LCD_V_RES\s+(\d+)', board).group(1))
render_prefix = r"""
#include <assert.h>
#include <stdio.h>
#include "display_text.h"
#define UI_LINE_H 16
#define UI_COLOR_WARN 0xFFE0
#define UI_COLOR_OK 0x07E0
#define UI_COLOR_TEXT 0xFFFF
static display_text_context_t display;
static void fill(int x, int y, int w, int h, uint16_t color, void *ctx) {
    assert(x>=0 && y>=0 && x+w<=UI_LCD_W && y+h<=UI_LCD_H);
}
static void status_lcd_fill_rect(int x,int y,int w,int h,uint16_t color) { fill(x,y,w,h,color,NULL); }
static display_text_result_t ui_text_put_box(display_text_region_id_t region,int x,int y,int w,int h,
    const char *text,uint16_t color,display_text_fit_t fit,display_text_align_t align,display_text_priority_t priority) {
    display_text_box_t box={.region_id=region,.x=x,.y=y,.width=w,.height=h,.scale=1,.color=color,
        .fit=fit,.align=align,.priority=priority,.collision=DISPLAY_TEXT_COLLISION_OVERLAY};
    display_text_result_t result=display_text_put(&display,&box,text);
    assert(result.drawn && result.all_visible_now && result.all_reachable);
    return result;
}
"""
render_suffix = r"""
int main(void) {
    display_text_context_init(&display,UI_LCD_W,UI_LCD_H,fill,NULL);
    display_text_begin_frame(&display,0);
    ui_render_web_auth_popup(UINT32_MAX);
    display_text_end_frame(&display);
    puts("Authorization popup fits the StickS3 LCD with all instructions visible");
}
"""
with tempfile.TemporaryDirectory() as directory:
    test = Path(directory) / 'popup.c'
    exe = Path(directory) / 'popup'
    test.write_text(f'#define UI_LCD_W {width}\n#define UI_LCD_H {height}\n' + render_prefix +
                    function('static display_text_region_id_t ui_body_row_region', render_source) + '\n' +
                    function('void ui_render_web_auth_popup', render_source) + '\n' + render_suffix)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                    '-I'+str(ROOT/'src/ui'),str(test),str(ROOT/'src/ui/display_text.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
