#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    gpio_num_t gpio;
    adc_unit_t unit;
    adc_channel_t channel;
    const char *source_key;
    uint16_t divider_numerator;
    uint16_t divider_denominator;
    bool safe_for_user_rules;
} board_adc_channel_desc_t;

typedef struct {
    bool initialized;
    adc_oneshot_unit_handle_t adc1;
    void *cali_adc1;
    bool calibration_enabled;
    uint32_t gpio_mask;
} board_adc_context_t;

typedef struct {
    const board_adc_channel_desc_t *channel;
    int raw;
    int voltage_mv;
    uint32_t sample_time_ms;
} board_adc_sample_t;

esp_err_t board_adc_init(board_adc_context_t *ctx);
esp_err_t board_adc_init_mask(board_adc_context_t *ctx, uint32_t gpio_mask);
void board_adc_deinit(board_adc_context_t *ctx);
const board_adc_channel_desc_t *board_adc_channels(size_t *out_count);
esp_err_t board_adc_read_mv(board_adc_context_t *ctx,
                            const board_adc_channel_desc_t *channel,
                            board_adc_sample_t *out_sample);
int board_adc_apply_divider_mv(int measured_mv, const board_adc_channel_desc_t *channel);
