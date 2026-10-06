#include "capability_registry.h"
#include "board_sticks3.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#ifndef CONFIG_APP_WIFI_ENABLE
#define CONFIG_APP_WIFI_ENABLE 0
#endif
#ifndef CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS
#define CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS 0
#endif
#ifndef CONFIG_APP_SOUND_LEVEL_TRIGGERS
#define CONFIG_APP_SOUND_LEVEL_TRIGGERS 0
#endif
/* Disabled ESP-IDF boolean options are absent from sdkconfig.h. Host defaults
 * belong in the test sdkconfig fixture, never in production capability gates. */
#ifndef CONFIG_APP_BATTERY_FACTS
#define CONFIG_APP_BATTERY_FACTS 0
#endif
#ifndef CONFIG_APP_USB_POWER_FACTS
#define CONFIG_APP_USB_POWER_FACTS 0
#endif
#ifndef CONFIG_APP_BMI270_FACTS
#define CONFIG_APP_BMI270_FACTS 0
#endif
#ifndef CONFIG_APP_ADC_FACTS
#define CONFIG_APP_ADC_FACTS 0
#endif
#ifndef CONFIG_APP_SPEAKER_ACTION
#define CONFIG_APP_SPEAKER_ACTION 0
#endif

static void set_error(char *error, size_t error_len, const char *message)
{
    if (error != NULL && error_len > 0) {
        (void)snprintf(error, error_len, "%s", message);
    }
}

static bool pin_is_one_of(int pin, const int *pins, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (pin == pins[i]) {
            return true;
        }
    }
    return false;
}

static bool profile_valid(rule_gpio_profile_t profile)
{
    return profile > RULE_GPIO_PROFILE_NONE && profile < RULE_GPIO_PROFILE_COUNT;
}

bool capability_adc_key_matches_pin(const char *key, int pin)
{
    static const char *keys[] = {"hat.g4", "hat.g5", "hat.g6", "hat.g7", "hat.g8", "grove.g9", "grove.g10"};
    return key != NULL && pin >= 4 && pin <= 10 && (key[0] == '\0' || strcmp(key, keys[pin - 4]) == 0);
}

bool capability_adc_key_valid(const char *key)
{
    for (int pin = 4; pin <= 10; ++pin) if (capability_adc_key_matches_pin(key, pin)) return true;
    return false;
}

typedef struct {
    rule_source_t source;
    const char *name;
    bool schema_supported;
    bool runtime_available;
    const char *reason;
} source_capability_t;

typedef struct {
    rule_action_type_t action;
    const char *name;
    bool supported;
    const char *reason;
} action_capability_t;

