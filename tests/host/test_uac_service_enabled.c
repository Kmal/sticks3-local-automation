#include "uac_service.h"
#include "uac_device_adapter.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
static unsigned creates, deletes, usb_calls, fail_task;
static bool fail_usb;
static uac_device_adapter_t *callback_context;
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *ctx, int prio, TaskHandle_t *handle)
{
    (void)task; (void)name; (void)stack; (void)ctx; (void)prio;
    ++creates;
    if (creates == fail_task) return 0;
    *handle = (void *)(uintptr_t)creates;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { assert(task != NULL); ++deletes; }
esp_err_t uac_esp_device_start(uac_device_adapter_t *adapter)
{
    assert(creates == 2);
    callback_context = adapter;
    ++usb_calls;
    return fail_usb ? ESP_FAIL : ESP_OK;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    unsigned scenario = (unsigned)atoi(argv[1]);
    fail_task = scenario <= 2 ? scenario : 0;
    fail_usb = scenario == 3;
    esp_err_t result = uac_service_start_from_kconfig();
    if (fail_task) {
        assert(result == ESP_ERR_NO_MEM);
        assert(usb_calls == 0);
        assert(deletes == fail_task - 1);
    } else {
        assert(result == (fail_usb ? ESP_FAIL : ESP_OK));
        assert(usb_calls == 1);
        assert(deletes == (fail_usb ? 2u : 0u));
        /* Simulate a retained driver callback after partial USB init failure. */
        uint8_t pcm[64] = {0};
        size_t read = 0;
        assert(uac_device_adapter_output_cb(pcm, sizeof(pcm), callback_context) == ESP_OK);
        assert(uac_device_adapter_input_cb(pcm, sizeof(pcm), &read, callback_context) == ESP_OK);
        assert(read == sizeof(pcm));
    }
    assert(uac_service_start_from_kconfig() == ESP_ERR_INVALID_STATE);
    puts("enabled UAC startup/failure regression passed");
    return 0;
}
