#include "action_hat.h"
#include "action_http.h"
#include "action_ir.h"
#include "action_speaker.h"
#include "rule_web.h"

#include <stdio.h>
#include <stdlib.h>

#define ASSERT_TRUE(value) do { if (!(value)) { fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #value); exit(1); } } while (0)
#define ASSERT_FALSE(value) ASSERT_TRUE(!(value))
#define ASSERT_EQ(expected, actual) do { if ((expected) != (actual)) { fprintf(stderr, "%s:%d expected %d got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); exit(1); } } while (0)

static void test_http_json_and_not_ready(void)
{
    rule_event_t event = {.sequence = 1, .uptime_ms = 2, .rule_id = 3, .source = RULE_SOURCE_KEY1_SHORT, .action = RULE_ACTION_HTTP_POST, .fire_count = 4};
    char json[160];
    ASSERT_TRUE(rule_event_to_json(&event, json, sizeof(json)));
    rule_action_t action = {.type = RULE_ACTION_HTTP_POST, .timeout_ms = 1000};
    snprintf(action.http_url, sizeof(action.http_url), "https://example.invalid/hook");
    ASSERT_FALSE(action_http_network_ready());
    ASSERT_EQ(ACTION_HTTP_RESULT_NOT_READY, action_http_post_event(&action, &event));
    action_http_set_network_ready(true);
    ASSERT_TRUE(action_http_network_ready());
    ASSERT_EQ(ACTION_HTTP_RESULT_NOT_READY, action_http_post_event(&action, &event));
    action_http_set_network_ready(false);
}

typedef struct {
    int init_calls;
    int deinit_calls;
    int amp_enable_calls;
    int amp_disable_calls;
    size_t bytes_written;
} fake_speaker_ctx_t;

static esp_err_t fake_speaker_init(void *ctx)
{
    fake_speaker_ctx_t *fake = (fake_speaker_ctx_t *)ctx;
    fake->init_calls++;
    return ESP_OK;
}

static esp_err_t fake_speaker_deinit(void *ctx)
{
    fake_speaker_ctx_t *fake = (fake_speaker_ctx_t *)ctx;
    fake->deinit_calls++;
    return ESP_OK;
}

static esp_err_t fake_speaker_amp_set(bool enable, void *ctx)
{
    fake_speaker_ctx_t *fake = (fake_speaker_ctx_t *)ctx;
    if (enable) {
        fake->amp_enable_calls++;
    } else {
        fake->amp_disable_calls++;
    }
    return ESP_OK;
}

static esp_err_t fake_speaker_write(const void *src, size_t size, size_t *bytes_written, uint32_t timeout_ms, void *ctx)
{
    (void)src;
    (void)timeout_ms;
    fake_speaker_ctx_t *fake = (fake_speaker_ctx_t *)ctx;
    fake->bytes_written += size;
    if (bytes_written != NULL) {
        *bytes_written = size;
    }
    return ESP_OK;
}

static void test_speaker_tone_action_validation_and_playback(void)
{
    rule_action_t action = {
        .type = RULE_ACTION_SPEAKER_TONE,
        .speaker_frequency_hz = 7000,
        .speaker_duration_ms = 100,
        .speaker_volume_percent = 50,
        .timeout_ms = 100,
    };
    action_speaker_config_t config;
    ASSERT_TRUE(action_speaker_config_from_action(&action, &config));
    ASSERT_EQ(7000, config.frequency_hz);

    action.speaker_volume_percent = 75;
    ASSERT_FALSE(action_speaker_config_from_action(&action, &config));
    action.speaker_volume_percent = 50;
    ASSERT_TRUE(action_speaker_config_from_action(&action, &config));

    fake_speaker_ctx_t fake = {0};
    const action_speaker_ops_t ops = {
        .audio_init_playback = fake_speaker_init,
        .audio_deinit = fake_speaker_deinit,
        .speaker_amp_set = fake_speaker_amp_set,
        .write_pcm = fake_speaker_write,
        .ctx = &fake,
    };
    ASSERT_EQ(ESP_OK, action_speaker_play_tone_with_ops(&config, &ops));
    ASSERT_EQ(1, fake.init_calls);
    ASSERT_EQ(1, fake.deinit_calls);
    ASSERT_EQ(1, fake.amp_enable_calls);
    ASSERT_EQ(1, fake.amp_disable_calls);
    ASSERT_TRUE(fake.bytes_written >= 1600u * sizeof(int32_t));
}

static void test_ir_and_hat_are_bounded_or_disabled(void)
{
    action_ir_config_t ir = {.protocol = RULE_IR_PROTOCOL_NEC, .carrier_hz = 38000, .repeat_count = 2, .timeout_ms = 250};
    ASSERT_TRUE(action_ir_validate(&ir));
    ir.repeat_count = 99;
    ASSERT_FALSE(action_ir_validate(&ir));
    ASSERT_FALSE(hat_operation_supported(RULE_HAT_OPERATION_RELAY_SET));
}

static void test_nec_repeat_period_and_payload(void)
{
    action_ir_config_t config = {.protocol=RULE_IR_PROTOCOL_NEC, .carrier_hz=38000, .timeout_ms=250, .address=0x59, .command=0x16};
    action_ir_symbol_t symbols[35];
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        size_t n = action_ir_encode_nec(&config, repeat != 0, symbols, 35);
        ASSERT_EQ(repeat ? 3 : 35, n);
        unsigned duration = 0;
        for (size_t i = 0; i < n; ++i) {
            ASSERT_TRUE(symbols[i].high_us <= 32767 && symbols[i].low_us <= 32767);
            duration += symbols[i].high_us + symbols[i].low_us;
        }
        ASSERT_EQ(110000, duration);
        ASSERT_EQ(9000, symbols[0].high_us);
        ASSERT_EQ(repeat ? 2250 : 4500, symbols[0].low_us);
        if (!repeat) {
            uint32_t payload = 0;
            for (size_t i = 0; i < 32; ++i) if (symbols[i+1].low_us == 1690) payload |= 1u << i;
            ASSERT_TRUE(payload == 0xe916a659u);
        }
    }
    ASSERT_EQ(0, action_ir_encode_nec(&config, false, symbols, 34));
}

int main(void)
{
    test_nec_repeat_period_and_payload();
    test_http_json_and_not_ready();
    test_ir_and_hat_are_bounded_or_disabled();
    test_speaker_tone_action_validation_and_playback();
    puts("action_modules tests passed");
    return 0;
}