static const source_capability_t s_source_caps[] = {
    {RULE_SOURCE_SOUND_RMS_DBFS, "sound.rms_dbfs", true, CONFIG_APP_SOUND_LEVEL_TRIGGERS, CONFIG_APP_SOUND_LEVEL_TRIGGERS ? "implemented" : "audio_capture_disabled"},
    {RULE_SOURCE_SOUND_PEAK_DBFS, "sound.peak_dbfs", true, CONFIG_APP_SOUND_LEVEL_TRIGGERS, CONFIG_APP_SOUND_LEVEL_TRIGGERS ? "implemented" : "audio_capture_disabled"},
    {RULE_SOURCE_SOUND_CLIPPED, "sound.clipped", true, CONFIG_APP_SOUND_LEVEL_TRIGGERS, CONFIG_APP_SOUND_LEVEL_TRIGGERS ? "implemented" : "audio_capture_disabled"},
    {RULE_SOURCE_KEY1_SHORT, "button.key1.short", true, true, "implemented"},
    {RULE_SOURCE_KEY2_SHORT, "button.key2.short", true, true, "implemented"},
    {RULE_SOURCE_BLE_CONNECTED, "ble.connected", true, CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS, CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS ? "implemented" : "ble_transport_disabled"},
    {RULE_SOURCE_WIFI_CONNECTED, "wifi.connected", true, CONFIG_APP_WIFI_ENABLE, CONFIG_APP_WIFI_ENABLE ? "implemented" : "wifi_disabled"},
    {RULE_SOURCE_BATTERY_PERCENT, "power.battery_percent", true, CONFIG_APP_BATTERY_FACTS, CONFIG_APP_BATTERY_FACTS ? "implemented" : "battery_facts_disabled"},
    {RULE_SOURCE_POWER_USB_PRESENT, "power.usb_present", true, CONFIG_APP_USB_POWER_FACTS, CONFIG_APP_USB_POWER_FACTS ? "implemented" : "usb_power_facts_disabled"},
    {RULE_SOURCE_BMI270_MOTION, "bmi270.motion", true, CONFIG_APP_BMI270_FACTS, CONFIG_APP_BMI270_FACTS ? "implemented" : "bmi270_facts_disabled"},
    {RULE_SOURCE_HAT_PIR_MOTION, "hat.pir.motion", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_ENV3_TEMPERATURE_C, "hat.env3.temperature_c", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_ENV3_HUMIDITY_RH, "hat.env3.humidity_rh", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_ENV3_PRESSURE_HPA, "hat.env3.pressure_hpa", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_LIGHT_LUX, "hat.light.lux", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_TOF_DISTANCE_MM, "hat.tof.distance_mm", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_NCIR_TEMPERATURE_C, "hat.ncir.temperature_c", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_THERMAL_AVG_C, "hat.thermal.avg_c", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_THERMAL_MAX_C, "hat.thermal.max_c", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_HEART_RATE_BPM, "hat.heart_rate.bpm", false, false, "missing_hat_driver"},
    {RULE_SOURCE_HAT_ADC_VOLTAGE_MV, "hat.adc.voltage_mv", false, false, "missing_hat_driver"},
    {RULE_SOURCE_GPIO_DIGITAL, "gpio.digital", true, true, "implemented"},
    {RULE_SOURCE_GPIO_EDGE, "gpio.edge", true, true, "implemented"},
    {RULE_SOURCE_GPIO_PULSE_COUNT, "gpio.pulse_count", false, false, "missing_gpio_driver"},
    {RULE_SOURCE_GPIO_FREQUENCY_HZ, "gpio.frequency_hz", false, false, "missing_gpio_driver"},
    {RULE_SOURCE_ADC_VOLTAGE_MV, "adc.voltage_mv", true, CONFIG_APP_ADC_FACTS, CONFIG_APP_ADC_FACTS ? "implemented" : "adc_facts_disabled"},
};

static const action_capability_t s_action_caps[] = {
    {RULE_ACTION_BLE_MESSAGE, "ble_message", CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS, CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS ? "implemented" : "ble_transport_disabled"},
    {RULE_ACTION_HTTP_POST, "http_post", true, "implemented"},
    {RULE_ACTION_HAT_OPERATION, "hat_operation", false, "missing_hat_action_driver"},
    {RULE_ACTION_IR_SEND, "ir_send", true, "implemented"},
    {RULE_ACTION_LOCAL_UI, "local_ui", true, "implemented"},
    {RULE_ACTION_SPEAKER_TONE, "speaker_tone", CONFIG_APP_SPEAKER_ACTION, CONFIG_APP_SPEAKER_ACTION ? "implemented" : "speaker_action_disabled"},
};

bool capability_source_supported(rule_source_t source)
{
    return capability_source_runtime_available(source);
}

bool capability_source_schema_supported(rule_source_t source)
{
    for (size_t i = 0; i < sizeof(s_source_caps) / sizeof(s_source_caps[0]); ++i) {
        if (s_source_caps[i].source == source) {
            return s_source_caps[i].schema_supported;
        }
    }
    return false;
}

bool capability_source_runtime_available(rule_source_t source)
{
    for (size_t i = 0; i < sizeof(s_source_caps) / sizeof(s_source_caps[0]); ++i) {
        if (s_source_caps[i].source == source) {
            return s_source_caps[i].runtime_available;
        }
    }
    return false;
}

bool capability_action_supported(rule_action_type_t action)
{
    for (size_t i = 0; i < sizeof(s_action_caps) / sizeof(s_action_caps[0]); ++i) {
        if (s_action_caps[i].action == action) {
            return s_action_caps[i].supported;
        }
    }
    return false;
}

