#include "capability_registry.h"
#include <assert.h>
int main(void) {
    assert(!capability_source_runtime_available(RULE_SOURCE_WIFI_CONNECTED));
    assert(!capability_source_runtime_available(RULE_SOURCE_BLE_CONNECTED));
    assert(!capability_action_supported(RULE_ACTION_BLE_MESSAGE));
    assert(!capability_source_runtime_available(RULE_SOURCE_BATTERY_PERCENT));
    assert(!capability_source_runtime_available(RULE_SOURCE_POWER_USB_PRESENT));
    assert(!capability_source_runtime_available(RULE_SOURCE_BMI270_MOTION));
    assert(!capability_source_runtime_available(RULE_SOURCE_ADC_VOLTAGE_MV));
    assert(!capability_source_runtime_available(RULE_SOURCE_SOUND_RMS_DBFS));
    assert(!capability_action_supported(RULE_ACTION_SPEAKER_TONE));
    return 0;
}
