set(APP_INCLUDE_DIRS
    "."
    "${CMAKE_SOURCE_DIR}/generated"
    "${CMAKE_SOURCE_DIR}/vendor/cjson"
    "actions"
    "app"
    "audio"
    "board"
    "rules"
    "transport"
    "triggers"
    "ui"
    "usb_audio"
)

set(APP_SRCS
    "app/main.c"
    "app/app_mode.c"
    "app/app_time.c"
    "app/app_wifi.c"
    "app/app_sound_level_demand.c"
    "app/button_state.c"
    "board/register_bus.c"
    "board/board_i2c.c"
    "board/m5pm1.c"
    "board/board_power.c"
    "board/bmi270.c"
    "board/board_adc.c"
    "board/hardware_fact_service.c"
    "ui/status_ui.c"
    "ui/status_ui_input_map.c"
    "ui/status_lcd.c"
    "ui/ui_keyboard.c"
    "ui/ui_render.c"
    "ui/display_text.c"
    "ui/ui_nav.c"
    "ui/ui_model.c"
    "rules/rule_types.c"
    "rules/rule_engine.c"
    "rules/capability_registry.c"
    "rules/rule_runtime.c"
    "rules/rule_config_store.c"
    "rules/rule_web.c"
    "${CMAKE_SOURCE_DIR}/vendor/cjson/cJSON.c"
    "${CMAKE_SOURCE_DIR}/generated/webui_assets.c"
    "triggers/trigger_sources.c"
    "triggers/trigger_gpio.c"
    "triggers/trigger_hat.c"
    "actions/action_dispatcher.c"
    "actions/action_http.c"
    "actions/action_ir.c"
    "actions/action_hat.c"
)
set(APP_REQUIRES
    esp_system
    nvs_flash
    esp_netif
    esp_event
    esp_lcd
    bt
    esp_driver_i2c
    esp_driver_gpio
    esp_driver_spi
    esp_http_client
    mbedtls
    esp_http_server
    esp_wifi
    esp_driver_rmt
    esp_driver_i2s
    esp_adc
)

set(APP_PRIV_REQUIRES
    esp_app_format
)

if(CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS)
    list(APPEND APP_SRCS "transport/transport_ble_gatt.c")
endif()

if(CONFIG_APP_SPEAKER_ACTION)
    list(APPEND APP_SRCS "actions/action_speaker.c")
endif()

if(CONFIG_APP_SOUND_LEVEL_TRIGGERS OR CONFIG_APP_SPEAKER_ACTION OR CONFIG_APP_USB_UAC_DEVICE)
    list(APPEND APP_SRCS
        "audio/board_audio.c"
        "audio/board_audio_clock.c"
        "audio/board_audio_power.c"
        "audio/board_i2s.c"
        "audio/es8311.c"
    )
endif()


if(CONFIG_APP_USB_UAC_DEVICE)
    list(APPEND APP_SRCS
        "usb_audio/uac_audio_buffer.c"
        "usb_audio/uac_config.c"
        "usb_audio/uac_device_adapter.c"
        "usb_audio/uac_esp_device.c"
        "usb_audio/uac_mic_source.c"
        "usb_audio/uac_speaker_sink.c"
        "usb_audio/uac_service.c"
    )
    list(APPEND APP_REQUIRES usb_device_uac)
endif()

if(CONFIG_APP_SOUND_LEVEL_TRIGGERS)
    list(APPEND APP_SRCS
        "audio/audio_metrics.c"
        "audio/sound_level_service.c"
    )
endif()

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS ${APP_INCLUDE_DIRS}
    REQUIRES ${APP_REQUIRES}
    PRIV_REQUIRES ${APP_PRIV_REQUIRES}
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CJSON_NESTING_LIMIT=16)
