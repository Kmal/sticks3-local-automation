#include "rule_types.h"
#include "capability_registry.h"

#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_len, const char *message)
{
    if (error != NULL && error_len > 0) {
        (void)snprintf(error, error_len, "%s", message);
    }
}

static bool c_string_terminated(const char *text, size_t capacity)
{
    return memchr(text, '\0', capacity) != NULL;
}

static bool rule_source_valid(rule_source_t source)
{
    return source > RULE_SOURCE_INVALID && source < RULE_SOURCE_COUNT;
}

static bool rule_action_valid(rule_action_type_t action)
{
    return action > RULE_ACTION_INVALID && action < RULE_ACTION_COUNT;
}

static bool rule_comparator_valid(rule_comparator_t comparator)
{
    return comparator > RULE_COMPARATOR_INVALID && comparator < RULE_COMPARATOR_COUNT;
}

static size_t bounded_strlen(const char *text, size_t capacity)
{
    size_t len = 0;
    while (len < capacity && text[len] != '\0') {
        ++len;
    }
    return len;
}

static bool ascii_is_control_or_space(char c)
{
    return c <= ' ' || c == 0x7f;
}

static bool http_url_valid(const char *url)
{
    const size_t len = bounded_strlen(url, RULE_HTTP_URL_MAX);
    if (len == 0 || len >= RULE_HTTP_URL_MAX) {
        return false;
    }

    size_t host_start = 0;
    if (strncmp(url, "http://", 7) == 0) {
        host_start = 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        host_start = 8;
    } else {
        return false;
    }

    if (host_start >= len || url[host_start] == '/' || url[host_start] == ':' || url[host_start] == '?' || url[host_start] == '#') {
        return false;
    }

    bool has_host_char = false;
    bool in_host = true;
    for (size_t i = host_start; i < len; ++i) {
        const char c = url[i];
        if (ascii_is_control_or_space(c) || c == '#') {
            return false;
        }
        if (in_host && (c == '/' || c == ':' || c == '?')) {
            in_host = false;
            continue;
        }
        if (in_host) {
            has_host_char = true;
        }
    }
    return has_host_char;
}

static bool comparator_supports_value(rule_comparator_t comparator, rule_value_kind_t kind)
{
    if (kind == RULE_VALUE_BOOL) {
        return comparator == RULE_COMPARATOR_EQ || comparator == RULE_COMPARATOR_NE;
    }
    if (kind == RULE_VALUE_I32) {
        return comparator == RULE_COMPARATOR_EQ || comparator == RULE_COMPARATOR_NE || comparator == RULE_COMPARATOR_GT ||
               comparator == RULE_COMPARATOR_GTE || comparator == RULE_COMPARATOR_LT || comparator == RULE_COMPARATOR_LTE;
    }
    return false;
}

bool rule_source_is_gpio(rule_source_t source)
{
    return source == RULE_SOURCE_GPIO_DIGITAL || source == RULE_SOURCE_GPIO_EDGE ||
           source == RULE_SOURCE_GPIO_PULSE_COUNT || source == RULE_SOURCE_GPIO_FREQUENCY_HZ;
}

