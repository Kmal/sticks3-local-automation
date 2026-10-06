#pragma once

#include "action_dispatcher.h"
#include "audio_metrics.h"
#include "button_state.h"
#include "rule_engine.h"
#include "trigger_gpio.h"
#include "hardware_fact_service.h"
#include "trigger_sources.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    rule_engine_t engine;
    trigger_adapter_t trigger_adapter;
    action_dispatcher_t dispatcher;
    trigger_gpio_t gpio_triggers[RULE_MAX_RULES];
    size_t gpio_trigger_count;
    size_t last_event_count;
    size_t enqueue_errors;
    hardware_fact_service_t hardware_facts;
} rule_runtime_t;

bool rule_runtime_init(rule_runtime_t *runtime, const automation_config_t *config);
bool rule_runtime_replace_config(rule_runtime_t *runtime, const automation_config_t *config);
/* Prepare/validate before persistence; the engine changes only after commit succeeds. */
typedef bool (*rule_config_commit_cb_t)(const automation_config_t *config, void *ctx);
bool rule_runtime_replace_config_with_commit(rule_runtime_t *runtime, const automation_config_t *config,
                                            rule_config_commit_cb_t commit, void *ctx);
void rule_runtime_set_ble_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx);
void rule_runtime_set_http_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx);
void rule_runtime_set_ir_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx);
void rule_runtime_set_local_ui_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx);
void rule_runtime_set_speaker_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx);
size_t rule_runtime_process_fact(rule_runtime_t *runtime, const trigger_fact_t *fact);
size_t rule_runtime_tick(rule_runtime_t *runtime, uint32_t uptime_ms);
size_t rule_runtime_process_metrics(rule_runtime_t *runtime, const audio_level_metrics_t *metrics, uint32_t uptime_ms);
size_t rule_runtime_process_button_event(rule_runtime_t *runtime, button_state_event_t event, uint32_t uptime_ms);
size_t rule_runtime_poll_gpio(rule_runtime_t *runtime, uint32_t uptime_ms);
size_t rule_runtime_poll_hardware(rule_runtime_t *runtime, uint32_t uptime_ms);
size_t rule_runtime_process_actions(rule_runtime_t *runtime);
action_result_t rule_runtime_get_last_action_result(const rule_runtime_t *runtime);
