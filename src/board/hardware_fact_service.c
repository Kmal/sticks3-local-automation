#include "hardware_fact_service.h"

#include "board_sticks3.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "capability_registry.h"

#ifndef CONFIG_APP_BATTERY_FACTS
#define CONFIG_APP_BATTERY_FACTS 0
#endif
#ifndef CONFIG_APP_USB_POWER_FACTS
#define CONFIG_APP_USB_POWER_FACTS 0
#endif
#ifndef CONFIG_APP_BMI270_FACTS
#define CONFIG_APP_BMI270_FACTS 0
#endif
#ifndef CONFIG_APP_ADC_FACTS
#define CONFIG_APP_ADC_FACTS 0
#endif

#include <stdio.h>
#include <string.h>

static const char *TAG = "HW_FACTS";

static bool emit_i32_fact(trigger_adapter_t *adapter,
                          rule_source_t source,
                          const char *source_key,
                          int32_t value,
                          uint32_t uptime_ms)
{
    trigger_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.source = source;
    if (source_key != NULL) {
        (void)snprintf(fact.source_key, sizeof(fact.source_key), "%s", source_key);
    }
    fact.value = rule_value_i32(value);
    fact.uptime_ms = uptime_ms;
    return trigger_emit_fact(adapter, &fact);
}

static bool emit_bool_fact(trigger_adapter_t *adapter,
                           rule_source_t source,
                           const char *source_key,
                           bool value,
                           uint32_t uptime_ms)
{
    trigger_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.source = source;
    if (source_key != NULL) {
        (void)snprintf(fact.source_key, sizeof(fact.source_key), "%s", source_key);
    }
    fact.value = rule_value_bool(value);
    fact.uptime_ms = uptime_ms;
    return trigger_emit_fact(adapter, &fact);
}

hardware_fact_service_config_t hardware_fact_service_default_config(void)
{
    return (hardware_fact_service_config_t) {
        .enable_battery = CONFIG_APP_BATTERY_FACTS,
        .enable_usb_power = CONFIG_APP_USB_POWER_FACTS,
        .enable_bmi270 = CONFIG_APP_BMI270_FACTS,
        .enable_adc = CONFIG_APP_ADC_FACTS,
        .adc_gpio_mask = UINT32_MAX,
        .poll_interval_ms = CONFIG_APP_HARDWARE_FACT_POLL_INTERVAL_MS,
        .power = {
            .usb_present_mv_threshold = CONFIG_APP_POWER_USB_PRESENT_MV,
            .battery_min_valid_mv = 2500,
            .battery_max_valid_mv = 4500,
        },
        .motion = {
            .motion_delta_mg = CONFIG_APP_BMI270_MOTION_DELTA_MG,
            .still_hysteresis_mg = 30,
            .min_interval_ms = CONFIG_APP_HARDWARE_FACT_POLL_INTERVAL_MS,
        },
};
}

void hardware_fact_service_config_for_rules(hardware_fact_service_config_t *out, const automation_config_t *rules)
{
    *out = hardware_fact_service_default_config();
    out->enable_battery &= automation_config_has_enabled_source(rules, RULE_SOURCE_BATTERY_PERCENT);
    out->enable_usb_power &= automation_config_has_enabled_source(rules, RULE_SOURCE_POWER_USB_PRESENT);
    out->enable_bmi270 &= automation_config_has_enabled_source(rules, RULE_SOURCE_BMI270_MOTION);
    out->enable_adc &= automation_config_has_enabled_source(rules, RULE_SOURCE_ADC_VOLTAGE_MV);
    out->adc_gpio_mask = 0;
    if (rules != NULL) for (size_t i = 0; i < rules->rule_count; ++i) {
        const automation_rule_t *rule = &rules->rules[i];
        if (!rule->enabled || rule->when.source != RULE_SOURCE_ADC_VOLTAGE_MV) continue;
        for (int pin = 4; pin <= 10; ++pin) if (capability_adc_key_matches_pin(rule->when.source_key, pin)) out->adc_gpio_mask |= 1u << pin;
    }
}

void hardware_fact_service_deinit(hardware_fact_service_t *service)
{
    if (service == NULL) return;
    board_adc_deinit(&service->adc);
    memset(service, 0, sizeof(*service));
}

