#pragma once

#include "bmi270.h"
#include "board_adc.h"
#include "board_power.h"
#include "trigger_sources.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool enable_battery;
    bool enable_usb_power;
    bool enable_bmi270;
    bool enable_adc;
    uint32_t adc_gpio_mask;
    uint32_t poll_interval_ms;
    board_power_config_t power;
    bmi270_motion_config_t motion;
} hardware_fact_service_config_t;

typedef struct {
    bool initialized;
    hardware_fact_service_config_t config;
    trigger_adapter_t *adapter;
    uint32_t last_poll_ms;
    bool has_polled;

    bool last_usb_present;
    bool last_usb_valid;

    uint8_t last_battery_percent;
    bool last_battery_valid;

    bool last_motion;
    bool last_motion_valid;

    bmi270_motion_state_t motion_state;
    board_adc_context_t adc;
} hardware_fact_service_t;

hardware_fact_service_config_t hardware_fact_service_default_config(void);

esp_err_t hardware_fact_service_init(hardware_fact_service_t *service,
                                     trigger_adapter_t *adapter,
                                     const hardware_fact_service_config_t *config);
void hardware_fact_service_deinit(hardware_fact_service_t *service);
void hardware_fact_service_config_for_rules(hardware_fact_service_config_t *out, const automation_config_t *rules);

size_t hardware_fact_service_poll(hardware_fact_service_t *service,
                                  uint32_t uptime_ms);