const char *capability_source_availability_reason(rule_source_t source)
{
    for (size_t i = 0; i < sizeof(s_source_caps) / sizeof(s_source_caps[0]); ++i) {
        if (s_source_caps[i].source == source) {
            return s_source_caps[i].reason;
        }
    }
    return "unknown_source";
}

const char *capability_source_reason(rule_source_t source)
{
    return capability_source_availability_reason(source);
}

const char *capability_action_reason(rule_action_type_t action)
{
    for (size_t i = 0; i < sizeof(s_action_caps) / sizeof(s_action_caps[0]); ++i) {
        if (s_action_caps[i].action == action) {
            return s_action_caps[i].reason;
        }
    }
    return "unknown_action";
}

bool capability_hat_supported(rule_source_t source)
{
    (void)source;
    return false;
}

bool capability_gpio_profile_validate(const rule_gpio_config_t *gpio, char *error, size_t error_len)
{
    return capability_gpio_source_profile_validate(RULE_SOURCE_GPIO_DIGITAL, gpio, error, error_len);
}

bool capability_gpio_source_profile_validate(rule_source_t source, const rule_gpio_config_t *gpio, char *error, size_t error_len)
{
    if (gpio == NULL) {
        set_error(error, error_len, "gpio config is null");
        return false;
    }
    if (!profile_valid(gpio->profile)) {
        set_error(error, error_len, "invalid gpio profile");
        return false;
    }

    bool source_profile_ok = false;
    switch (source) {
    case RULE_SOURCE_GPIO_DIGITAL:
        source_profile_ok = gpio->profile == RULE_GPIO_PROFILE_DIGITAL_HIGH_LOW || gpio->profile == RULE_GPIO_PROFILE_DEBOUNCED_CONTACT;
        break;
    case RULE_SOURCE_GPIO_EDGE:
        source_profile_ok = gpio->profile == RULE_GPIO_PROFILE_RISING_EDGE || gpio->profile == RULE_GPIO_PROFILE_FALLING_EDGE;
        break;
    case RULE_SOURCE_GPIO_PULSE_COUNT:
        source_profile_ok = gpio->profile == RULE_GPIO_PROFILE_PULSE_COUNT;
        break;
    case RULE_SOURCE_GPIO_FREQUENCY_HZ:
        source_profile_ok = gpio->profile == RULE_GPIO_PROFILE_FREQUENCY;
        break;
    default:
        source_profile_ok = false;
        break;
    }
    if (!source_profile_ok) {
        set_error(error, error_len, "gpio profile does not match source");
        return false;
    }

    if (gpio->pin < 0 || gpio->pin > 48) {
        set_error(error, error_len, "gpio pin out of range");
        return false;
    }
    const int lcd_pins[] = {BOARD_LCD_MOSI_GPIO, BOARD_LCD_SCLK_GPIO, BOARD_LCD_DC_GPIO, BOARD_LCD_CS_GPIO,
                            BOARD_LCD_RST_GPIO, BOARD_LCD_BL_GPIO};
    const int i2c_pins[] = {BOARD_I2C_SDA_IO, BOARD_I2C_SCL_IO};
    const int i2s_pins[] = {BOARD_I2S_DO_IO, BOARD_I2S_BCK_IO, BOARD_I2S_WS_IO, BOARD_I2S_DI_IO, BOARD_I2S_MCLK_IO};
    const int button_pins[] = {BOARD_BUTTON_KEY1_GPIO, BOARD_BUTTON_KEY2_GPIO};
    const int ir_pins[] = {BOARD_IR_TX_GPIO, BOARD_IR_RX_GPIO};
    const int boot_usb_risk_pins[] = {0, 19, 20, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37};
    if (pin_is_one_of(gpio->pin, lcd_pins, sizeof(lcd_pins) / sizeof(lcd_pins[0]))) {
        set_error(error, error_len, "gpio pin conflicts with lcd");
        return false;
    }
    if (pin_is_one_of(gpio->pin, i2c_pins, sizeof(i2c_pins) / sizeof(i2c_pins[0]))) {
        set_error(error, error_len, "gpio pin conflicts with i2c");
        return false;
    }
    if (pin_is_one_of(gpio->pin, i2s_pins, sizeof(i2s_pins) / sizeof(i2s_pins[0]))) {
        set_error(error, error_len, "gpio pin conflicts with audio");
        return false;
    }
    if (pin_is_one_of(gpio->pin, button_pins, sizeof(button_pins) / sizeof(button_pins[0]))) {
        set_error(error, error_len, "gpio pin conflicts with buttons");
        return false;
    }
    if (pin_is_one_of(gpio->pin, ir_pins, sizeof(ir_pins) / sizeof(ir_pins[0]))) {
        set_error(error, error_len, "gpio pin conflicts with ir");
        return false;
    }
    if (pin_is_one_of(gpio->pin, boot_usb_risk_pins, sizeof(boot_usb_risk_pins) / sizeof(boot_usb_risk_pins[0]))) {
        set_error(error, error_len, "gpio pin has boot usb or internal risk");
        return false;
    }
    if ((gpio->profile == RULE_GPIO_PROFILE_DEBOUNCED_CONTACT || gpio->profile == RULE_GPIO_PROFILE_RISING_EDGE ||
         gpio->profile == RULE_GPIO_PROFILE_FALLING_EDGE) &&
        (gpio->debounce_ms < RULE_GPIO_MIN_DEBOUNCE_MS || gpio->debounce_ms > RULE_GPIO_MAX_DEBOUNCE_MS)) {
        set_error(error, error_len, "invalid gpio debounce");
        return false;
    }
    return true;
}

