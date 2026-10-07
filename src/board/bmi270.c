#include "bmi270.h"

#include "register_bus.h"
#include "bmi270_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(bmi270_config_file) == 8192, "BMI270 configuration size");

#define BMI270_REG_CHIP_ID 0x00
#define BMI270_CHIP_ID 0x24
#define BMI270_REG_ACC_CONF 0x40
#define BMI270_REG_ACC_RANGE 0x41
#define BMI270_REG_PWR_CONF 0x7c
#define BMI270_REG_PWR_CTRL 0x7d
#define BMI270_REG_DATA_8 0x0c
#define BMI270_ACC_EN 0x04
#define BMI270_PWR_CONF_NORMAL 0x02
#define BMI270_ACC_CONF_ODR_100HZ_BWP_NORMAL_AVG4 0xa8
#define BMI270_ACC_RANGE_2G 0x00

static int16_t bmi270_i16_le(uint8_t lo, uint8_t hi)
{
    return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
}

static int16_t bmi270_lsb_to_mg(int16_t raw)
{
    return (int16_t)((int32_t)raw * 2000 / 32768);
}

esp_err_t bmi270_init(i2c_port_t port, uint8_t addr)
{
    uint8_t chip_id = 0;
    esp_err_t err = register_bus_read_u8(port, addr, BMI270_REG_CHIP_ID, &chip_id);
    if (err != ESP_OK) {
        return err;
    }
    if (chip_id != BMI270_CHIP_ID) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    /* Bosch datasheet section 4.4: initialize after every POR/soft reset.
     * Chunk addresses are WORD offsets, not incrementing register addresses. */
    err = register_bus_write_u8(port, addr, 0x7e, 0xb6);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(5) + 1);
    err = register_bus_write_u8(port, addr, BMI270_REG_PWR_CONF, 0x00);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(1) + 1); /* >=450 us even with coarse RTOS ticks */
    err = register_bus_write_u8(port, addr, 0x59, 0x00);
    if (err != ESP_OK) return err;
    for (size_t offset = 0; offset < sizeof(bmi270_config_file); offset += 32) {
        uint8_t address[2] = {(uint8_t)((offset / 2) & 0x0f), (uint8_t)(offset >> 5)};
        err = register_bus_write(port, addr, 0x5b, address, sizeof(address));
        if (err != ESP_OK) return err;
        err = register_bus_write(port, addr, 0x5e, bmi270_config_file + offset, 32);
        if (err != ESP_OK) return err;
    }
    err = register_bus_write_u8(port, addr, 0x59, 0x01);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20) + 1);
    uint8_t status = 0;
    err = register_bus_read_u8(port, addr, 0x21, &status);
    if (err != ESP_OK) return err;
    if ((status & 0x0f) != 0x01) return ESP_ERR_INVALID_RESPONSE;
    err = register_bus_write_u8(port, addr, BMI270_REG_PWR_CONF, BMI270_PWR_CONF_NORMAL);
    if (err != ESP_OK) return err;
    err = register_bus_write_u8(port, addr, BMI270_REG_ACC_CONF, BMI270_ACC_CONF_ODR_100HZ_BWP_NORMAL_AVG4);
    if (err != ESP_OK) {
        return err;
    }
    err = register_bus_write_u8(port, addr, BMI270_REG_ACC_RANGE, BMI270_ACC_RANGE_2G);
    if (err != ESP_OK) {
        return err;
    }
    return register_bus_write_u8(port, addr, BMI270_REG_PWR_CTRL, BMI270_ACC_EN);
}

esp_err_t bmi270_read_accel(i2c_port_t port, uint8_t addr, bmi270_accel_sample_t *out_sample)
{
    if (out_sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_sample, 0, sizeof(*out_sample));
    uint8_t data[6] = {0};
    esp_err_t err = register_bus_read(port, addr, BMI270_REG_DATA_8, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }
    out_sample->x_mg = bmi270_lsb_to_mg(bmi270_i16_le(data[0], data[1]));
    out_sample->y_mg = bmi270_lsb_to_mg(bmi270_i16_le(data[2], data[3]));
    out_sample->z_mg = bmi270_lsb_to_mg(bmi270_i16_le(data[4], data[5]));
    return ESP_OK;
}

esp_err_t bmi270_motion_state_init(bmi270_motion_state_t *state,
                                   const bmi270_motion_config_t *config)
{
    if (state == NULL || config == NULL || config->still_hysteresis_mg >= config->motion_delta_mg) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(state, 0, sizeof(*state));
    state->config = *config;
    return ESP_OK;
}

esp_err_t bmi270_motion_poll(bmi270_motion_state_t *state,
                             i2c_port_t port,
                             uint8_t addr,
                             uint32_t uptime_ms,
                             bool *out_motion)
{
    if (state == NULL || out_motion == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state->initialized && state->config.min_interval_ms > 0 &&
        uptime_ms - state->last_sample.sample_time_ms < state->config.min_interval_ms) {
        *out_motion = state->last_motion;
        return ESP_OK;
    }
    bmi270_accel_sample_t sample = {0};
    esp_err_t err = bmi270_read_accel(port, addr, &sample);
    if (err != ESP_OK) {
        return err;
    }
    sample.sample_time_ms = uptime_ms;
    if (!state->initialized) {
        state->initialized = true;
        state->last_sample = sample;
        state->last_motion = false;
        *out_motion = false;
        return ESP_OK;
    }
    int dx = abs((int)sample.x_mg - (int)state->last_sample.x_mg);
    int dy = abs((int)sample.y_mg - (int)state->last_sample.y_mg);
    int dz = abs((int)sample.z_mg - (int)state->last_sample.z_mg);
    const int threshold = state->config.motion_delta_mg;
    const int clear_threshold = threshold - state->config.still_hysteresis_mg;
    bool motion = state->last_motion;
    if (dx >= threshold || dy >= threshold || dz >= threshold) {
        motion = true;
    } else if (dx < clear_threshold && dy < clear_threshold && dz < clear_threshold) {
        motion = false;
    }
    state->last_sample = sample;
    state->last_motion = motion;
    *out_motion = motion;
    return ESP_OK;
}
