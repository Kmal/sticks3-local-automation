#include "rule_runtime.h"

#include <stdlib.h>
#include <string.h>


static bool runtime_build_gpio_triggers(const automation_config_t *config, trigger_gpio_t *out, size_t *out_count)
{
    if (config == NULL || out == NULL || out_count == NULL) {
        return false;
    }
    size_t count = 0;
    for (size_t i = 0; i < config->rule_count; ++i) {
        const automation_rule_t *rule = &config->rules[i];
        if (!rule->enabled || !rule_source_is_gpio(rule->when.source)) {
            continue;
        }
        bool duplicate = false;
        for (size_t j = 0; j < count; ++j) {
            duplicate |= out[j].source == rule->when.source && out[j].config.pin == rule->when.gpio.pin &&
                         out[j].config.profile == rule->when.gpio.profile &&
                         out[j].config.active_low == rule->when.gpio.active_low &&
                         out[j].config.debounce_ms == rule->when.gpio.debounce_ms;
        }
        if (duplicate) continue;
        if (count >= RULE_MAX_RULES || !trigger_gpio_init(&out[count], rule->when.source, &rule->when.gpio)) {
            for (size_t j = 0; j < count; ++j) trigger_gpio_deinit(&out[j]);
            return false;
        }
        ++count;
    }
    *out_count = count;
    return true;
}

static bool runtime_apply_gpio_triggers(rule_runtime_t *runtime, const automation_config_t *config)
{
    trigger_gpio_t triggers[RULE_MAX_RULES];
    size_t count = 0;
    if (!runtime_build_gpio_triggers(config, triggers, &count)) {
        return false;
    }
    memset(runtime->gpio_triggers, 0, sizeof(runtime->gpio_triggers));
    if (count > 0) {
        memcpy(runtime->gpio_triggers, triggers, count * sizeof(triggers[0]));
    }
    runtime->gpio_trigger_count = count;
    return true;
}

static bool runtime_fact_sink(const trigger_fact_t *fact, void *user_ctx)
{
    rule_runtime_t *runtime = (rule_runtime_t *)user_ctx;
    if (runtime == NULL || fact == NULL) {
        return false;
    }
    (void)rule_runtime_process_fact(runtime, fact);
    return true;
}

bool rule_runtime_init(rule_runtime_t *runtime, const automation_config_t *config)
{
    if (runtime == NULL) {
        return false;
    }
    memset(runtime, 0, sizeof(*runtime));
    automation_config_t *defaults = NULL;
    if (config == NULL) {
        defaults = malloc(sizeof(*defaults));
        if (defaults == NULL) {
            return false;
        }
        automation_config_set_defaults(defaults);
        config = defaults;
    }
    if (!rule_engine_init(&runtime->engine, config)) {
        free(defaults);
        return false;
    }
    if (!action_dispatcher_start(&runtime->dispatcher)) {
        free(defaults);
        return false;
    }
    trigger_adapter_init(&runtime->trigger_adapter, runtime_fact_sink, runtime);
    hardware_fact_service_config_t hw_cfg;
    hardware_fact_service_config_for_rules(&hw_cfg, config);
    (void)hardware_fact_service_init(&runtime->hardware_facts, &runtime->trigger_adapter, &hw_cfg);
    if (!runtime_apply_gpio_triggers(runtime, config)) {
        action_dispatcher_stop(&runtime->dispatcher);
        hardware_fact_service_deinit(&runtime->hardware_facts);
        free(defaults);
        return false;
    }
    free(defaults);
    return true;
}

bool rule_runtime_replace_config_with_commit(rule_runtime_t *runtime, const automation_config_t *config,
                                            rule_config_commit_cb_t commit, void *ctx)
{
    if (runtime == NULL || config == NULL || !automation_config_validate(config, NULL, 0)) {
        return false;
    }
    hardware_fact_service_config_t hw_cfg;
    hardware_fact_service_config_for_rules(&hw_cfg, config);
    const hardware_fact_service_config_t old = runtime->hardware_facts.config;
    const bool hardware_changed = old.enable_battery != hw_cfg.enable_battery || old.enable_usb_power != hw_cfg.enable_usb_power ||
        old.enable_bmi270 != hw_cfg.enable_bmi270 || old.enable_adc != hw_cfg.enable_adc || old.adc_gpio_mask != hw_cfg.adc_gpio_mask;
    /* Release ADC pin ownership before setting up newly selected GPIO inputs. */
    if (hardware_changed) hardware_fact_service_deinit(&runtime->hardware_facts);
    for (size_t i = 0; i < runtime->gpio_trigger_count; ++i) trigger_gpio_deinit(&runtime->gpio_triggers[i]);
    trigger_gpio_t triggers[RULE_MAX_RULES];
    size_t count = 0;
    const bool prepared = runtime_build_gpio_triggers(config, triggers, &count);
    if (!prepared || (commit != NULL && !commit(config, ctx))) {
        for (size_t i = 0; i < count; ++i) trigger_gpio_deinit(&triggers[i]);
        if (hardware_changed) (void)hardware_fact_service_init(&runtime->hardware_facts, &runtime->trigger_adapter, &old);
        (void)runtime_apply_gpio_triggers(runtime, &runtime->engine.config);
        return false;
    }
    if (hardware_changed) {
        (void)hardware_fact_service_init(&runtime->hardware_facts, &runtime->trigger_adapter, &hw_cfg);
    }
    if (!rule_engine_replace_config(&runtime->engine, config)) return false;
    memset(runtime->gpio_triggers, 0, sizeof(runtime->gpio_triggers));
    if (count > 0) {
        memcpy(runtime->gpio_triggers, triggers, count * sizeof(triggers[0]));
    }
    runtime->gpio_trigger_count = count;
    return true;
}

