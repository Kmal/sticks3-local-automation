#include "rule_runtime.h"
#include "board_sticks3.h"
#include "m5pm1.h"
#include "fake_register_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(value) do { if (!(value)) { fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #value); exit(1); } } while (0)
#define ASSERT_EQ(expected, actual) do { if ((expected) != (actual)) { fprintf(stderr, "%s:%d expected %d got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); exit(1); } } while (0)

static automation_config_t runtime_config(void)
{
    automation_config_t config;
    automation_config_set_defaults(&config);
    config.rule_count = 1;
    config.rules[0].enabled = true;
    config.rules[0].id = 7;
    (void)snprintf(config.rules[0].name, RULE_NAME_MAX, "runtime button");
    config.rules[0].when.source = RULE_SOURCE_KEY1_SHORT;
    config.rules[0].when.comparator = RULE_COMPARATOR_EQ;
    config.rules[0].when.threshold = rule_value_bool(true);
    config.rules[0].when.sustain_ms = 0;
    config.rules[0].cooldown_ms = 100;
    config.rules[0].action_count = 1;
    config.rules[0].actions[0].type = RULE_ACTION_LOCAL_UI;
    return config;
}


static automation_config_t gpio_runtime_config(void)
{
    automation_config_t config = runtime_config();
    (void)snprintf(config.rules[0].name, RULE_NAME_MAX, "runtime gpio");
    config.rules[0].when.source = RULE_SOURCE_GPIO_DIGITAL;
    (void)snprintf(config.rules[0].when.source_key, RULE_SOURCE_KEY_MAX, "gpio.digital.4");
    config.rules[0].when.gpio.pin = 4;
    config.rules[0].when.gpio.profile = RULE_GPIO_PROFILE_DIGITAL_HIGH_LOW;
    config.rules[0].when.gpio.debounce_ms = 10;
    return config;
}

static void test_runtime_gpio_poll_to_action_result(void)
{
    automation_config_t config = gpio_runtime_config();
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_EQ(1, runtime.gpio_trigger_count);
    trigger_gpio_set_host_level(&runtime.gpio_triggers[0], false);
    ASSERT_EQ(0, rule_runtime_poll_gpio(&runtime, 100));
    trigger_gpio_set_host_level(&runtime.gpio_triggers[0], true);
    ASSERT_EQ(0, rule_runtime_poll_gpio(&runtime, 105));
    ASSERT_EQ(1, rule_runtime_poll_gpio(&runtime, 116));
    action_result_t result = rule_runtime_get_last_action_result(&runtime);
    ASSERT_EQ(ACTION_RESULT_OK, result.code);
    ASSERT_EQ(7, result.rule_id);
}


static void test_runtime_ble_connected_fact_to_action_result(void)
{
    automation_config_t config = runtime_config();
    (void)snprintf(config.rules[0].name, RULE_NAME_MAX, "runtime ble");
    config.rules[0].when.source = RULE_SOURCE_BLE_CONNECTED;
    config.rules[0].when.threshold = rule_value_bool(true);
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    trigger_fact_t fact = {
        .source = RULE_SOURCE_BLE_CONNECTED,
        .value = {.kind = RULE_VALUE_BOOL, .as.bool_value = true},
        .uptime_ms = 10,
    };
    ASSERT_EQ(1, rule_runtime_process_fact(&runtime, &fact));
    ASSERT_EQ(1, rule_runtime_process_actions(&runtime));
    ASSERT_EQ(ACTION_RESULT_OK, rule_runtime_get_last_action_result(&runtime).code);
}


static void test_runtime_wifi_connected_fact_to_action_result(void)
{
    automation_config_t config = runtime_config();
    (void)snprintf(config.rules[0].name, RULE_NAME_MAX, "runtime wifi");
    config.rules[0].when.source = RULE_SOURCE_WIFI_CONNECTED;
    config.rules[0].when.threshold = rule_value_bool(true);
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    trigger_fact_t fact = {
        .source = RULE_SOURCE_WIFI_CONNECTED,
        .value = {.kind = RULE_VALUE_BOOL, .as.bool_value = true},
        .uptime_ms = 10,
    };
    ASSERT_EQ(1, rule_runtime_process_fact(&runtime, &fact));
    ASSERT_EQ(1, rule_runtime_process_actions(&runtime));
    ASSERT_EQ(ACTION_RESULT_OK, rule_runtime_get_last_action_result(&runtime).code);
}

