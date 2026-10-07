#include "hardware_fact_service.h"
#include "board_sticks3.h"
#include "m5pm1.h"
#include "fake_register_bus.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_FACTS 64

static trigger_fact_t s_facts[MAX_FACTS];
static size_t s_fact_count;

static bool collect_fact(const trigger_fact_t *fact, void *user_ctx)
{
    (void)user_ctx;
    if (fact == NULL || s_fact_count >= MAX_FACTS) {
        return false;
    }
    s_facts[s_fact_count++] = *fact;
    return true;
}

static void reset_collector(void)
{
    memset(s_facts, 0, sizeof(s_facts));
    s_fact_count = 0;
}

static void set_reg16(uint8_t addr, uint8_t reg, uint16_t value)
{
    fake_register_bus_set_reg(addr, reg, (uint8_t)(value & 0xffu));
    fake_register_bus_set_reg(addr, (uint8_t)(reg + 1), (uint8_t)(value >> 8));
}

static void set_accel_raw(int16_t x, int16_t y, int16_t z)
{
    const uint8_t reg = 0x0c;
    const uint16_t values[3] = {(uint16_t)x, (uint16_t)y, (uint16_t)z};
    for (size_t i = 0; i < 3; ++i) {
        set_reg16(BOARD_BMI270_ADDR, (uint8_t)(reg + i * 2), values[i]);
    }
}

static hardware_fact_service_config_t test_config(void)
{
    hardware_fact_service_config_t cfg = hardware_fact_service_default_config();
    cfg.poll_interval_ms = 100;
    cfg.power.usb_present_mv_threshold = 4400;
    cfg.motion.min_interval_ms = 0;
    cfg.motion.motion_delta_mg = 120;
    cfg.motion.still_hysteresis_mg = 30;
    return cfg;
}

static void prepare_hardware_registers(uint16_t vbat_mv, uint16_t vin_mv, uint16_t fivev_mv)
{
    fake_register_bus_reset();
    fake_register_bus_set_reg(BOARD_BMI270_ADDR, 0x00, 0x24);
    fake_register_bus_set_reg(BOARD_BMI270_ADDR, 0x21, 1);
    set_accel_raw(0, 0, 0);
    set_reg16(BOARD_M5PM1_ADDR, M5PM1_REG_VBAT_L, vbat_mv);
    set_reg16(BOARD_M5PM1_ADDR, M5PM1_REG_VIN_L, vin_mv);
    set_reg16(BOARD_M5PM1_ADDR, M5PM1_REG_5VINOUT_L, fivev_mv);
}

static bool has_i32_fact(rule_source_t source, const char *key, int32_t min_value)
{
    for (size_t i = 0; i < s_fact_count; ++i) {
        if (s_facts[i].source == source && strcmp(s_facts[i].source_key, key) == 0 &&
            s_facts[i].value.kind == RULE_VALUE_I32 && s_facts[i].value.as.i32_value >= min_value) {
            return true;
        }
    }
    return false;
}

static bool has_bool_fact(rule_source_t source, bool value)
{
    for (size_t i = 0; i < s_fact_count; ++i) {
        if (s_facts[i].source == source && s_facts[i].value.kind == RULE_VALUE_BOOL &&
            s_facts[i].value.as.bool_value == value) {
            return true;
        }
    }
    return false;
}

static void test_emits_power_and_adc_facts(void)
{
    prepare_hardware_registers(4000, 5000, 0);
    reset_collector();
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, collect_fact, NULL);
    hardware_fact_service_t service;
    hardware_fact_service_config_t cfg = test_config();

    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    assert(hardware_fact_service_poll(&service, 100) > 0);
    assert(has_i32_fact(RULE_SOURCE_BATTERY_PERCENT, "", 70));
    assert(has_bool_fact(RULE_SOURCE_POWER_USB_PRESENT, true));
    assert(has_i32_fact(RULE_SOURCE_ADC_VOLTAGE_MV, "grove.g9", 1000));
    assert(has_i32_fact(RULE_SOURCE_ADC_VOLTAGE_MV, "grove.g10", 1000));
}

static void test_usb_change_and_poll_interval(void)
{
    prepare_hardware_registers(4000, 5000, 0);
    reset_collector();
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, collect_fact, NULL);
    hardware_fact_service_t service;
    hardware_fact_service_config_t cfg = test_config();
    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    assert(hardware_fact_service_poll(&service, 100) > 0);
    const size_t first_count = s_fact_count;
    assert(hardware_fact_service_poll(&service, 150) == 0);
    assert(s_fact_count == first_count);
    reset_collector();
    assert(hardware_fact_service_poll(&service, 200) > 0);
    assert(has_bool_fact(RULE_SOURCE_POWER_USB_PRESENT, true));
    assert(has_bool_fact(RULE_SOURCE_BMI270_MOTION, false));

    set_reg16(BOARD_M5PM1_ADDR, M5PM1_REG_VIN_L, 0);
    set_reg16(BOARD_M5PM1_ADDR, M5PM1_REG_5VINOUT_L, 0);
    assert(hardware_fact_service_poll(&service, 300) > 0);
    assert(has_bool_fact(RULE_SOURCE_POWER_USB_PRESENT, false));
}