const char *rule_source_name(rule_source_t source)
{
    switch (source) {
    case RULE_SOURCE_SOUND_RMS_DBFS:
        return "sound.rms_dbfs";
    case RULE_SOURCE_SOUND_PEAK_DBFS:
        return "sound.peak_dbfs";
    case RULE_SOURCE_SOUND_CLIPPED:
        return "sound.clipped";
    case RULE_SOURCE_KEY1_SHORT:
        return "button.key1.short";
    case RULE_SOURCE_KEY2_SHORT:
        return "button.key2.short";
    case RULE_SOURCE_BLE_CONNECTED:
        return "ble.connected";
    case RULE_SOURCE_WIFI_CONNECTED:
        return "wifi.connected";
    case RULE_SOURCE_BATTERY_PERCENT:
        return "power.battery_percent";
    case RULE_SOURCE_POWER_USB_PRESENT:
        return "power.usb_present";
    case RULE_SOURCE_BMI270_MOTION:
        return "bmi270.motion";
    case RULE_SOURCE_HAT_PIR_MOTION:
        return "hat.pir.motion";
    case RULE_SOURCE_HAT_ENV3_TEMPERATURE_C:
        return "hat.env3.temperature_c";
    case RULE_SOURCE_HAT_ENV3_HUMIDITY_RH:
        return "hat.env3.humidity_rh";
    case RULE_SOURCE_HAT_ENV3_PRESSURE_HPA:
        return "hat.env3.pressure_hpa";
    case RULE_SOURCE_HAT_LIGHT_LUX:
        return "hat.light.lux";
    case RULE_SOURCE_HAT_TOF_DISTANCE_MM:
        return "hat.tof.distance_mm";
    case RULE_SOURCE_HAT_NCIR_TEMPERATURE_C:
        return "hat.ncir.temperature_c";
    case RULE_SOURCE_HAT_THERMAL_AVG_C:
        return "hat.thermal.avg_c";
    case RULE_SOURCE_HAT_THERMAL_MAX_C:
        return "hat.thermal.max_c";
    case RULE_SOURCE_HAT_HEART_RATE_BPM:
        return "hat.heart_rate.bpm";
    case RULE_SOURCE_HAT_ADC_VOLTAGE_MV:
        return "hat.adc.voltage_mv";
    case RULE_SOURCE_GPIO_DIGITAL:
        return "gpio.digital";
    case RULE_SOURCE_GPIO_EDGE:
        return "gpio.edge";
    case RULE_SOURCE_GPIO_PULSE_COUNT:
        return "gpio.pulse_count";
    case RULE_SOURCE_GPIO_FREQUENCY_HZ:
        return "gpio.frequency_hz";
    case RULE_SOURCE_ADC_VOLTAGE_MV:
        return "adc.voltage_mv";
    default:
        return "invalid";
    }
}

const char *rule_action_name(rule_action_type_t action)
{
    switch (action) {
    case RULE_ACTION_BLE_MESSAGE:
        return "ble_message";
    case RULE_ACTION_HTTP_POST:
        return "http_post";
    case RULE_ACTION_HAT_OPERATION:
        return "hat_operation";
    case RULE_ACTION_IR_SEND:
        return "ir_send";
    case RULE_ACTION_LOCAL_UI:
        return "local_ui";
    case RULE_ACTION_SPEAKER_TONE:
        return "speaker_tone";
    default:
        return "invalid";
    }
}

bool rule_value_equal(rule_value_t left, rule_value_t right)
{
    if (left.kind != right.kind) {
        return false;
    }
    switch (left.kind) {
    case RULE_VALUE_BOOL:
        return left.as.bool_value == right.as.bool_value;
    case RULE_VALUE_I32:
        return left.as.i32_value == right.as.i32_value;
    default:
        return false;
    }
}

bool rule_source_is_sound(rule_source_t source)
{
    return source == RULE_SOURCE_SOUND_RMS_DBFS || source == RULE_SOURCE_SOUND_PEAK_DBFS ||
           source == RULE_SOURCE_SOUND_CLIPPED;
}

bool automation_config_has_enabled_source(const automation_config_t *config, rule_source_t source)
{
    if (config == NULL) {
        return false;
    }
    for (size_t i = 0; i < config->rule_count && i < RULE_MAX_RULES; ++i) {
        const automation_rule_t *rule = &config->rules[i];
        if (rule->enabled && rule->when.source == source) {
            return true;
        }
    }
    return false;
}

bool automation_config_has_enabled_sound_source(const automation_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    for (size_t i = 0; i < config->rule_count && i < RULE_MAX_RULES; ++i) {
        const automation_rule_t *rule = &config->rules[i];
        if (rule->enabled && rule_source_is_sound(rule->when.source)) {
            return true;
        }
    }
    return false;
}

