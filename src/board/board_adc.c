#include "board_adc.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include <string.h>

#define TAG "BOARD_ADC"

static const board_adc_channel_desc_t s_adc_channels[] = {
    {.gpio = GPIO_NUM_9, .unit = ADC_UNIT_1, .channel = ADC_CHANNEL_8, .source_key = "grove.g9", .divider_numerator = 1, .divider_denominator = 1, .safe_for_user_rules = true},
    {.gpio = GPIO_NUM_10, .unit = ADC_UNIT_1, .channel = ADC_CHANNEL_9, .source_key = "grove.g10", .divider_numerator = 1, .divider_denominator = 1, .safe_for_user_rules = true},
    {.gpio = GPIO_NUM_4, .unit = ADC_UNIT_1, .channel = ADC_CHANNEL_3, .source_key = "hat.g4", .divider_numerator = 1, .divider_denominator = 1, .safe_for_user_rules = true},
    {.gpio = GPIO_NUM_5, .unit = ADC_UNIT_1, .channel = ADC_CHANNEL_4, .source_key = "hat.g5", .divider_numerator = 1, .divider_denominator = 1, .safe_for_user_rules = true},
    {.gpio = GPIO_NUM_6, .unit = ADC_UNIT_1, .channel = ADC_CHANNEL_5, .source_key = "hat.g6", .divider_numerator = 1, .divider_denominator = 1, .safe_for_user_rules = true},
    {.gpio = GPIO_NUM_7, .unit = ADC_UNIT_1, .channel = ADC_CHANNEL_6, .source_key = "hat.g7", .divider_numerator = 1, .divider_denominator = 1, .safe_for_user_rules = true},
    {.gpio = GPIO_NUM_8, .unit = ADC_UNIT_1, .channel = ADC_CHANNEL_7, .source_key = "hat.g8", .divider_numerator = 1, .divider_denominator = 1, .safe_for_user_rules = true},
};

const board_adc_channel_desc_t *board_adc_channels(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(s_adc_channels) / sizeof(s_adc_channels[0]);
    }
    return s_adc_channels;
}

int board_adc_apply_divider_mv(int measured_mv, const board_adc_channel_desc_t *channel)
{
    if (channel == NULL || channel->divider_denominator == 0) {
        return measured_mv;
    }
    return (int)(((int64_t)measured_mv * channel->divider_numerator) / channel->divider_denominator);
}

esp_err_t board_adc_init(board_adc_context_t *ctx)
{
    return board_adc_init_mask(ctx, UINT32_MAX);
}

void board_adc_deinit(board_adc_context_t *ctx)
{
    if (ctx == NULL) return;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (ctx->cali_adc1 != NULL) (void)adc_cali_delete_scheme_curve_fitting((adc_cali_handle_t)ctx->cali_adc1);
#endif
    if (ctx->adc1 != NULL) (void)adc_oneshot_del_unit(ctx->adc1);
    memset(ctx, 0, sizeof(*ctx));
}

esp_err_t board_adc_init_mask(board_adc_context_t *ctx, uint32_t gpio_mask)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(ctx, 0, sizeof(*ctx));
    adc_oneshot_unit_init_cfg_t init_cfg = {.unit_id = ADC_UNIT_1};
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &ctx->adc1);
    if (err != ESP_OK) {
        return err;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    for (size_t i = 0; i < sizeof(s_adc_channels) / sizeof(s_adc_channels[0]); ++i) {
        if ((gpio_mask & (1u << s_adc_channels[i].gpio)) == 0) continue;
        err = adc_oneshot_config_channel(ctx->adc1, s_adc_channels[i].channel, &chan_cfg);
        if (err != ESP_OK) {
            board_adc_deinit(ctx);
            return err;
        }
        ctx->gpio_mask |= 1u << s_adc_channels[i].gpio;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {.unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, (adc_cali_handle_t *)&ctx->cali_adc1) == ESP_OK) {
        ctx->calibration_enabled = true;
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable; voltage facts will fail closed");
    }
#else
    ESP_LOGW(TAG, "ADC calibration scheme unavailable; voltage facts will fail closed");
#endif
    ctx->initialized = true;
    return ESP_OK;
}

esp_err_t board_adc_read_mv(board_adc_context_t *ctx,
                            const board_adc_channel_desc_t *channel,
                            board_adc_sample_t *out_sample)
{
    if (ctx == NULL || channel == NULL || out_sample == NULL || !ctx->initialized ||
        channel->gpio < 0 || channel->gpio >= 32 || (ctx->gpio_mask & (1u << channel->gpio)) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_sample, 0, sizeof(*out_sample));
    int raw = 0;
    esp_err_t err = adc_oneshot_read(ctx->adc1, channel->channel, &raw);
    if (err != ESP_OK) {
        return err;
    }
    if (!ctx->calibration_enabled || ctx->cali_adc1 == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    int measured_mv = 0;
    err = adc_cali_raw_to_voltage((adc_cali_handle_t)ctx->cali_adc1, raw, &measured_mv);
    if (err != ESP_OK) {
        return err;
    }
    out_sample->channel = channel;
    out_sample->raw = raw;
    out_sample->voltage_mv = board_adc_apply_divider_mv(measured_mv, channel);
    return ESP_OK;
}