static void test_runtime_button_to_action_result(void)
{
    automation_config_t config = runtime_config();
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    ASSERT_EQ(1, rule_runtime_process_button_event(&runtime, BUTTON_STATE_EVENT_KEY1_SHORT, 10));
    action_result_t result = rule_runtime_get_last_action_result(&runtime);
    ASSERT_EQ(ACTION_RESULT_OK, result.code);
    ASSERT_EQ(7, result.rule_id);
}


static action_result_t fake_runtime_local_ui_sender(const rule_event_t *event, void *ctx)
{
    int *calls = (int *)ctx;
    if (calls != NULL) {
        (*calls)++;
    }
    action_result_t result = {
        .code = ACTION_RESULT_OK,
        .sequence = event->sequence,
        .rule_id = event->rule_id,
        .action = event->action,
    };
    return result;
}


static void set_fake_reg16(uint8_t addr, uint8_t reg, uint16_t value)
{
    fake_register_bus_set_reg(addr, reg, (uint8_t)(value & 0xffu));
    fake_register_bus_set_reg(addr, (uint8_t)(reg + 1), (uint8_t)(value >> 8));
}

static void set_fake_accel_raw(int16_t x, int16_t y, int16_t z)
{
    const uint8_t reg = 0x0c;
    const uint16_t values[3] = {(uint16_t)x, (uint16_t)y, (uint16_t)z};
    for (size_t i = 0; i < 3; ++i) {
        set_fake_reg16(BOARD_BMI270_ADDR, (uint8_t)(reg + i * 2), values[i]);
    }
}

static automation_config_t hardware_runtime_config(rule_source_t source,
                                                   const char *source_key,
                                                   rule_comparator_t comparator,
                                                   rule_value_t threshold)
{
    automation_config_t config = runtime_config();
    (void)snprintf(config.rules[0].name, RULE_NAME_MAX, "runtime hardware");
    config.rules[0].when.source = source;
    memset(config.rules[0].when.source_key, 0, sizeof(config.rules[0].when.source_key));
    if (source_key != NULL) {
        (void)snprintf(config.rules[0].when.source_key, RULE_SOURCE_KEY_MAX, "%s", source_key);
    }
    config.rules[0].when.comparator = comparator;
    config.rules[0].when.threshold = threshold;
    config.rules[0].when.sustain_ms = 0;
    config.rules[0].cooldown_ms = 100;
    return config;
}

static void prepare_runtime_power_regs(uint16_t vbat_mv, uint16_t vin_mv, uint16_t fivev_mv)
{
    fake_register_bus_reset();
    set_fake_reg16(BOARD_M5PM1_ADDR, M5PM1_REG_VBAT_L, vbat_mv);
    set_fake_reg16(BOARD_M5PM1_ADDR, M5PM1_REG_VIN_L, vin_mv);
    set_fake_reg16(BOARD_M5PM1_ADDR, M5PM1_REG_5VINOUT_L, fivev_mv);
}

static void test_runtime_battery_hardware_fact_to_action_result(void)
{
    prepare_runtime_power_regs(4000, 0, 0);
    automation_config_t config = hardware_runtime_config(RULE_SOURCE_BATTERY_PERCENT, "", RULE_COMPARATOR_GT, rule_value_i32(20));
    rule_runtime_t runtime;
    int calls = 0;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
    ASSERT_TRUE(rule_runtime_poll_hardware(&runtime, 100) > 0);
    ASSERT_EQ(1, calls);
}

static void test_runtime_usb_hardware_fact_to_action_result(void)
{
    prepare_runtime_power_regs(4000, 5000, 0);
    automation_config_t config = hardware_runtime_config(RULE_SOURCE_POWER_USB_PRESENT, "", RULE_COMPARATOR_EQ, rule_value_bool(true));
    rule_runtime_t runtime;
    int calls = 0;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
    ASSERT_TRUE(rule_runtime_poll_hardware(&runtime, 100) > 0);
    ASSERT_EQ(1, calls);
}