void automation_config_set_defaults(automation_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->schema_version = RULE_CONFIG_SCHEMA_VERSION;
    config->rule_count = 2;

    automation_rule_t *sound = &config->rules[0];
    sound->enabled = false;
    sound->id = 1;
    (void)snprintf(sound->name, sizeof(sound->name), "Loud sound notice");
    sound->when.source = RULE_SOURCE_SOUND_RMS_DBFS;
    sound->when.comparator = RULE_COMPARATOR_GTE;
    sound->when.threshold = rule_value_i32(-20480); /* -80 dBFS in Q8-style units. */
    sound->when.sustain_ms = 250;
    sound->action_count = 1;
    sound->actions[0].type = RULE_ACTION_LOCAL_UI;
    sound->cooldown_ms = 1000;

    automation_rule_t *button = &config->rules[1];
    button->enabled = false;
    button->id = 2;
    (void)snprintf(button->name, sizeof(button->name), "KEY1 BLE notice");
    button->when.source = RULE_SOURCE_KEY1_SHORT;
    button->when.comparator = RULE_COMPARATOR_EQ;
    button->when.threshold = rule_value_bool(true);
    button->action_count = 1;
    button->actions[0].type = RULE_ACTION_LOCAL_UI;
    button->cooldown_ms = 500;
}

bool automation_rule_validate(const automation_rule_t *rule, char *error, size_t error_len)
{
    if (rule == NULL) {
        set_error(error, error_len, "rule is null");
        return false;
    }
    if (!c_string_terminated(rule->name, RULE_NAME_MAX)) {
        set_error(error, error_len, "rule name is too long");
        return false;
    }
    if (!rule_source_valid(rule->when.source)) {
        set_error(error, error_len, "invalid trigger source");
        return false;
    }
    if (!capability_source_schema_supported(rule->when.source)) {
        set_error(error, error_len, "unsupported trigger source");
        return false;
    }
    if (!c_string_terminated(rule->when.source_key, RULE_SOURCE_KEY_MAX)) {
        set_error(error, error_len, "source key is too long");
        return false;
    }
    if (!rule_comparator_valid(rule->when.comparator)) {
        set_error(error, error_len, "invalid comparator");
        return false;
    }
    if (rule->when.threshold.kind != RULE_VALUE_BOOL && rule->when.threshold.kind != RULE_VALUE_I32) {
        set_error(error, error_len, "invalid value type");
        return false;
    }
    if (!comparator_supports_value(rule->when.comparator, rule->when.threshold.kind)) {
        set_error(error, error_len, "comparator does not support value type");
        return false;
    }
    if (rule->when.sustain_ms > RULE_MAX_SUSTAIN_MS) {
        set_error(error, error_len, "sustain is too long");
        return false;
    }
    if (rule->when.sustain_ms != 0 && (rule->when.source == RULE_SOURCE_KEY1_SHORT ||
        rule->when.source == RULE_SOURCE_KEY2_SHORT || rule->when.source == RULE_SOURCE_GPIO_EDGE)) {
        set_error(error, error_len, "momentary events cannot sustain");
        return false;
    }
    if (rule->cooldown_ms < RULE_MIN_COOLDOWN_MS || rule->cooldown_ms > RULE_MAX_COOLDOWN_MS) {
        set_error(error, error_len, "invalid cooldown");
        return false;
    }
    if (rule->action_count == 0 || rule->action_count > RULE_MAX_ACTIONS_PER_RULE) {
        set_error(error, error_len, "invalid action count");
        return false;
    }
    if (rule_source_is_gpio(rule->when.source)) {
        if (rule->when.source_key[0] == '\0') {
            set_error(error, error_len, "gpio source requires a validated profile key");
            return false;
        }
        if (!capability_gpio_source_profile_validate(rule->when.source, &rule->when.gpio, error, error_len)) {
            return false;
        }
    }
    if (rule->when.source == RULE_SOURCE_ADC_VOLTAGE_MV && !capability_adc_key_valid(rule->when.source_key)) {
        set_error(error, error_len, "invalid adc source key");
        return false;
    }

    for (size_t i = 0; i < rule->action_count; ++i) {
        const rule_action_t *action = &rule->actions[i];
        if (!rule_action_valid(action->type)) {
            set_error(error, error_len, "invalid action type");
            return false;
        }
        if (!capability_action_supported(action->type)) {
            set_error(error, error_len, "unsupported action type");
            return false;
        }
        if (!c_string_terminated(action->http_url, RULE_HTTP_URL_MAX) ||
            !c_string_terminated(action->http_bearer_token, RULE_HTTP_AUTH_MAX)) {
            set_error(error, error_len, "action string is too long");
            return false;
        }
        if (action->type == RULE_ACTION_HTTP_POST) {
            if (!http_url_valid(action->http_url)) {
                set_error(error, error_len, "invalid http url");
                return false;
            }
            if (action->timeout_ms == 0 || action->timeout_ms > RULE_MAX_HTTP_TIMEOUT_MS) {
                set_error(error, error_len, "invalid http timeout");
                return false;
            }
        }
        if (action->type == RULE_ACTION_IR_SEND) {
            if (action->ir_protocol != RULE_IR_PROTOCOL_NEC || action->ir_carrier_hz < 30000u || action->ir_carrier_hz > 60000u ||
                action->ir_repeat_count > 5u || action->timeout_ms == 0 || action->timeout_ms > 1000u) {
                set_error(error, error_len, "invalid ir action");
                return false;
            }
        }
        if (action->type == RULE_ACTION_SPEAKER_TONE) {
            if (action->speaker_frequency_hz < RULE_SPEAKER_MIN_FREQUENCY_HZ ||
                action->speaker_frequency_hz > RULE_SPEAKER_MAX_FREQUENCY_HZ ||
                action->speaker_duration_ms < RULE_SPEAKER_MIN_DURATION_MS ||
                action->speaker_duration_ms > RULE_SPEAKER_MAX_DURATION_MS ||
                action->speaker_volume_percent == 0 ||
                action->speaker_volume_percent > RULE_SPEAKER_MAX_VOLUME_PERCENT ||
                action->timeout_ms == 0 || action->timeout_ms > RULE_SPEAKER_MAX_TIMEOUT_MS) {
                set_error(error, error_len, "invalid speaker action");
                return false;
            }
        }
        if (action->type == RULE_ACTION_HAT_OPERATION) {
            set_error(error, error_len, "unsupported hat operation");
            return false;
        }
    }

    return true;
}

