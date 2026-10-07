#pragma once

#include "rule_types.h"

#include <stdbool.h>
#include <stddef.h>

bool capability_source_supported(rule_source_t source);
bool capability_source_schema_supported(rule_source_t source);
bool capability_source_runtime_available(rule_source_t source);
const char *capability_source_availability_reason(rule_source_t source);
bool capability_action_supported(rule_action_type_t action);
const char *capability_source_reason(rule_source_t source);
const char *capability_action_reason(rule_action_type_t action);
bool capability_gpio_profile_validate(const rule_gpio_config_t *gpio, char *error, size_t error_len);
bool capability_gpio_source_profile_validate(rule_source_t source, const rule_gpio_config_t *gpio, char *error, size_t error_len);
bool capability_hat_supported(rule_source_t source);
/* Empty ADC keys address all allowed channels. */
bool capability_adc_key_matches_pin(const char *key, int pin);
bool capability_adc_key_valid(const char *key);
size_t capability_build_json(char *out, size_t out_len);
