#include "board_i2s.h"

#include "board_audio_clock.h"
#include "board_sticks3.h"

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *TAG = "BOARD_I2S";

static i2s_chan_handle_t s_rx_handle;
static i2s_chan_handle_t s_tx_handle;
static board_audio_profile_t s_active_profile;
static bool s_i2s_ready;


static size_t board_i2s_decode_mono16_from_32slot(const int32_t *src, size_t words, int16_t *dst, size_t max_samples)
{
    size_t out = 0;
    for (size_t i = 0; i < words && out < max_samples; ++i) {
        dst[out++] = (int16_t)(src[i] >> 16);
    }
    return out;
}


static void board_i2s_cleanup_partial(bool rx_enabled, bool tx_enabled)
{
    if (s_tx_handle != NULL) {
        if (tx_enabled) {
            (void)i2s_channel_disable(s_tx_handle);
        }
        (void)i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }
    if (s_rx_handle != NULL) {
        if (rx_enabled) {
            (void)i2s_channel_disable(s_rx_handle);
        }
        (void)i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
    }
    s_i2s_ready = false;
}

static esp_err_t board_i2s_enable_channel(i2s_chan_handle_t handle, const char *name)
{
    (void)name;
    esp_err_t err = i2s_channel_enable(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S %s channel enable failed: %s", name, esp_err_to_name(err));
    }
    return err;
}

esp_err_t board_i2s_init_profile(board_audio_profile_t profile)
{
    if (profile != BOARD_AUDIO_PROFILE_CAPTURE_ONLY &&
        profile != BOARD_AUDIO_PROFILE_PLAYBACK_ONLY &&
        profile != BOARD_AUDIO_PROFILE_SIMULTANEOUS_MIC_SPEAKER) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_i2s_ready) {
        return (s_active_profile == profile) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    s_rx_handle = NULL;
    s_tx_handle = NULL;
    bool rx_enabled = false;
    bool tx_enabled = false;

    const board_audio_clock_profile_t *clock = board_audio_clock_get_profile();
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_I2S_PORT, I2S_ROLE_MASTER);
    i2s_chan_handle_t *tx_out = (profile == BOARD_AUDIO_PROFILE_CAPTURE_ONLY) ? NULL : &s_tx_handle;
    i2s_chan_handle_t *rx_out = (profile == BOARD_AUDIO_PROFILE_PLAYBACK_ONLY) ? NULL : &s_rx_handle;
    esp_err_t err = i2s_new_channel(&chan_cfg, tx_out, rx_out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel allocation failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(clock->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK_IO,
            .bclk = BOARD_I2S_BCK_IO,
            .ws = BOARD_I2S_WS_IO,
            .dout = (profile == BOARD_AUDIO_PROFILE_CAPTURE_ONLY) ? I2S_GPIO_UNUSED : BOARD_I2S_DO_IO,
            .din = (profile == BOARD_AUDIO_PROFILE_PLAYBACK_ONLY) ? I2S_GPIO_UNUSED : BOARD_I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_768;
    /* Standard mode clocks two slots even for mono: 16 kHz * 2 * 32 bits
     * gives 1.024 MHz BCLK. DMA payload remains 16-bit mono. */
    std_cfg.slot_cfg.slot_bit_width = (i2s_slot_bit_width_t)clock->slot_bits;

    if (s_rx_handle != NULL) {
        err = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S RX standard-mode init failed: %s", esp_err_to_name(err));
            board_i2s_cleanup_partial(rx_enabled, tx_enabled);
            return err;
        }
    }
    if (s_tx_handle != NULL) {
        err = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S TX standard-mode init failed: %s", esp_err_to_name(err));
            board_i2s_cleanup_partial(rx_enabled, tx_enabled);
            return err;
        }
    }

    if (s_rx_handle != NULL) {
        err = board_i2s_enable_channel(s_rx_handle, "RX");
        if (err != ESP_OK) {
            board_i2s_cleanup_partial(rx_enabled, tx_enabled);
            return err;
        }
        rx_enabled = true;
    }
    if (s_tx_handle != NULL) {
        err = board_i2s_enable_channel(s_tx_handle, "TX");
        if (err != ESP_OK) {
            board_i2s_cleanup_partial(rx_enabled, tx_enabled);
            return err;
        }
        tx_enabled = true;
    }

    s_active_profile = profile;
    s_i2s_ready = true;
    ESP_LOGI(TAG, "I2S standard %s profile ready: Fs=%d MCLK=%d BCLK=%d LRCK=%d bits=%d channels=%d slot_bits=%d",
             profile == BOARD_AUDIO_PROFILE_PLAYBACK_ONLY ? "playback-only" :
             (profile == BOARD_AUDIO_PROFILE_SIMULTANEOUS_MIC_SPEAKER ? "simultaneous-mic-speaker" : "capture-only"),
             clock->sample_rate_hz, clock->mclk_hz, clock->bclk_hz, clock->lrck_hz,
             clock->bits_per_sample, clock->channels, (int)std_cfg.slot_cfg.slot_bit_width);
    if (profile == BOARD_AUDIO_PROFILE_CAPTURE_ONLY) {
        ESP_LOGI(TAG, "I2S pins: MCLK=GPIO%d BCLK=GPIO%d WS=GPIO%d DIN=GPIO%d DOUT=unused",
                 BOARD_I2S_MCLK_IO, BOARD_I2S_BCK_IO, BOARD_I2S_WS_IO, BOARD_I2S_DI_IO);
    } else if (profile == BOARD_AUDIO_PROFILE_PLAYBACK_ONLY) {
        ESP_LOGI(TAG, "I2S pins: MCLK=GPIO%d BCLK=GPIO%d WS=GPIO%d DIN=unused DOUT=GPIO%d",
                 BOARD_I2S_MCLK_IO, BOARD_I2S_BCK_IO, BOARD_I2S_WS_IO, BOARD_I2S_DO_IO);
    } else {
        ESP_LOGI(TAG, "I2S pins: MCLK=GPIO%d BCLK=GPIO%d WS=GPIO%d DIN=GPIO%d DOUT=GPIO%d",
                 BOARD_I2S_MCLK_IO, BOARD_I2S_BCK_IO, BOARD_I2S_WS_IO, BOARD_I2S_DI_IO, BOARD_I2S_DO_IO);
    }
    return ESP_OK;
}

