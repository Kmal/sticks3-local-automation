#include "uac_esp_device.h"

#include "sdkconfig.h"

#if CONFIG_APP_USB_UAC_DEVICE
#include "usb_device_uac.h"
_Static_assert(CONFIG_UAC_SAMPLE_RATE == CONFIG_APP_USB_UAC_SAMPLE_RATE_HZ,
               "UAC descriptor and PCM sample rates must match; use the UAC profile defaults");
#if CONFIG_APP_USB_UAC_MIC
_Static_assert(CONFIG_UAC_MIC_CHANNEL_NUM == 1, "UAC microphone must be mono");
#else
_Static_assert(CONFIG_UAC_MIC_CHANNEL_NUM == 0, "Disable microphone descriptors for speaker-only UAC");
#endif
#if CONFIG_APP_USB_UAC_SPEAKER
_Static_assert(CONFIG_UAC_SPEAKER_CHANNEL_NUM == 1, "UAC speaker must be mono");
#else
_Static_assert(CONFIG_UAC_SPEAKER_CHANNEL_NUM == 0, "Disable speaker descriptors for mic-only UAC");
#endif
#endif

esp_err_t uac_esp_device_start(uac_device_adapter_t *adapter)
{
    if (adapter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_APP_USB_UAC_DEVICE
    uac_device_descriptor_plan_t plan = uac_device_adapter_descriptor_plan(adapter);
    uac_device_config_t config = {
        .skip_tinyusb_init = plan.skip_tinyusb_init,
        .output_cb = plan.output_enabled ? uac_device_adapter_output_cb : NULL,
        .input_cb = plan.input_enabled ? uac_device_adapter_input_cb : NULL,
        .set_mute_cb = uac_device_adapter_set_mute_cb,
        .set_volume_cb = uac_device_adapter_set_volume_cb,
        .cb_ctx = adapter,
    };
    return uac_device_init(&config);
#else
    (void)adapter;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