void rule_runtime_set_ble_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (runtime == NULL) {
        return;
    }
    action_dispatcher_set_ble_sender(&runtime->dispatcher, cb, ctx);
}

void rule_runtime_set_http_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (runtime == NULL) {
        return;
    }
    action_dispatcher_set_http_sender(&runtime->dispatcher, cb, ctx);
}

void rule_runtime_set_ir_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (runtime == NULL) {
        return;
    }
    action_dispatcher_set_ir_sender(&runtime->dispatcher, cb, ctx);
}

void rule_runtime_set_local_ui_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (runtime == NULL) {
        return;
    }
    action_dispatcher_set_local_ui_sender(&runtime->dispatcher, cb, ctx);
}

void rule_runtime_set_speaker_sender(rule_runtime_t *runtime, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (runtime == NULL) {
        return;
    }
    action_dispatcher_set_speaker_sender(&runtime->dispatcher, cb, ctx);
}

static bool runtime_action_sink(const rule_event_batch_t *batch, void *ctx)
{
    rule_runtime_t *runtime = ctx;
    if (action_enqueue_batch(&runtime->dispatcher, batch)) return true;
    runtime->enqueue_errors += batch->event_count;
    return false;
}

size_t rule_runtime_process_fact(rule_runtime_t *runtime, const trigger_fact_t *fact)
{
    if (runtime == NULL || fact == NULL) return 0;
    runtime->last_event_count = rule_engine_process_fact_to_sink(&runtime->engine, fact, runtime_action_sink, runtime);
    return runtime->last_event_count;
}

size_t rule_runtime_tick(rule_runtime_t *runtime, uint32_t uptime_ms)
{
    if (runtime == NULL) return 0;
    const size_t count = rule_engine_tick_to_sink(&runtime->engine, uptime_ms, runtime_action_sink, runtime);
#ifndef ESP_PLATFORM
    (void)action_dispatcher_process_all(&runtime->dispatcher);
#endif
    return count;
}

size_t rule_runtime_process_metrics(rule_runtime_t *runtime, const audio_level_metrics_t *metrics, uint32_t uptime_ms)
{
    if (runtime == NULL) {
        return 0;
    }
    const size_t emitted = trigger_emit_sound_facts(&runtime->trigger_adapter, metrics, uptime_ms);
#ifndef ESP_PLATFORM
    (void)action_dispatcher_process_all(&runtime->dispatcher);
#endif
    return emitted;
}

size_t rule_runtime_process_button_event(rule_runtime_t *runtime, button_state_event_t event, uint32_t uptime_ms)
{
    if (runtime == NULL) {
        return 0;
    }
    const size_t emitted = trigger_emit_button_event(&runtime->trigger_adapter, event, uptime_ms);
#ifndef ESP_PLATFORM
    (void)action_dispatcher_process_all(&runtime->dispatcher);
#endif
    return emitted;
}

size_t rule_runtime_poll_gpio(rule_runtime_t *runtime, uint32_t uptime_ms)
{
    if (runtime == NULL) {
        return 0;
    }
    size_t emitted = 0;
    for (size_t i = 0; i < runtime->gpio_trigger_count; ++i) {
        emitted += trigger_gpio_poll(&runtime->gpio_triggers[i], &runtime->trigger_adapter, uptime_ms);
    }
#ifndef ESP_PLATFORM
    (void)action_dispatcher_process_all(&runtime->dispatcher);
#endif
    return emitted;
}


size_t rule_runtime_poll_hardware(rule_runtime_t *runtime, uint32_t uptime_ms)
{
    if (runtime == NULL) {
        return 0;
    }
    size_t emitted = hardware_fact_service_poll(&runtime->hardware_facts, uptime_ms);
#ifndef ESP_PLATFORM
    (void)action_dispatcher_process_all(&runtime->dispatcher);
#endif
    return emitted;
}

size_t rule_runtime_process_actions(rule_runtime_t *runtime)
{
    if (runtime == NULL) {
        return 0;
    }
    return action_dispatcher_process_all(&runtime->dispatcher);
}

action_result_t rule_runtime_get_last_action_result(const rule_runtime_t *runtime)
{
    if (runtime == NULL) {
        return action_dispatcher_get_last_result(NULL);
    }
    return action_dispatcher_get_last_result(&runtime->dispatcher);
}

bool rule_runtime_replace_config(rule_runtime_t *runtime, const automation_config_t *config)
{
    return rule_runtime_replace_config_with_commit(runtime, config, NULL, NULL);
}