static void test_runtime_bmi270_hardware_fact_to_action_result(void)
{
    fake_register_bus_reset();
    fake_register_bus_set_reg(BOARD_BMI270_ADDR, 0x00, 0x24);
    set_fake_accel_raw(0, 0, 0);
    automation_config_t config = hardware_runtime_config(RULE_SOURCE_BMI270_MOTION, "", RULE_COMPARATOR_EQ, rule_value_bool(true));
    rule_runtime_t runtime;
    int calls = 0;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
    (void)rule_runtime_poll_hardware(&runtime, 100);
    set_fake_accel_raw(4000, 0, 0);
    ASSERT_TRUE(rule_runtime_poll_hardware(&runtime, 1200) > 0);
    ASSERT_EQ(1, calls);
}

static void test_runtime_adc_source_key_matching(void)
{
    prepare_runtime_power_regs(4000, 0, 0);
    automation_config_t config = hardware_runtime_config(RULE_SOURCE_ADC_VOLTAGE_MV, "grove.g9", RULE_COMPARATOR_GT, rule_value_i32(1000));
    rule_runtime_t runtime;
    int calls = 0;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
    ASSERT_TRUE(rule_runtime_poll_hardware(&runtime, 100) > 0);
    ASSERT_EQ(1, calls);

    prepare_runtime_power_regs(4000, 0, 0);
    config = hardware_runtime_config(RULE_SOURCE_ADC_VOLTAGE_MV, "grove.g9", RULE_COMPARATOR_GT, rule_value_i32(1008));
    calls = 0;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
    ASSERT_TRUE(rule_runtime_poll_hardware(&runtime, 100) > 0);
    ASSERT_EQ(0, calls);
}


static automation_config_t sound_runtime_config(rule_source_t source)
{
    automation_config_t config = runtime_config();
    (void)snprintf(config.rules[0].name, RULE_NAME_MAX, "runtime sound");
    config.rules[0].when.source = source;
    config.rules[0].when.comparator = source == RULE_SOURCE_SOUND_CLIPPED ? RULE_COMPARATOR_EQ : RULE_COMPARATOR_GTE;
    config.rules[0].when.threshold = source == RULE_SOURCE_SOUND_CLIPPED ? rule_value_bool(true) : rule_value_i32(-20 * 256);
    config.rules[0].when.sustain_ms = 0;
    config.rules[0].cooldown_ms = 100;
    return config;
}

static audio_level_metrics_t sound_metrics(int32_t rms_q8, int32_t peak_q8, uint16_t clipped)
{
    audio_level_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.rms_dbfs_q8 = rms_q8;
    metrics.peak_dbfs_q8 = peak_q8;
    metrics.clipped_samples = clipped;
    return metrics;
}

static void test_runtime_sound_rms_metrics_to_local_ui_action(void)
{
    automation_config_t config = sound_runtime_config(RULE_SOURCE_SOUND_RMS_DBFS);
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    audio_level_metrics_t metrics = sound_metrics(-18 * 256, -12 * 256, 0);
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &metrics, 10));
    ASSERT_EQ(ACTION_RESULT_OK, rule_runtime_get_last_action_result(&runtime).code);
    ASSERT_EQ(7, rule_runtime_get_last_action_result(&runtime).rule_id);
}

static void test_runtime_sound_peak_metrics_to_local_ui_action(void)
{
    automation_config_t config = sound_runtime_config(RULE_SOURCE_SOUND_PEAK_DBFS);
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    audio_level_metrics_t metrics = sound_metrics(-40 * 256, -10 * 256, 0);
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &metrics, 10));
    ASSERT_EQ(ACTION_RESULT_OK, rule_runtime_get_last_action_result(&runtime).code);
}

static void test_runtime_sound_clipped_metrics_to_local_ui_action(void)
{
    automation_config_t config = sound_runtime_config(RULE_SOURCE_SOUND_CLIPPED);
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    audio_level_metrics_t metrics = sound_metrics(-40 * 256, -10 * 256, 1);
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &metrics, 10));
    ASSERT_EQ(ACTION_RESULT_OK, rule_runtime_get_last_action_result(&runtime).code);
}

