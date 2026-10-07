#include "register_bus.h"

#include "board_i2c.h"
#include "board_sticks3.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <stdbool.h>
#include <stddef.h>

#define REGISTER_BUS_TIMEOUT_MS 1000
#define REGISTER_BUS_MAX_DEVICES 8

static uint32_t register_bus_device_speed_hz(uint8_t dev_addr)
{
    /*
     * M5PM1 powers up in 100 kHz I2C mode. Keep its handle at that rate
     * until a dedicated M5PM1 initialization path explicitly changes both
     * the PMIC configuration and the host device speed.
     */
    return (dev_addr == BOARD_M5PM1_ADDR) ? BOARD_M5PM1_I2C_CLK_HZ : BOARD_I2C_CLK_HZ;
}

static const char *TAG = "REG_BUS";

typedef struct {
    bool in_use;
    i2c_port_t port;
    uint8_t dev_addr;
    i2c_master_dev_handle_t handle;
} register_bus_device_t;

static register_bus_device_t s_devices[REGISTER_BUS_MAX_DEVICES];

static esp_err_t register_bus_get_device(i2c_port_t port, uint8_t dev_addr, i2c_master_dev_handle_t *out_handle)
{
    if (out_handle == NULL || dev_addr > 0x7f) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = board_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < REGISTER_BUS_MAX_DEVICES; ++i) {
        if (s_devices[i].in_use && s_devices[i].port == port && s_devices[i].dev_addr == dev_addr) {
            *out_handle = s_devices[i].handle;
            return ESP_OK;
        }
    }

    i2c_master_bus_handle_t bus_handle = NULL;
    err = i2c_master_get_bus_handle(port, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus handle lookup failed for port %d: %s", (int)port, esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = register_bus_device_speed_hz(dev_addr),
    };

    for (size_t i = 0; i < REGISTER_BUS_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_devices[i].handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "I2C add dev 0x%02x failed: %s", dev_addr, esp_err_to_name(err));
                return err;
            }
            s_devices[i].in_use = true;
            s_devices[i].port = port;
            s_devices[i].dev_addr = dev_addr;
            *out_handle = s_devices[i].handle;
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "I2C device cache exhausted while adding dev 0x%02x", dev_addr);
    return ESP_ERR_NO_MEM;
}

esp_err_t register_bus_probe(i2c_port_t port, uint8_t dev_addr)
{
    if (dev_addr > 0x7f) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = board_i2c_init();
    if (err != ESP_OK) {
        return err;
    }
    i2c_master_bus_handle_t bus_handle = NULL;
    err = i2c_master_get_bus_handle(port, &bus_handle);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_master_probe(bus_handle, dev_addr, REGISTER_BUS_TIMEOUT_MS);
}

/* Bounded burst writes for sensor configuration uploads. */
esp_err_t register_bus_write(i2c_port_t port, uint8_t dev_addr, uint8_t reg, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || len > 32) return ESP_ERR_INVALID_ARG;
    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err = register_bus_get_device(port, dev_addr, &dev_handle);
    if (err != ESP_OK) return err;
    uint8_t tx[33];
    tx[0] = reg;
    for (size_t i = 0; i < len; ++i) tx[i + 1] = data[i];
    return i2c_master_transmit(dev_handle, tx, len + 1, REGISTER_BUS_TIMEOUT_MS);
}

esp_err_t register_bus_write_u8(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t value)
{
    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err = register_bus_get_device(port, dev_addr, &dev_handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t tx_buf[2] = {reg, value};
    err = i2c_master_transmit(dev_handle, tx_buf, sizeof(tx_buf), REGISTER_BUS_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write dev 0x%02x reg 0x%02x failed: %s", dev_addr, reg, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t register_bus_read_u8_impl(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *value, bool log_error)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err = register_bus_get_device(port, dev_addr, &dev_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_transmit_receive(dev_handle, &reg, 1, value, 1, REGISTER_BUS_TIMEOUT_MS);
    if (err != ESP_OK && log_error) {
        ESP_LOGE(TAG, "I2C read dev 0x%02x reg 0x%02x failed: %s", dev_addr, reg, esp_err_to_name(err));
    }
    return err;
}

esp_err_t register_bus_read_u8(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *value)
{
    return register_bus_read_u8_impl(port, dev_addr, reg, value, true);
}

esp_err_t register_bus_read(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err = register_bus_get_device(port, dev_addr, &dev_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit_receive(dev_handle, &reg, 1, data, len, REGISTER_BUS_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read dev 0x%02x reg 0x%02x len %u failed: %s", dev_addr, reg, (unsigned)len, esp_err_to_name(err));
    }
    return err;
}

esp_err_t register_bus_read_u8_quiet(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *value)
{
    return register_bus_read_u8_impl(port, dev_addr, reg, value, false);
}

esp_err_t register_bus_update_u8(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    esp_err_t err = register_bus_read_u8(port, dev_addr, reg, &current);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t next = (current & (uint8_t)~mask) | (value & mask);
    return register_bus_write_u8(port, dev_addr, reg, next);
}
