#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "driver/i2s_types.h"
typedef void *i2s_chan_handle_t;
typedef int i2s_slot_bit_width_t;
#define I2S_ROLE_MASTER 1
#define I2S_GPIO_UNUSED (-1)
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_MONO 1
#define I2S_MCLK_MULTIPLE_768 768
#define I2S_SLOT_BIT_WIDTH_32BIT 32
#define I2S_CHANNEL_DEFAULT_CONFIG(port_value, role_value) ((i2s_chan_config_t){.id=(port_value), .role=(role_value)})
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) ((i2s_std_clk_config_t){.sample_rate_hz=(rate)})
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, mode) ((i2s_std_slot_config_t){.data_bit_width=(width), .slot_mode=(mode)})
typedef struct { int id; int role; } i2s_chan_config_t;
typedef struct { int sample_rate_hz; int mclk_multiple; } i2s_std_clk_config_t;
typedef struct { int data_bit_width; int slot_mode; int slot_bit_width; } i2s_std_slot_config_t;
typedef struct { int mclk; int bclk; int ws; int dout; int din; struct { bool mclk_inv; bool bclk_inv; bool ws_inv; } invert_flags; } i2s_std_gpio_config_t;
typedef struct { i2s_std_clk_config_t clk_cfg; i2s_std_slot_config_t slot_cfg; i2s_std_gpio_config_t gpio_cfg; } i2s_std_config_t;

esp_err_t i2s_new_channel(const i2s_chan_config_t *chan_cfg, i2s_chan_handle_t *tx_handle, i2s_chan_handle_t *rx_handle);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t handle, const i2s_std_config_t *std_cfg);
esp_err_t i2s_channel_enable(i2s_chan_handle_t handle);
esp_err_t i2s_channel_disable(i2s_chan_handle_t handle);
esp_err_t i2s_del_channel(i2s_chan_handle_t handle);
esp_err_t i2s_channel_read(i2s_chan_handle_t handle, void *dest, size_t size, size_t *bytes_read, uint32_t timeout_ms);
esp_err_t i2s_channel_write(i2s_chan_handle_t handle, const void *src, size_t size, size_t *bytes_written, uint32_t timeout_ms);