esp_err_t board_i2s_deinit(void)
{
    const bool rx_enabled = s_i2s_ready && s_rx_handle != NULL;
    const bool tx_enabled = s_i2s_ready && s_tx_handle != NULL;
    board_i2s_cleanup_partial(rx_enabled, tx_enabled);
    s_active_profile = BOARD_AUDIO_PROFILE_CAPTURE_ONLY;
    ESP_LOGI(TAG, "I2S channels released");
    return ESP_OK;
}

esp_err_t board_i2s_read(void *dest, size_t size, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!s_i2s_ready || s_rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_read(s_rx_handle, dest, size, bytes_read, timeout_ms);
}


esp_err_t board_i2s_read_mono_i16(int16_t *dest, size_t max_samples, size_t *samples_read, uint32_t timeout_ms)
{
    int32_t raw_words[256];

    if (dest == NULL || max_samples == 0 || samples_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *samples_read = 0;

    const size_t words_to_read = (max_samples < (sizeof(raw_words) / sizeof(raw_words[0])))
                                 ? max_samples
                                 : (sizeof(raw_words) / sizeof(raw_words[0]));
    size_t bytes_read = 0;
    esp_err_t err = board_i2s_read(raw_words, words_to_read * sizeof(raw_words[0]), &bytes_read, timeout_ms);
    if (err != ESP_OK) {
        return err;
    }

    const size_t words = bytes_read / sizeof(raw_words[0]);
    *samples_read = board_i2s_decode_mono16_from_32slot(raw_words, words, dest, max_samples);
    return *samples_read > 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t board_i2s_write(const void *src, size_t size, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!s_i2s_ready || s_tx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_write(s_tx_handle, src, size, bytes_written, timeout_ms);
}
