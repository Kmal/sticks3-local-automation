#include "rule_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(value) do { if (!(value)) { fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #value); exit(1); } } while (0)
#define ASSERT_EQ(expected, actual) do { if ((expected) != (actual)) { fprintf(stderr, "%s:%d expected %d got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); exit(1); } } while (0)

static automation_config_t engine_config(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    config.rule_count = 1;
    config.rules[0].enabled = true;
    config.rules[0].id = 42;
    (void)snprintf(config.rules[0].name, RULE_NAME_MAX, "engine test");
    config.rules[0].when.source = RULE_SOURCE_SOUND_RMS_DBFS;
    config.rules[0].when.comparator = RULE_COMPARATOR_GTE;
    config.rules[0].when.threshold = rule_value_i32(10);
    config.rules[0].when.sustain_ms = 0;
    config.rules[0].action_count = 1;
    config.rules[0].actions[0].type = RULE_ACTION_LOCAL_UI;
    config.rules[0].cooldown_ms = 100;
    return config;
}

static trigger_fact_t fact(rule_source_t source, int32_t value, uint32_t uptime_ms)
{
    trigger_fact_t out;
    memset(&out, 0, sizeof(out));
    out.source = source;
    out.value = rule_value_i32(value);
    out.uptime_ms = uptime_ms;
    out.sequence = uptime_ms;
    return out;
}

static size_t process_i32(rule_engine_t *engine, rule_source_t source, int32_t value, uint32_t uptime_ms, rule_event_t *events, size_t max_events)
{
    trigger_fact_t current = fact(source, value, uptime_ms);
    return rule_engine_process_fact(engine, &current, events, max_events);
}

static void test_non_matching_fact_produces_no_events(void)
{
    automation_config_t config = engine_config();
    rule_engine_t engine;
    rule_event_t events[2];
    ASSERT_TRUE(rule_engine_init(&engine, &config));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_PEAK_DBFS, 99, 1, events, 2));
}

static void test_transition_fires_once_until_false(void)
{
    automation_config_t config = engine_config();
    rule_engine_t engine;
    rule_event_t events[2];
    ASSERT_TRUE(rule_engine_init(&engine, &config));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 10, 10, events, 2));
    ASSERT_EQ(42, events[0].rule_id);
    ASSERT_EQ(RULE_ACTION_LOCAL_UI, events[0].action);
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 11, 20, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 9, 30, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 90, events, 2));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 140, events, 2));
}

static void test_sustain_and_cooldown(void)
{
    automation_config_t config = engine_config();
    config.rules[0].when.sustain_ms = 50;
    config.rules[0].cooldown_ms = 200;
    rule_engine_t engine;
    rule_event_t events[2];
    ASSERT_TRUE(rule_engine_init(&engine, &config));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 100, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 149, events, 2));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 150, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 9, 160, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 200, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 260, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 9, 300, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 340, events, 2));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 400, events, 2));
}

static void test_small_event_buffer_does_not_drop_actions(void)
{
    automation_config_t config = engine_config();
    config.rules[0].action_count = 2;
    config.rules[0].actions[0].type = RULE_ACTION_LOCAL_UI;
    config.rules[0].actions[1].type = RULE_ACTION_LOCAL_UI;
    rule_engine_t engine;
    rule_event_t events[2];
    ASSERT_TRUE(rule_engine_init(&engine, &config));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 10, events, 1));
    ASSERT_EQ(2, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 11, events, 2));
    ASSERT_EQ(RULE_ACTION_LOCAL_UI, events[0].action);
    ASSERT_EQ(RULE_ACTION_LOCAL_UI, events[1].action);
}

static void test_cooldown_after_fire_at_zero(void)
{
    automation_config_t config = engine_config();
    config.rules[0].cooldown_ms = 100;
    rule_engine_t engine;
    rule_event_t events[2];
    ASSERT_TRUE(rule_engine_init(&engine, &config));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 0, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 9, 10, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 50, events, 2));
    ASSERT_EQ(0, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 9, 60, events, 2));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 100, events, 2));
}

static void test_full_buffer_still_updates_later_matching_rule_state(void)
{
    automation_config_t config = engine_config();
    config.rule_count = 2;
    config.rules[1] = config.rules[0];
    config.rules[1].id = 43;
    config.rules[0].when.comparator = RULE_COMPARATOR_LT;
    rule_engine_t engine;
    rule_event_t event;
    ASSERT_TRUE(rule_engine_init(&engine, &config));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 100, &event, 1));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 9, 200, &event, 1));
    ASSERT_EQ(1, process_i32(&engine, RULE_SOURCE_SOUND_RMS_DBFS, 12, 300, &event, 1));
    ASSERT_EQ(43, event.rule_id);
}

int main(void)
{
    test_full_buffer_still_updates_later_matching_rule_state();
    test_non_matching_fact_produces_no_events();
    test_transition_fires_once_until_false();
    test_sustain_and_cooldown();
    test_small_event_buffer_does_not_drop_actions();
    test_cooldown_after_fire_at_zero();
    puts("rule_engine tests passed");
    return 0;
}
