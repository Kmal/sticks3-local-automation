#include "rule_engine.h"

#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"

#ifndef CONFIG_APP_SOUND_LEVEL_WINDOW_MS
#define CONFIG_APP_SOUND_LEVEL_WINDOW_MS 100
#endif
#ifndef CONFIG_APP_HARDWARE_FACT_POLL_INTERVAL_MS
#define CONFIG_APP_HARDWARE_FACT_POLL_INTERVAL_MS 1000
#endif

/* Sampled observations expire after three producer intervals. State/edge
 * sources retain their existing transition semantics. Sustain on samples is
 * evaluated only by a new measurement, never by elapsed time alone. */
static uint32_t sample_max_gap_ms(rule_source_t source)
{
    if (rule_source_is_sound(source)) return 3u * CONFIG_APP_SOUND_LEVEL_WINDOW_MS;
    if (source == RULE_SOURCE_BATTERY_PERCENT || source == RULE_SOURCE_POWER_USB_PRESENT ||
        source == RULE_SOURCE_BMI270_MOTION || source == RULE_SOURCE_ADC_VOLTAGE_MV)
        return 3u * CONFIG_APP_HARDWARE_FACT_POLL_INTERVAL_MS;
    return 0;
}

static void invalidate_condition(rule_engine_t *engine, size_t i)
{
    engine->state[i].condition_true = false;
    engine->state[i].fired_while_true = false;
    engine->state[i].pending_pulse = false;
    engine->state[i].has_last_fact = false;
}

void rule_engine_invalidate_source(rule_engine_t *engine, rule_source_t source)
{
    if (engine == NULL) return;
    for (size_t i = 0; i < engine->config.rule_count; ++i)
        if (engine->config.rules[i].when.source == source) invalidate_condition(engine, i);
}

static bool source_key_matches(const char *rule_key, const char *fact_key)
{
    return rule_key[0] == '\0' || strncmp(rule_key, fact_key, RULE_SOURCE_KEY_MAX) == 0;
}

static bool values_compare(rule_value_t measured, rule_comparator_t comparator, rule_value_t threshold)
{
    if (measured.kind != threshold.kind) {
        return false;
    }
    if (measured.kind == RULE_VALUE_BOOL) {
        const bool left = measured.as.bool_value;
        const bool right = threshold.as.bool_value;
        switch (comparator) {
        case RULE_COMPARATOR_EQ:
            return left == right;
        case RULE_COMPARATOR_NE:
            return left != right;
        default:
            return false;
        }
    }
    if (measured.kind == RULE_VALUE_I32) {
        const int32_t left = measured.as.i32_value;
        const int32_t right = threshold.as.i32_value;
        switch (comparator) {
        case RULE_COMPARATOR_EQ:
            return left == right;
        case RULE_COMPARATOR_NE:
            return left != right;
        case RULE_COMPARATOR_GT:
            return left > right;
        case RULE_COMPARATOR_GTE:
            return left >= right;
        case RULE_COMPARATOR_LT:
            return left < right;
        case RULE_COMPARATOR_LTE:
            return left <= right;
        default:
            return false;
        }
    }
    return false;
}

static void reset_runtime_state(rule_engine_t *engine)
{
    memset(engine->state, 0, sizeof(engine->state));
    engine->next_event_sequence = 1;
}

bool rule_engine_init(rule_engine_t *engine, const automation_config_t *config)
{
    if (engine == NULL) {
        return false;
    }
    automation_config_t *defaults = NULL;
    if (config == NULL) {
        defaults = malloc(sizeof(*defaults));
        if (defaults == NULL) {
            return false;
        }
        automation_config_set_defaults(defaults);
        config = defaults;
    }
    if (!automation_config_validate(config, NULL, 0)) {
        free(defaults);
        return false;
    }
    memset(engine, 0, sizeof(*engine));
    engine->config = *config;
    free(defaults);
    reset_runtime_state(engine);
    return true;
}

bool rule_engine_replace_config(rule_engine_t *engine, const automation_config_t *config)
{
    if (engine == NULL || config == NULL || !automation_config_validate(config, NULL, 0)) {
        return false;
    }
    engine->config = *config;
    reset_runtime_state(engine);
    return true;
}

static bool source_is_pulse(rule_source_t source)
{
    return source == RULE_SOURCE_KEY1_SHORT || source == RULE_SOURCE_KEY2_SHORT || source == RULE_SOURCE_GPIO_EDGE;
}

