#include "trigger_gpio.h"
#include "capability_registry.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#endif

static bool read_gpio_level(const trigger_gpio_t *gpio, bool *level)
{
    if (gpio == NULL || level == NULL) {
        return false;
    }
#ifdef ESP_PLATFORM
    int raw = gpio_get_level((gpio_num_t)gpio->config.pin);
    *level = gpio->config.active_low ? raw == 0 : raw != 0;
#else
    *level = gpio->host_level;
#endif
    return true;
}

bool trigger_gpio_init(trigger_gpio_t *gpio, rule_source_t source, const rule_gpio_config_t *config)
{
    if (gpio == NULL || config == NULL || !capability_source_supported(source) ||
        !capability_gpio_source_profile_validate(source, config, NULL, 0)) {
        return false;
    }
    memset(gpio, 0, sizeof(*gpio));
    gpio->source = source;
    gpio->config = *config;
#ifdef ESP_PLATFORM
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << (gpio_num_t)config->pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io_conf) != ESP_OK) {
        return false;
    }
#endif
    gpio->enabled = true;
    bool level = false;
    (void)read_gpio_level(gpio, &level);
    gpio->last_level = level;
    gpio->stable_level = level;
    gpio->last_change_ms = 0;
    gpio->has_sample = false;
    return true;
}

void trigger_gpio_deinit(trigger_gpio_t *gpio)
{
    if (gpio == NULL || !gpio->enabled) return;
#ifdef ESP_PLATFORM
    (void)gpio_reset_pin((gpio_num_t)gpio->config.pin);
#endif
    gpio->enabled = false;
}

bool trigger_gpio_probe(const trigger_gpio_t *gpio)
{
    return gpio != NULL && gpio->enabled;
}

#ifndef ESP_PLATFORM
void trigger_gpio_set_host_level(trigger_gpio_t *gpio, bool level)
{
    if (gpio != NULL) {
        gpio->host_level = level;
    }
}
#endif

size_t trigger_gpio_poll(trigger_gpio_t *gpio, trigger_adapter_t *adapter, uint32_t uptime_ms)
{
    if (gpio == NULL || adapter == NULL || !gpio->enabled) {
        return 0;
    }
    bool level = false;
    if (!read_gpio_level(gpio, &level)) {
        return 0;
    }

    if (!gpio->has_sample) {
        gpio->has_sample = true;
        gpio->last_level = level;
        gpio->stable_level = level;
        gpio->last_change_ms = uptime_ms;
        gpio->initial_pending = gpio->source == RULE_SOURCE_GPIO_DIGITAL;
        return 0;
    }

    if (level != gpio->last_level) {
        gpio->last_level = level;
        gpio->last_change_ms = uptime_ms;
    }

    uint32_t debounce_ms = gpio->config.debounce_ms;
    if (debounce_ms == 0) {
        debounce_ms = RULE_GPIO_MIN_DEBOUNCE_MS;
    }
    if ((!gpio->initial_pending && level == gpio->stable_level) || uptime_ms - gpio->last_change_ms < debounce_ms) {
        return 0;
    }

    gpio->stable_level = level;
    gpio->initial_pending = false;
    if ((gpio->config.profile == RULE_GPIO_PROFILE_RISING_EDGE && !level) ||
        (gpio->config.profile == RULE_GPIO_PROFILE_FALLING_EDGE && level)) {
        return 0;
    }
    trigger_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.source = gpio->source;
    const char *prefix = gpio->source == RULE_SOURCE_GPIO_EDGE ? "gpio.edge" : "gpio.digital";
    (void)snprintf(fact.source_key, sizeof(fact.source_key), "%s.%d", prefix, gpio->config.pin);
    fact.value = rule_value_bool(level);
    fact.uptime_ms = uptime_ms;
    return trigger_emit_fact(adapter, &fact) ? 1u : 0u;
}
