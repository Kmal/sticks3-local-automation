#include "board_adc.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static const board_adc_channel_desc_t *find_channel(const char *source_key)
{
    size_t count = 0;
    const board_adc_channel_desc_t *channels = board_adc_channels(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(channels[i].source_key, source_key) == 0) {
            return &channels[i];
        }
    }
    return NULL;
}

static int contains_gpio(gpio_num_t gpio)
{
    size_t count = 0;
    const board_adc_channel_desc_t *channels = board_adc_channels(&count);
    for (size_t i = 0; i < count; ++i) {
        if (channels[i].gpio == gpio) {
            return 1;
        }
    }
    return 0;
}

static void test_safe_channel_allowlist(void)
{
    assert(find_channel("grove.g9") != NULL);
    assert(find_channel("grove.g10") != NULL);
    assert(find_channel("hat.g4") != NULL);
    assert(find_channel("hat.g5") != NULL);
    assert(find_channel("hat.g6") != NULL);
    assert(find_channel("hat.g7") != NULL);
    assert(find_channel("hat.g8") != NULL);
}

static void test_internal_and_risky_pins_are_excluded(void)
{
    assert(!contains_gpio(GPIO_NUM_1));
    assert(!contains_gpio(GPIO_NUM_2));
    assert(!contains_gpio(GPIO_NUM_3));
    assert(!contains_gpio(GPIO_NUM_11));
    assert(!contains_gpio(GPIO_NUM_12));
    assert(!contains_gpio(GPIO_NUM_19));
    assert(!contains_gpio(GPIO_NUM_20));
    assert(!contains_gpio(GPIO_NUM_38));
    assert(!contains_gpio(GPIO_NUM_39));
    assert(!contains_gpio(GPIO_NUM_40));
    assert(!contains_gpio(GPIO_NUM_41));
    assert(!contains_gpio(GPIO_NUM_42));
    assert(!contains_gpio(GPIO_NUM_43));
    assert(!contains_gpio(GPIO_NUM_44));
    assert(!contains_gpio(GPIO_NUM_45));
    assert(!contains_gpio(GPIO_NUM_46));
    assert(!contains_gpio(GPIO_NUM_47));
    assert(!contains_gpio(GPIO_NUM_48));
}

static void test_divider_scaling(void)
{
    const board_adc_channel_desc_t channel = {
        .divider_numerator = 2,
        .divider_denominator = 1,
    };
    assert(board_adc_apply_divider_mv(1000, &channel) == 2000);
    assert(board_adc_apply_divider_mv(1000, NULL) == 1000);
}

static void test_null_validation_and_read(void)
{
    assert(board_adc_init(NULL) == ESP_ERR_INVALID_ARG);
    board_adc_context_t ctx;
    assert(board_adc_init(&ctx) == ESP_OK);

    board_adc_sample_t sample;
    const board_adc_channel_desc_t *channel = find_channel("grove.g9");
    assert(channel != NULL);
    assert(board_adc_read_mv(NULL, channel, &sample) == ESP_ERR_INVALID_ARG);
    assert(board_adc_read_mv(&ctx, NULL, &sample) == ESP_ERR_INVALID_ARG);
    assert(board_adc_read_mv(&ctx, channel, NULL) == ESP_ERR_INVALID_ARG);
    assert(board_adc_read_mv(&ctx, channel, &sample) == ESP_OK);
    assert(sample.channel == channel);

    ctx.calibration_enabled = false;
    ctx.cali_adc1 = NULL;
    assert(board_adc_read_mv(&ctx, channel, &sample) == ESP_ERR_NOT_SUPPORTED);
}

static void test_selected_channel_ownership(void)
{
    board_adc_context_t context;
    board_adc_sample_t sample;
    assert(board_adc_init_mask(&context, 1u << 9) == ESP_OK);
    assert(context.gpio_mask == (1u << 9));
    assert(board_adc_read_mv(&context, find_channel("grove.g9"), &sample) == ESP_OK);
    assert(board_adc_read_mv(&context, find_channel("grove.g10"), &sample) == ESP_ERR_INVALID_ARG);
    board_adc_deinit(&context);
    assert(!context.initialized && context.adc1 == NULL);
}

int main(void)
{
    test_selected_channel_ownership();
    test_safe_channel_allowlist();
    test_internal_and_risky_pins_are_excluded();
    test_divider_scaling();
    test_null_validation_and_read();
    return 0;
}