static void test_battery_and_usb_gates_are_independent(void)
{
    prepare_hardware_registers(4000, 5000, 0);
    reset_collector();
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, collect_fact, NULL);
    hardware_fact_service_t service;
    hardware_fact_service_config_t cfg = test_config();
    cfg.enable_battery = false;
    cfg.enable_usb_power = true;
    cfg.enable_bmi270 = false;
    cfg.enable_adc = false;

    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    assert(hardware_fact_service_poll(&service, 100) > 0);
    assert(!has_i32_fact(RULE_SOURCE_BATTERY_PERCENT, "", 0));
    assert(has_bool_fact(RULE_SOURCE_POWER_USB_PRESENT, true));

    reset_collector();
    cfg = test_config();
    cfg.enable_battery = true;
    cfg.enable_usb_power = false;
    cfg.enable_bmi270 = false;
    cfg.enable_adc = false;
    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    assert(hardware_fact_service_poll(&service, 250) > 0);
    assert(has_i32_fact(RULE_SOURCE_BATTERY_PERCENT, "", 70));
    assert(!has_bool_fact(RULE_SOURCE_POWER_USB_PRESENT, true));
}

static void test_usb_gate_survives_vbat_read_failure(void)
{
    prepare_hardware_registers(4000, 5000, 0);
    fake_register_bus_fail_next_read(ESP_FAIL);
    reset_collector();
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, collect_fact, NULL);
    hardware_fact_service_t service;
    hardware_fact_service_config_t cfg = test_config();
    cfg.enable_battery = false;
    cfg.enable_usb_power = true;
    cfg.enable_bmi270 = false;
    cfg.enable_adc = false;

    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    assert(hardware_fact_service_poll(&service, 100) > 0);
    assert(!has_i32_fact(RULE_SOURCE_BATTERY_PERCENT, "", 0));
    assert(has_bool_fact(RULE_SOURCE_POWER_USB_PRESENT, true));
}

static void test_usb_fact_not_emitted_when_absence_is_unknown(void)
{
    prepare_hardware_registers(4000, 1000, 0);
    fake_register_bus_fail_reg_read_once(BOARD_M5PM1_ADDR, M5PM1_REG_5VINOUT_L, ESP_FAIL);
    reset_collector();
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, collect_fact, NULL);
    hardware_fact_service_t service;
    hardware_fact_service_config_t cfg = test_config();
    cfg.enable_battery = false;
    cfg.enable_usb_power = true;
    cfg.enable_bmi270 = false;
    cfg.enable_adc = false;

    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    assert(hardware_fact_service_poll(&service, 100) == 0);
    assert(!has_bool_fact(RULE_SOURCE_POWER_USB_PRESENT, false));
}

static void test_motion_true_and_false_transition(void)
{
    prepare_hardware_registers(4000, 0, 0);
    reset_collector();
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, collect_fact, NULL);
    hardware_fact_service_t service;
    hardware_fact_service_config_t cfg = test_config();
    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    (void)hardware_fact_service_poll(&service, 100);

    reset_collector();
    set_accel_raw(4000, 0, 0);
    assert(hardware_fact_service_poll(&service, 250) > 0);
    assert(has_bool_fact(RULE_SOURCE_BMI270_MOTION, true));

    reset_collector();
    set_accel_raw(4050, 0, 0);
    assert(hardware_fact_service_poll(&service, 400) > 0);
    assert(has_bool_fact(RULE_SOURCE_BMI270_MOTION, false));
}

static void test_invalid_battery_still_emits_usb(void)
{
    prepare_hardware_registers(100, 5000, 0);
    reset_collector();
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, collect_fact, NULL);
    hardware_fact_service_t service;
    hardware_fact_service_config_t cfg = test_config();
    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    assert(hardware_fact_service_poll(&service, 100) > 0);
    assert(!has_i32_fact(RULE_SOURCE_BATTERY_PERCENT, "", 0));
    assert(has_bool_fact(RULE_SOURCE_POWER_USB_PRESENT, true));
}

static void test_disabled_features_are_tolerated(void)
{
    reset_collector();
    trigger_adapter_t adapter;
    trigger_adapter_init(&adapter, collect_fact, NULL);
    hardware_fact_service_t service;
    hardware_fact_service_config_t cfg = test_config();
    cfg.enable_battery = false;
    cfg.enable_usb_power = false;
    cfg.enable_bmi270 = false;
    cfg.enable_adc = false;
    assert(hardware_fact_service_init(&service, &adapter, &cfg) == ESP_OK);
    assert(hardware_fact_service_poll(&service, 100) == 0);
}

int main(void)
{
    test_emits_power_and_adc_facts();
    test_usb_change_and_poll_interval();
    test_battery_and_usb_gates_are_independent();
    test_usb_gate_survives_vbat_read_failure();
    test_usb_fact_not_emitted_when_absence_is_unknown();
    test_motion_true_and_false_transition();
    test_invalid_battery_still_emits_usb();
    test_disabled_features_are_tolerated();
    return 0;
}
