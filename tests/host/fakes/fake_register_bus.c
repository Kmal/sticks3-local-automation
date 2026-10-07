#include "fake_register_bus.h"
#include "register_bus.h"

#include <stdbool.h>
#include <string.h>

#define MAX_OPS 12000
static uint8_t s_regs[128][256];
static fake_bus_op_t s_ops[MAX_OPS];
static size_t s_op_count;
static esp_err_t s_fail_next_read;
static esp_err_t s_fail_next_write;
static bool s_fail_reg_read_enabled;
static uint8_t s_fail_reg_read_addr;
static uint8_t s_fail_reg_read_reg;
static esp_err_t s_fail_reg_read_err;

void fake_register_bus_reset(void)
{
    memset(s_regs, 0, sizeof(s_regs));
    memset(s_ops, 0, sizeof(s_ops));
    s_op_count = 0;
    s_fail_next_read = ESP_OK;
    s_fail_next_write = ESP_OK;
    s_fail_reg_read_enabled = false;
    s_fail_reg_read_addr = 0;
    s_fail_reg_read_reg = 0;
    s_fail_reg_read_err = ESP_OK;
}

void fake_register_bus_set_reg(uint8_t addr, uint8_t reg, uint8_t value) { s_regs[addr & 0x7f][reg] = value; }
void fake_register_bus_fail_next_read(esp_err_t err) { s_fail_next_read = err; }
void fake_register_bus_fail_next_write(esp_err_t err) { s_fail_next_write = err; }
void fake_register_bus_fail_reg_read_once(uint8_t addr, uint8_t reg, esp_err_t err)
{
    s_fail_reg_read_enabled = true;
    s_fail_reg_read_addr = addr;
    s_fail_reg_read_reg = reg;
    s_fail_reg_read_err = err;
}
size_t fake_register_bus_op_count(void) { return s_op_count; }
const fake_bus_op_t *fake_register_bus_op(size_t index) { return index < s_op_count ? &s_ops[index] : NULL; }

int fake_register_bus_has_write(uint8_t addr, uint8_t reg, uint8_t value)
{
    for (size_t i = 0; i < s_op_count; ++i) {
        if (s_ops[i].type == FAKE_BUS_OP_WRITE && s_ops[i].addr == addr && s_ops[i].reg == reg && s_ops[i].value == value) {
            return 1;
        }
    }
    return 0;
}

static void record(fake_bus_op_type_t type, uint8_t addr, uint8_t reg, uint8_t value)
{
    if (s_op_count < MAX_OPS) {
        s_ops[s_op_count++] = (fake_bus_op_t){.type = type, .addr = addr, .reg = reg, .value = value};
    }
}

esp_err_t register_bus_write(i2c_port_t port, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || len > 32) return ESP_ERR_INVALID_ARG;
    /* INIT_DATA is a FIFO; do not overwrite adjacent simulated registers. */
    for (size_t i = 0; i < len; ++i) {
        esp_err_t err = register_bus_write_u8(port, addr, reg == 0x5e ? reg : (uint8_t)(reg + i), data[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t register_bus_write_u8(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t value)
{
    (void)port;
    if (s_fail_next_write != ESP_OK) {
        esp_err_t err = s_fail_next_write;
        s_fail_next_write = ESP_OK;
        return err;
    }
    record(FAKE_BUS_OP_WRITE, dev_addr, reg, value);
    s_regs[dev_addr & 0x7f][reg] = value;
    return ESP_OK;
}

esp_err_t register_bus_read_u8(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *value)
{
    (void)port;
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_next_read != ESP_OK) {
        esp_err_t err = s_fail_next_read;
        s_fail_next_read = ESP_OK;
        return err;
    }
    if (s_fail_reg_read_enabled && s_fail_reg_read_addr == dev_addr && s_fail_reg_read_reg == reg) {
        esp_err_t err = s_fail_reg_read_err;
        s_fail_reg_read_enabled = false;
        s_fail_reg_read_err = ESP_OK;
        return err;
    }
    *value = s_regs[dev_addr & 0x7f][reg];
    record(FAKE_BUS_OP_READ, dev_addr, reg, *value);
    return ESP_OK;
}


esp_err_t register_bus_read_u8_quiet(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *value)
{
    return register_bus_read_u8(port, dev_addr, reg, value);
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


esp_err_t register_bus_read(i2c_port_t port, uint8_t dev_addr, uint8_t reg, uint8_t *data, size_t len)
{
    (void)port;
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_next_read != ESP_OK) {
        esp_err_t err = s_fail_next_read;
        s_fail_next_read = ESP_OK;
        return err;
    }
    if (s_fail_reg_read_enabled && s_fail_reg_read_addr == dev_addr && s_fail_reg_read_reg == reg) {
        esp_err_t err = s_fail_reg_read_err;
        s_fail_reg_read_enabled = false;
        s_fail_reg_read_err = ESP_OK;
        return err;
    }
    for (size_t i = 0; i < len; ++i) {
        data[i] = s_regs[dev_addr & 0x7f][(uint8_t)(reg + i)];
        record(FAKE_BUS_OP_READ, dev_addr, (uint8_t)(reg + i), data[i]);
    }
    return ESP_OK;
}