static bool append_text(char *out, size_t out_len, size_t *used, const char *text)
{
    if (out == NULL || used == NULL || *used >= out_len) {
        return false;
    }
    const int written = snprintf(out + *used, out_len - *used, "%s", text);
    if (written < 0 || (size_t)written >= out_len - *used) {
        *used = out_len > 0 ? out_len - 1u : 0;
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool append_json_string_array_item(char *out, size_t out_len, size_t *used, const char *value, bool *first)
{
    if (!*first && !append_text(out, out_len, used, ",")) {
        return false;
    }
    *first = false;
    if (!append_text(out, out_len, used, "\"")) {
        return false;
    }
    if (!append_text(out, out_len, used, value)) {
        return false;
    }
    return append_text(out, out_len, used, "\"");
}

static bool append_json_capability_item(char *out, size_t out_len, size_t *used, const char *name, bool supported,
                                        const char *reason, bool *first)
{
    if (!*first && !append_text(out, out_len, used, ",")) {
        return false;
    }
    *first = false;
    const int written = snprintf(out + *used, out_len - *used,
                                 "{\"name\":\"%s\",\"supported\":%s,\"reason\":\"%s\"}",
                                 name, supported ? "true" : "false", reason);
    if (written < 0 || (size_t)written >= out_len - *used) {
        *used = out_len > 0 ? out_len - 1u : 0;
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool source_is_hat(rule_source_t source)
{
    return source == RULE_SOURCE_HAT_PIR_MOTION || source == RULE_SOURCE_HAT_ENV3_TEMPERATURE_C ||
           source == RULE_SOURCE_HAT_ENV3_HUMIDITY_RH || source == RULE_SOURCE_HAT_ENV3_PRESSURE_HPA ||
           source == RULE_SOURCE_HAT_LIGHT_LUX || source == RULE_SOURCE_HAT_TOF_DISTANCE_MM ||
           source == RULE_SOURCE_HAT_NCIR_TEMPERATURE_C || source == RULE_SOURCE_HAT_THERMAL_AVG_C ||
           source == RULE_SOURCE_HAT_THERMAL_MAX_C || source == RULE_SOURCE_HAT_HEART_RATE_BPM ||
           source == RULE_SOURCE_HAT_ADC_VOLTAGE_MV;
}

size_t capability_build_json(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return 0;
    }
    size_t used = 0;
    bool first = true;
    if (!append_text(out, out_len, &used, "{\"sources\":[")) {
        return used;
    }
    for (size_t i = 0; i < sizeof(s_source_caps) / sizeof(s_source_caps[0]); ++i) {
        if (s_source_caps[i].schema_supported) {
            (void)append_json_string_array_item(out, out_len, &used, s_source_caps[i].name, &first);
        }
    }
    if (!append_text(out, out_len, &used, "],\"source_capabilities\":[")) {
        return used;
    }
    first = true;
    for (size_t i = 0; i < sizeof(s_source_caps) / sizeof(s_source_caps[0]); ++i) {
        if (!s_source_caps[i].schema_supported) {
            continue;
        }
        if (!first && !append_text(out, out_len, &used, ",")) {
            return used;
        }
        first = false;
        const int written = snprintf(out + used, out_len - used,
                                     "{\"name\":\"%s\",\"schema_supported\":%s,\"runtime_available\":%s,\"reason\":\"%s\"}",
                                     s_source_caps[i].name,
                                     s_source_caps[i].schema_supported ? "true" : "false",
                                     s_source_caps[i].runtime_available ? "true" : "false",
                                     s_source_caps[i].reason);
        if (written < 0 || (size_t)written >= out_len - used) {
            used = out_len > 0 ? out_len - 1u : 0;
            return used;
        }
        used += (size_t)written;
    }
    if (!append_text(out, out_len, &used, "],\"actions\":[")) {
        return used;
    }
    first = true;
    for (size_t i = 0; i < sizeof(s_action_caps) / sizeof(s_action_caps[0]); ++i) {
        if (s_action_caps[i].supported) {
            (void)append_json_string_array_item(out, out_len, &used, s_action_caps[i].name, &first);
        }
    }
    if (!append_text(out, out_len, &used, "],\"disabled\":[")) {
        return used;
    }
    first = true;
    for (size_t i = 0; i < sizeof(s_source_caps) / sizeof(s_source_caps[0]); ++i) {
        if (!s_source_caps[i].runtime_available) {
            (void)append_json_string_array_item(out, out_len, &used, s_source_caps[i].name, &first);
        }
    }
    for (size_t i = 0; i < sizeof(s_action_caps) / sizeof(s_action_caps[0]); ++i) {
        if (!s_action_caps[i].supported) {
            (void)append_json_string_array_item(out, out_len, &used, s_action_caps[i].name, &first);
        }
    }
    if (!append_text(out, out_len, &used,
                     "],\"gpio_profiles\":["
                     "{\"name\":\"digital_high_low\",\"supported\":true,\"source\":\"gpio.digital\"},"
                     "{\"name\":\"debounced_contact\",\"supported\":true,\"source\":\"gpio.digital\"},"
                     "{\"name\":\"rising_edge\",\"supported\":true,\"source\":\"gpio.edge\"},"
                     "{\"name\":\"falling_edge\",\"supported\":true,\"source\":\"gpio.edge\"},"
                     "{\"name\":\"pulse_count\",\"supported\":false,\"source\":\"gpio.pulse_count\"},"
                     "{\"name\":\"frequency\",\"supported\":false,\"source\":\"gpio.frequency_hz\"}]")) {
        return used;
    }
    if (!append_text(out, out_len, &used, ",\"hat_sources\":[")) {
        return used;
    }
    first = true;
    for (size_t i = 0; i < sizeof(s_source_caps) / sizeof(s_source_caps[0]); ++i) {
        if (source_is_hat(s_source_caps[i].source)) {
            (void)append_json_capability_item(out, out_len, &used, s_source_caps[i].name, s_source_caps[i].runtime_available,
                                             s_source_caps[i].reason, &first);
        }
    }
    if (!append_text(out, out_len, &used, "],\"hat_operations\":[")) {
        return used;
    }
    first = true;
    (void)append_json_capability_item(out, out_len, &used, "relay_set", false, "missing_hat_action_driver", &first);
    (void)append_json_capability_item(out, out_len, &used, "led_set", false, "missing_hat_action_driver", &first);
    (void)append_json_capability_item(out, out_len, &used, "buzzer_tone", false, "missing_hat_action_driver", &first);
    (void)append_json_capability_item(out, out_len, &used, "servo_set", false, "missing_hat_action_driver", &first);
    (void)append_json_capability_item(out, out_len, &used, "dac_set", false, "missing_hat_action_driver", &first);
    if (!append_text(out, out_len, &used, "]")) {
        return used;
    }
    (void)append_text(out, out_len, &used, ",\"pin_conflicts\":[\"lcd\",\"i2c\",\"audio\",\"buttons\",\"ir\",\"boot_usb_internal\"]}");
    return used;
}
