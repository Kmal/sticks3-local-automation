#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t register_bus_write(i2c_port_t port, uint8_t dev_addr, uint8_t reg, const uint8_t *data, size_t len);
esp_err_t register_bus_probe(i2c_port_t port, uint8_t dev_addr);
esp_err_t register_bus_write_u8(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t value);
esp_err_t register_bus_read_u8(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *value);
esp_err_t register_bus_read(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *data, size_t len);
esp_err_t register_bus_read_u8_quiet(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *value);
esp_err_t register_bus_update_u8(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t mask, uint8_t value);

#ifdef __cplusplus
}
#endif
