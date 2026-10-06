#pragma once

#include "rule_types.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sequence;
    uint32_t uptime_ms;
    uint32_t rule_id;
    rule_source_t source;
    rule_action_type_t action;
    rule_action_t action_config;
    rule_value_t measured_value;
    uint32_t fire_count;
    char rule_name[RULE_NAME_MAX];
} rule_event_t;

/* One rule's actions are admitted together, including under backpressure. */
typedef struct {
    size_t event_count;
    rule_event_t events[RULE_MAX_ACTIONS_PER_RULE];
} rule_event_batch_t;

typedef bool (*rule_event_batch_sink_t)(const rule_event_batch_t *batch, void *ctx);

typedef struct {
    automation_config_t config;
    struct {
        bool condition_true;
        bool fired_while_true;
        uint32_t satisfied_since_ms;
        bool has_last_fire;
        uint32_t last_fire_ms;
        uint32_t fire_count;
    } state[RULE_MAX_RULES];
    uint32_t next_event_sequence;
} rule_engine_t;

bool rule_engine_init(rule_engine_t *engine, const automation_config_t *config);
bool rule_engine_replace_config(rule_engine_t *engine, const automation_config_t *config);
size_t rule_engine_process_fact(rule_engine_t *engine, const trigger_fact_t *fact, rule_event_t *events, size_t max_events);
size_t rule_engine_process_fact_with_sink(rule_engine_t *engine, const trigger_fact_t *fact,
                                         rule_event_batch_sink_t sink, void *ctx);
const automation_rule_t *rule_engine_get_rule_by_id(const rule_engine_t *engine, uint32_t rule_id);