static void test_runtime_sound_sustain_requires_multiple_windows(void)
{
    automation_config_t config = sound_runtime_config(RULE_SOURCE_SOUND_RMS_DBFS);
    config.rules[0].when.sustain_ms = 250;
    rule_runtime_t runtime;
    int calls = 0;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
    audio_level_metrics_t metrics = sound_metrics(-18 * 256, -12 * 256, 0);
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &metrics, 100));
    ASSERT_EQ(0, calls);
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &metrics, 360));
    ASSERT_EQ(1, calls);
}

static void test_runtime_sound_cooldown_prevents_repeated_events(void)
{
    automation_config_t config = sound_runtime_config(RULE_SOURCE_SOUND_RMS_DBFS);
    config.rules[0].cooldown_ms = 500;
    rule_runtime_t runtime;
    int calls = 0;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
    audio_level_metrics_t metrics = sound_metrics(-18 * 256, -12 * 256, 0);
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &metrics, 100));
    ASSERT_EQ(1, calls);
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &metrics, 200));
    ASSERT_EQ(1, calls);
    audio_level_metrics_t quiet = sound_metrics(-40 * 256, -30 * 256, 0);
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &quiet, 300));
    ASSERT_EQ(3, rule_runtime_process_metrics(&runtime, &metrics, 701));
    ASSERT_EQ(2, calls);
}

static void test_runtime_local_ui_callback(void)
{
    automation_config_t config = runtime_config();
    rule_runtime_t runtime;
    int calls = 0;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
    ASSERT_EQ(1, rule_runtime_process_button_event(&runtime, BUTTON_STATE_EVENT_KEY1_SHORT, 10));
    ASSERT_EQ(1, calls);
    ASSERT_EQ(ACTION_RESULT_OK, rule_runtime_get_last_action_result(&runtime).code);
}

static void test_replace_config_rejects_invalid(void)
{
    automation_config_t config = runtime_config();
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    config.rules[0].cooldown_ms = 0;
    ASSERT_EQ(0, rule_runtime_replace_config(&runtime, &config));
}

static void test_repeated_button_events_respect_cooldown(void)
{
    for (rule_source_t source = RULE_SOURCE_KEY1_SHORT; source <= RULE_SOURCE_KEY2_SHORT; ++source) {
        automation_config_t config = runtime_config();
        config.rules[0].when.source = source;
        rule_runtime_t runtime;
        int calls = 0;
        ASSERT_TRUE(rule_runtime_init(&runtime, &config));
        rule_runtime_set_local_ui_sender(&runtime, fake_runtime_local_ui_sender, &calls);
        const button_state_event_t button = source == RULE_SOURCE_KEY1_SHORT
            ? BUTTON_STATE_EVENT_KEY1_SHORT : BUTTON_STATE_EVENT_KEY2_SHORT;
        (void)rule_runtime_process_button_event(&runtime, button, 0);
        (void)rule_runtime_process_button_event(&runtime, button, 99);
        ASSERT_EQ(1, calls);
        (void)rule_runtime_process_button_event(&runtime, button, 100);
        (void)rule_runtime_process_button_event(&runtime, button, 300);
        ASSERT_EQ(3, calls);
        ASSERT_EQ(3, runtime.engine.state[0].fire_count);
    }
}

static uint32_t s_delivered_ids[RULE_MAX_RULES * RULE_MAX_ACTIONS_PER_RULE];
static size_t s_delivered_count;
static action_result_t capture_fanout(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    ASSERT_TRUE(s_delivered_count < RULE_MAX_RULES * RULE_MAX_ACTIONS_PER_RULE);
    s_delivered_ids[s_delivered_count++] = event->rule_id;
    ASSERT_EQ(s_delivered_count, event->sequence);
    return (action_result_t){.code = ACTION_RESULT_OK, .sequence = event->sequence};
}