bool automation_config_validate(const automation_config_t *config, char *error, size_t error_len)
{
    if (config == NULL) {
        set_error(error, error_len, "config is null");
        return false;
    }
    if (config->schema_version != RULE_CONFIG_SCHEMA_VERSION) {
        set_error(error, error_len, "unsupported schema version");
        return false;
    }
    if (config->rule_count > RULE_MAX_RULES) {
        set_error(error, error_len, "too many rules");
        return false;
    }
    for (size_t i = 0; i < config->rule_count; ++i) {
        if (config->rules[i].id == 0) {
            set_error(error, error_len, "rule id must be nonzero");
            return false;
        }
        for (size_t j = 0; j < i; ++j) if (config->rules[j].id == config->rules[i].id) {
            set_error(error, error_len, "duplicate rule id");
            return false;
        }
        if (!automation_rule_validate(&config->rules[i], error, error_len)) {
            return false;
        }
        const automation_rule_t *rule = &config->rules[i];
        if (!rule->enabled || !rule_source_is_gpio(rule->when.source)) continue;
        for (size_t j = 0; j < config->rule_count; ++j) {
            const automation_rule_t *other = &config->rules[j];
            if (!other->enabled) continue;
            if (other->when.source == RULE_SOURCE_ADC_VOLTAGE_MV &&
                capability_adc_key_matches_pin(other->when.source_key, rule->when.gpio.pin)) {
                set_error(error, error_len, "adc and gpio cannot share a pin");
                return false;
            }
            if (rule_source_is_gpio(other->when.source) && other->when.gpio.pin == rule->when.gpio.pin &&
                other->when.gpio.active_low != rule->when.gpio.active_low) {
                set_error(error, error_len, "conflicting gpio pull configuration");
                return false;
            }
        }
    }
    return true;
}
