#include "rule_engine.h"

#include <stdlib.h>
#include <string.h>

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

size_t rule_engine_process_fact_with_sink(rule_engine_t *engine, const trigger_fact_t *fact,
                                         rule_event_batch_sink_t sink, void *ctx)
{
    if (engine == NULL || fact == NULL || sink == NULL) {
        return 0;
    }

    size_t event_count = 0;
    for (size_t i = 0; i < engine->config.rule_count; ++i) {
        const automation_rule_t *rule = &engine->config.rules[i];
        if (!rule->enabled || rule->when.source != fact->source || !source_key_matches(rule->when.source_key, fact->source_key)) {
            continue;
        }

        /* Button facts are discrete events, not a latched level. Rearm each
         * event while retaining the successful-fire timestamp for cooldown. */
        if (fact->source == RULE_SOURCE_KEY1_SHORT || fact->source == RULE_SOURCE_KEY2_SHORT) {
            engine->state[i].condition_true = false;
            engine->state[i].fired_while_true = false;
        }
        const bool now_true = values_compare(fact->value, rule->when.comparator, rule->when.threshold);
        if (!now_true) {
            engine->state[i].condition_true = false;
            engine->state[i].fired_while_true = false;
            engine->state[i].satisfied_since_ms = 0;
            continue;
        }

        if (!engine->state[i].condition_true) {
            engine->state[i].condition_true = true;
            engine->state[i].fired_while_true = false;
            engine->state[i].satisfied_since_ms = fact->uptime_ms;
        }

        if ((uint32_t)(fact->uptime_ms - engine->state[i].satisfied_since_ms) < rule->when.sustain_ms) {
            continue;
        }
        if (engine->state[i].fired_while_true) {
            continue;
        }
        if (engine->state[i].has_last_fire &&
            (uint32_t)(fact->uptime_ms - engine->state[i].last_fire_ms) < rule->cooldown_ms) {
            continue;
        }
        rule_event_batch_t batch = {.event_count = rule->action_count};
        for (size_t action_index = 0; action_index < rule->action_count; ++action_index) {
            rule_event_t *event = &batch.events[action_index];
            event->sequence = engine->next_event_sequence + (uint32_t)action_index;
            event->uptime_ms = fact->uptime_ms;
            event->rule_id = rule->id;
            event->source = fact->source;
            event->action = rule->actions[action_index].type;
            event->action_config = rule->actions[action_index];
            event->measured_value = fact->value;
            event->fire_count = engine->state[i].fire_count + 1u;
            (void)strncpy(event->rule_name, rule->name, sizeof(event->rule_name) - 1);
        }
        if (!sink(&batch, ctx)) {
            continue;
        }
        engine->state[i].fired_while_true = true;
        engine->state[i].has_last_fire = true;
        engine->state[i].last_fire_ms = fact->uptime_ms;
        engine->state[i].fire_count++;
        engine->next_event_sequence += (uint32_t)batch.event_count;
        event_count += batch.event_count;
    }
    return event_count;
}

typedef struct {
    rule_event_t *events;
    size_t capacity;
    size_t count;
} event_buffer_t;

static bool buffer_event_batch(const rule_event_batch_t *batch, void *ctx)
{
    event_buffer_t *buffer = ctx;
    if (batch->event_count > buffer->capacity - buffer->count) {
        return false;
    }
    memcpy(&buffer->events[buffer->count], batch->events, batch->event_count * sizeof(batch->events[0]));
    buffer->count += batch->event_count;
    return true;
}

size_t rule_engine_process_fact(rule_engine_t *engine, const trigger_fact_t *fact, rule_event_t *events, size_t max_events)
{
    if (events == NULL || max_events == 0) {
        return 0;
    }
    event_buffer_t buffer = {.events = events, .capacity = max_events};
    return rule_engine_process_fact_with_sink(engine, fact, buffer_event_batch, &buffer);
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
