#pragma once
#include "esp_err.h"
typedef int adc_unit_t;
typedef int adc_channel_t;
typedef void *adc_oneshot_unit_handle_t;
static inline esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t unit) { (void)unit; return ESP_OK; }
#define ADC_UNIT_1 1
#define ADC_CHANNEL_3 3
#define ADC_CHANNEL_4 4
#define ADC_CHANNEL_5 5
#define ADC_CHANNEL_6 6
#define ADC_CHANNEL_7 7
#define ADC_CHANNEL_8 8
#define ADC_CHANNEL_9 9
#define ADC_ATTEN_DB_12 12
#define ADC_BITWIDTH_DEFAULT 0
typedef struct { adc_unit_t unit_id; } adc_oneshot_unit_init_cfg_t;
typedef struct { int atten; int bitwidth; } adc_oneshot_chan_cfg_t;
static inline esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *cfg, adc_oneshot_unit_handle_t *ret_unit) { (void)cfg; *ret_unit=(void*)1; return ESP_OK; }
static inline esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t unit, adc_channel_t channel, const adc_oneshot_chan_cfg_t *cfg) { (void)unit; (void)channel; (void)cfg; return ESP_OK; }
static inline esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t unit, adc_channel_t channel, int *out_raw) { (void)unit; *out_raw=1000 + channel; return ESP_OK; }