static void test_all_matching_rules_deliver_all_actions_in_order(void)
{
    automation_config_t config = runtime_config();
    config.rule_count = RULE_MAX_RULES;
    config.rules[0].action_count = RULE_MAX_ACTIONS_PER_RULE;
    for (size_t j = 0; j < RULE_MAX_ACTIONS_PER_RULE; ++j) {
        config.rules[0].actions[j].type = RULE_ACTION_LOCAL_UI;
    }
    for (size_t i = 1; i < RULE_MAX_RULES; ++i) {
        config.rules[i] = config.rules[0];
        config.rules[i].id += (uint32_t)i;
    }
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    s_delivered_count = 0;
    rule_runtime_set_local_ui_sender(&runtime, capture_fanout, NULL);
    trigger_fact_t fact = {.source = RULE_SOURCE_KEY1_SHORT, .value = rule_value_bool(true), .uptime_ms = 100};
    ASSERT_EQ(24, rule_runtime_process_fact(&runtime, &fact));
    ASSERT_EQ(0, runtime.enqueue_errors);
    ASSERT_EQ(24, rule_runtime_process_actions(&runtime));
    ASSERT_EQ(24, s_delivered_count);
    for (size_t i = 0; i < RULE_MAX_RULES; ++i) {
        ASSERT_EQ(1, runtime.engine.state[i].fire_count);
        for (size_t j = 0; j < RULE_MAX_ACTIONS_PER_RULE; ++j) {
            ASSERT_EQ(config.rules[i].id, s_delivered_ids[i * RULE_MAX_ACTIONS_PER_RULE + j]);
        }
    }
}

static void test_queue_overflow_rejects_whole_rule_without_consuming_transition(void)
{
    automation_config_t config = runtime_config();
    config.rules[0].when.source = RULE_SOURCE_WIFI_CONNECTED;
    config.rules[0].action_count = 3;
    for (size_t j = 0; j < 3; ++j) {
        config.rules[0].actions[j].type = RULE_ACTION_LOCAL_UI;
    }
    rule_runtime_t runtime;
    ASSERT_TRUE(rule_runtime_init(&runtime, &config));
    rule_event_t old = {.action = RULE_ACTION_LOCAL_UI};
    for (size_t i = 0; i < ACTION_DISPATCHER_QUEUE_LEN; ++i) {
        ASSERT_TRUE(action_enqueue(&runtime.dispatcher, &old));
    }
    trigger_fact_t fact = {.source = RULE_SOURCE_WIFI_CONNECTED, .value = rule_value_bool(true), .uptime_ms = 100};
    ASSERT_EQ(0, rule_runtime_process_fact(&runtime, &fact));
    ASSERT_EQ(3, runtime.enqueue_errors);
    ASSERT_EQ(ACTION_RESULT_QUEUE_FULL, rule_runtime_get_last_action_result(&runtime).code);
    ASSERT_EQ(0, runtime.engine.state[0].fire_count);
    ASSERT_TRUE(!runtime.engine.state[0].has_last_fire);
    ASSERT_EQ(1, runtime.engine.next_event_sequence);
    ASSERT_EQ(ACTION_DISPATCHER_QUEUE_LEN, rule_runtime_process_actions(&runtime));
    fact.uptime_ms = 101;
    ASSERT_EQ(3, rule_runtime_process_fact(&runtime, &fact));
    ASSERT_EQ(1, runtime.engine.state[0].fire_count);
    ASSERT_EQ(3, rule_runtime_process_actions(&runtime));
}

int main(void)
{
    test_repeated_button_events_respect_cooldown();
    test_all_matching_rules_deliver_all_actions_in_order();
    test_queue_overflow_rejects_whole_rule_without_consuming_transition();
    test_runtime_button_to_action_result();
    test_runtime_gpio_poll_to_action_result();
    test_runtime_ble_connected_fact_to_action_result();
    test_runtime_wifi_connected_fact_to_action_result();
    test_runtime_local_ui_callback();
    test_runtime_battery_hardware_fact_to_action_result();
    test_runtime_usb_hardware_fact_to_action_result();
    test_runtime_bmi270_hardware_fact_to_action_result();
    test_runtime_adc_source_key_matching();
    test_runtime_sound_rms_metrics_to_local_ui_action();
    test_runtime_sound_peak_metrics_to_local_ui_action();
    test_runtime_sound_clipped_metrics_to_local_ui_action();
    test_runtime_sound_sustain_requires_multiple_windows();
    test_runtime_sound_cooldown_prevents_repeated_events();
    test_replace_config_rejects_invalid();
    puts("rule_runtime tests passed");
    return 0;
}
