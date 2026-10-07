#include "action_ir.h"
#include "board_sticks3.h"
#ifdef ESP_PLATFORM
#include "m5pm1.h"
#include "esp_log.h"
#endif

#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/rmt_tx.h"
#include "esp_err.h"
#endif

#define NEC_LEADER_HIGH_US 9000u
#define NEC_LEADER_LOW_US 4500u
#define NEC_BIT_HIGH_US 560u
#define NEC_ZERO_LOW_US 560u
#define NEC_ONE_LOW_US 1690u
#define NEC_STOP_HIGH_US 560u
#define NEC_REPEAT_HIGH_US 9000u
#define NEC_REPEAT_LOW_US 2250u
#define NEC_SYMBOL_COUNT 35u
#define NEC_REPEAT_SYMBOL_COUNT 3u
#define NEC_PERIOD_US 110000u

bool action_ir_validate(const action_ir_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    if (config->protocol != RULE_IR_PROTOCOL_NEC) {
        return false;
    }
    if (config->carrier_hz < 30000u || config->carrier_hz > 60000u) {
        return false;
    }
    if (config->repeat_count > 5u || config->timeout_ms == 0 || config->timeout_ms > 1000u) {
        return false;
    }
    return true;
}

bool action_ir_config_from_action(const rule_action_t *action, action_ir_config_t *config)
{
    if (action == NULL || config == NULL || action->type != RULE_ACTION_IR_SEND) {
        return false;
    }
    memset(config, 0, sizeof(*config));
    config->protocol = action->ir_protocol;
    config->carrier_hz = action->ir_carrier_hz;
    config->address = action->ir_address;
    config->command = action->ir_command;
    config->repeat_count = action->ir_repeat_count;
    config->timeout_ms = action->timeout_ms;
    return action_ir_validate(config);
}

size_t action_ir_encode_nec(const action_ir_config_t *config, bool repeat, action_ir_symbol_t *out, size_t capacity)
{
    const size_t count = repeat ? NEC_REPEAT_SYMBOL_COUNT : NEC_SYMBOL_COUNT;
    if (!action_ir_validate(config) || out == NULL || capacity < count) return 0;
    memset(out, 0, count * sizeof(*out));
    uint32_t duration = 0;
    out[0] = (action_ir_symbol_t){NEC_LEADER_HIGH_US, repeat ? NEC_REPEAT_LOW_US : NEC_LEADER_LOW_US};
    if (!repeat) {
        uint32_t payload = (config->address & 0xffu) | (((~config->address) & 0xffu) << 8u) |
                           ((config->command & 0xffu) << 16u) | (((~config->command) & 0xffu) << 24u);
        for (size_t i = 0; i < 32; ++i)
            out[i + 1] = (action_ir_symbol_t){NEC_BIT_HIGH_US, payload & (1u << i) ? NEC_ONE_LOW_US : NEC_ZERO_LOW_US};
    }
    out[count - 2].high_us = NEC_STOP_HIGH_US;
    for (size_t i = 0; i < count; ++i) duration += out[i].high_us + out[i].low_us;
    uint32_t silence = NEC_PERIOD_US - duration;
    /* RMT durations are 15-bit. Carry the quiet interval as LOW levels,
     * rather than relying on scheduling between repeat transmissions. */
    out[count - 2].low_us = silence > 32767u ? 32767u : silence;
    silence -= out[count - 2].low_us;
    out[count - 1].high_us = silence > 32767u ? 32767u : silence;
    out[count - 1].low_us = silence - out[count - 1].high_us;
    return count;
}

#ifdef ESP_PLATFORM
static size_t build_nec_symbols(const action_ir_config_t *config, bool repeat, rmt_symbol_word_t *symbols)
{
    action_ir_symbol_t durations[NEC_SYMBOL_COUNT];
    size_t count = action_ir_encode_nec(config, repeat, durations, NEC_SYMBOL_COUNT);
    for (size_t i = 0; i < count; ++i) {
        symbols[i] = (rmt_symbol_word_t){.level0 = i + 1 < count, .level1 = 0,
            .duration0 = durations[i].high_us, .duration1 = durations[i].low_us};
    }
    return count;
}

static bool transmit_symbols(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder,
                             const rmt_symbol_word_t *symbols, size_t symbol_count, uint32_t timeout_ms)
{
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    esp_err_t err = rmt_transmit(channel, encoder, symbols, symbol_count * sizeof(symbols[0]), &tx_config);
    if (err != ESP_OK) {
        return false;
    }
    /* A NEC period is 110 ms; legacy short timeouts cannot truncate it. */
    return rmt_tx_wait_all_done(channel, timeout_ms < 120u ? 120u : timeout_ms) == ESP_OK;
}
#endif

bool action_ir_send(const action_ir_config_t *config, const rule_event_t *event)
{
    (void)event;
    if (!action_ir_validate(config)) {
        return false;
    }
#ifdef ESP_PLATFORM
    /* EXT_5V defaults to INPUT. Do not drive an externally powered Grove/HAT
     * rail: require measured supply instead of changing its direction. */
    uint16_t rail_mv = 0;
    if (m5pm1_read_5v_inout_mv(BOARD_I2C_PORT, BOARD_M5PM1_ADDR, &rail_mv) != ESP_OK || rail_mv < 4000) {
        ESP_LOGW("ACTION_IR", "IR unavailable: EXT_5V supply absent or unreadable; rail mode unchanged");
        return false;
    }
    rmt_channel_handle_t channel = NULL;
    rmt_encoder_handle_t encoder = NULL;
    rmt_tx_channel_config_t channel_config = {
        .gpio_num = BOARD_IR_TX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000u,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    if (rmt_new_tx_channel(&channel_config, &channel) != ESP_OK) {
        return false;
    }
    rmt_carrier_config_t carrier = {
        .frequency_hz = config->carrier_hz,
        .duty_cycle = 0.33f,
    };
    rmt_copy_encoder_config_t encoder_config = {};
    bool ok = rmt_apply_carrier(channel, &carrier) == ESP_OK &&
              rmt_new_copy_encoder(&encoder_config, &encoder) == ESP_OK &&
              rmt_enable(channel) == ESP_OK;
    if (ok) {
        rmt_symbol_word_t frame[NEC_SYMBOL_COUNT];
        (void)build_nec_symbols(config, false, frame);
        ok = transmit_symbols(channel, encoder, frame, NEC_SYMBOL_COUNT, config->timeout_ms);
        rmt_symbol_word_t repeat[NEC_REPEAT_SYMBOL_COUNT];
        (void)build_nec_symbols(config, true, repeat);
        for (uint8_t i = 0; ok && i < config->repeat_count; ++i) {
            ok = transmit_symbols(channel, encoder, repeat, NEC_REPEAT_SYMBOL_COUNT, config->timeout_ms);
        }
    }
    if (channel != NULL) {
        (void)rmt_disable(channel);
    }
    if (encoder != NULL) {
        (void)rmt_del_encoder(encoder);
    }
    if (channel != NULL) {
        (void)rmt_del_channel(channel);
    }
    return ok;
#else
    return false;
#endif
}

bool action_ir_send_event(const rule_event_t *event)
{
    if (event == NULL) {
        return false;
    }
    action_ir_config_t config;
    if (!action_ir_config_from_action(&event->action_config, &config)) {
        return false;
    }
    return action_ir_send(&config, event);
}