static size_t evaluate_rule(rule_engine_t *engine, size_t i, uint32_t now,
                            rule_event_batch_sink_t sink, void *ctx)
{
    const automation_rule_t *rule = &engine->config.rules[i];
    const bool pulse = source_is_pulse(rule->when.source);
    if (!rule->enabled || (!engine->state[i].condition_true && !engine->state[i].pending_pulse) ||
        engine->state[i].fired_while_true ||
        (uint32_t)(now - engine->state[i].satisfied_since_ms) < rule->when.sustain_ms ||
        (engine->state[i].has_last_fire && (uint32_t)(now - engine->state[i].last_fire_ms) < rule->cooldown_ms)) return 0;

    rule_event_batch_t batch = {.event_count = rule->action_count};
    for (size_t j = 0; j < rule->action_count; ++j) {
        rule_event_t *event = &batch.events[j];
        memset(event, 0, sizeof(*event));
        event->sequence = engine->next_event_sequence + (uint32_t)j;
        event->uptime_ms = now;
        event->rule_id = rule->id;
        event->source = rule->when.source;
        event->action = rule->actions[j].type;
        event->action_config = rule->actions[j];
        event->measured_value = engine->state[i].last_value;
        event->fire_count = engine->state[i].fire_count + 1;
        (void)strncpy(event->rule_name, rule->name, sizeof(event->rule_name) - 1);
    }
    if (!sink(&batch, ctx)) {
        if (pulse) invalidate_condition(engine, i);
        return 0;
    }
    engine->next_event_sequence += (uint32_t)rule->action_count;
    engine->state[i].fired_while_true = !pulse;
    engine->state[i].condition_true = !pulse;
    engine->state[i].pending_pulse = false;
    engine->state[i].has_last_fire = true;
    engine->state[i].last_fire_ms = now;
    engine->state[i].fire_count++;
    return rule->action_count;
}

size_t rule_engine_process_fact_to_sink(rule_engine_t *engine, const trigger_fact_t *fact,
                                      rule_event_batch_sink_t sink, void *ctx)
{
    if (engine == NULL || fact == NULL || sink == NULL) return 0;
    size_t count = 0;
    for (size_t i = 0; i < engine->config.rule_count; ++i) {
        const automation_rule_t *rule = &engine->config.rules[i];
        if (!rule->enabled || rule->when.source != fact->source || !source_key_matches(rule->when.source_key, fact->source_key)) continue;
        const uint32_t max_gap = sample_max_gap_ms(fact->source);
        if (max_gap != 0 && engine->state[i].has_last_fact &&
            (uint32_t)(fact->uptime_ms - engine->state[i].last_fact_ms) > max_gap)
            invalidate_condition(engine, i);
        engine->state[i].last_fact_ms = fact->uptime_ms;
        engine->state[i].has_last_fact = true;
        const bool pulse = source_is_pulse(fact->source);
        const bool now_true = values_compare(fact->value, rule->when.comparator, rule->when.threshold);
        if (!now_true) {
            if (pulse) continue;
            engine->state[i].condition_true = false;
            engine->state[i].fired_while_true = false;
            engine->state[i].pending_pulse = false;
            continue;
        }
        if (pulse && engine->state[i].has_last_fire &&
            (uint32_t)(fact->uptime_ms - engine->state[i].last_fire_ms) < rule->cooldown_ms) continue;
        if (pulse || !engine->state[i].condition_true) {
            engine->state[i].satisfied_since_ms = fact->uptime_ms;
            engine->state[i].fired_while_true = false;
        }
        engine->state[i].condition_true = true;
        engine->state[i].pending_pulse = pulse;
        engine->state[i].last_value = fact->value;
        count += evaluate_rule(engine, i, fact->uptime_ms, sink, ctx);
    }
    return count;
}

size_t rule_engine_tick_to_sink(rule_engine_t *engine, uint32_t uptime_ms, rule_event_batch_sink_t sink, void *ctx)
{
    if (engine == NULL || sink == NULL) return 0;
    size_t count = 0;
    for (size_t i = 0; i < engine->config.rule_count; ++i) {
        const uint32_t max_gap = sample_max_gap_ms(engine->config.rules[i].when.source);
        if (max_gap != 0) {
            if (engine->state[i].has_last_fact &&
                (uint32_t)(uptime_ms - engine->state[i].last_fact_ms) > max_gap)
                invalidate_condition(engine, i);
            continue;
        }
        count += evaluate_rule(engine, i, uptime_ms, sink, ctx);
    }
    return count;
}

typedef struct { rule_event_t *events; size_t capacity; size_t used; } event_buffer_t;
static bool append_event_batch(const rule_event_batch_t *batch, void *ctx)
{
    event_buffer_t *buffer = ctx;
    const size_t count = batch->event_count;
    if (count > buffer->capacity - buffer->used) return false;
    memcpy(buffer->events + buffer->used, batch->events, count * sizeof(batch->events[0]));
    buffer->used += count;
    return true;
}

size_t rule_engine_process_fact(rule_engine_t *engine, const trigger_fact_t *fact, rule_event_t *events, size_t max_events)
{
    if (events == NULL || max_events == 0) return 0;
    event_buffer_t buffer = {.events = events, .capacity = max_events};
    return rule_engine_process_fact_to_sink(engine, fact, append_event_batch, &buffer);
}

const automation_rule_t *rule_engine_get_rule_by_id(const rule_engine_t *engine, uint32_t rule_id)
{
    if (engine == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < engine->config.rule_count; ++i) {
        if (engine->config.rules[i].id == rule_id) {
            return &engine->config.rules[i];
        }
    }
    return NULL;
}

size_t rule_engine_process_fact_with_sink(rule_engine_t *engine, const trigger_fact_t *fact, rule_event_batch_sink_t sink, void *ctx)
{
    return rule_engine_process_fact_to_sink(engine, fact, sink, ctx);
}
