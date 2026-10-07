#pragma once

#include "rule_engine.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    rule_ir_protocol_t protocol;
    uint32_t carrier_hz;
    uint16_t address;
    uint16_t command;
    uint8_t repeat_count;
    uint32_t timeout_ms;
} action_ir_config_t;

/* Portable encoder also lets host tests inspect complete on-wire timing. */
typedef struct { uint16_t high_us, low_us; } action_ir_symbol_t;
size_t action_ir_encode_nec(const action_ir_config_t *config, bool repeat, action_ir_symbol_t *out, size_t capacity);
bool action_ir_validate(const action_ir_config_t *config);
bool action_ir_config_from_action(const rule_action_t *action, action_ir_config_t *config);
bool action_ir_send(const action_ir_config_t *config, const rule_event_t *event);
bool action_ir_send_event(const rule_event_t *event);
