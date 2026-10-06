#pragma once

#include "rule_types.h"
#include "trigger_sources.h"

#include <stdbool.h>

typedef struct {
    bool enabled;
    rule_gpio_config_t config;
    rule_source_t source;
    bool has_sample;
    bool initial_pending;
    bool last_level;
    bool stable_level;
    uint32_t last_change_ms;
#ifndef ESP_PLATFORM
    bool host_level;
#endif
} trigger_gpio_t;

bool trigger_gpio_init(trigger_gpio_t *gpio, rule_source_t source, const rule_gpio_config_t *config);
void trigger_gpio_deinit(trigger_gpio_t *gpio);
bool trigger_gpio_probe(const trigger_gpio_t *gpio);
#ifndef ESP_PLATFORM
void trigger_gpio_set_host_level(trigger_gpio_t *gpio, bool level);
#endif
size_t trigger_gpio_poll(trigger_gpio_t *gpio, trigger_adapter_t *adapter, uint32_t uptime_ms);
