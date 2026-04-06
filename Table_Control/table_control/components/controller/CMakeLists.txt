idf_component_register(
    SRCS "controller.c"
    INCLUDE_DIRS "."

    REQUIRES
        fsm
        esp_driver_gpio
        nvs_flash

    PRIV_REQUIRES
        esp_wifi
        esp_common
)