esp_err_t hardware_fact_service_init(hardware_fact_service_t *service,
                                     trigger_adapter_t *adapter,
                                     const hardware_fact_service_config_t *config)
{
    if (service == NULL || adapter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    hardware_fact_service_config_t cfg = config != NULL ? *config : hardware_fact_service_default_config();
    memset(service, 0, sizeof(*service));
    service->config = cfg;
    service->adapter = adapter;
    if ((service->config.enable_battery || service->config.enable_usb_power) &&
        board_power_init(&service->config.power) != ESP_OK) {
        service->config.enable_battery = false;
        service->config.enable_usb_power = false;
    }
    if (service->config.enable_bmi270) {
        if (bmi270_motion_state_init(&service->motion_state, &service->config.motion) != ESP_OK ||
            bmi270_init(BOARD_I2C_PORT, BOARD_BMI270_ADDR) != ESP_OK) {
            ESP_LOGW(TAG, "BMI270 unavailable; disabling motion facts");
            service->config.enable_bmi270 = false;
        }
    }
    if (service->config.enable_adc && board_adc_init_mask(&service->adc, service->config.adc_gpio_mask) != ESP_OK) {
        ESP_LOGW(TAG, "ADC unavailable; disabling ADC facts");
        service->config.enable_adc = false;
    }
    service->initialized = true;
    return ESP_OK;
}

size_t hardware_fact_service_poll(hardware_fact_service_t *service,
                                  uint32_t uptime_ms)
{
    if (service == NULL || !service->initialized || service->adapter == NULL) {
        return 0;
    }
    if (service->has_polled && service->config.poll_interval_ms > 0 &&
        uptime_ms - service->last_poll_ms < service->config.poll_interval_ms) {
        return 0;
    }
    service->has_polled = true;
    service->last_poll_ms = uptime_ms;

    size_t emitted = 0;
    if (service->config.enable_battery || service->config.enable_usb_power) {
        board_power_status_t status;
        if (board_power_read_status(&status) == ESP_OK) {
            if (service->config.enable_battery) {
                if (status.valid) {
                    if (emit_i32_fact(service->adapter, RULE_SOURCE_BATTERY_PERCENT, "", status.battery_percent, uptime_ms)) {
                        ++emitted;
                    }
                    service->last_battery_valid = true;
                    service->last_battery_percent = status.battery_percent;
                } else {
                    service->last_battery_valid = false;
                }
            }
            if (service->config.enable_usb_power) {
                if (status.usb_valid) {
                    if (!service->last_usb_valid || service->last_usb_present != status.usb_present) {
                        if (emit_bool_fact(service->adapter, RULE_SOURCE_POWER_USB_PRESENT, "", status.usb_present, uptime_ms)) {
                            ++emitted;
                        }
                    }
                    service->last_usb_valid = true;
                    service->last_usb_present = status.usb_present;
                } else {
                    service->last_usb_valid = false;
                }
            }
        }
    }
    if (service->config.enable_bmi270) {
        bool motion = false;
        if (bmi270_motion_poll(&service->motion_state, BOARD_I2C_PORT, BOARD_BMI270_ADDR, uptime_ms, &motion) == ESP_OK) {
            if (motion || (service->last_motion_valid && service->last_motion && !motion)) {
                if (emit_bool_fact(service->adapter, RULE_SOURCE_BMI270_MOTION, "", motion, uptime_ms)) {
                    ++emitted;
                }
            }
            service->last_motion = motion;
            service->last_motion_valid = true;
        }
    }
    if (service->config.enable_adc) {
        size_t count = 0;
        const board_adc_channel_desc_t *channels = board_adc_channels(&count);
        for (size_t i = 0; i < count; ++i) {
            if (!channels[i].safe_for_user_rules || (service->config.adc_gpio_mask & (1u << channels[i].gpio)) == 0) {
                continue;
            }
            board_adc_sample_t sample;
            if (board_adc_read_mv(&service->adc, &channels[i], &sample) == ESP_OK) {
                if (emit_i32_fact(service->adapter, RULE_SOURCE_ADC_VOLTAGE_MV, channels[i].source_key, sample.voltage_mv, uptime_ms)) {
                    ++emitted;
                }
            }
        }
    }
    return emitted;
}
