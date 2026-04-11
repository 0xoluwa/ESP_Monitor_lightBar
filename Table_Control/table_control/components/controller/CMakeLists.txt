idf_component_register(
    SRCS "controller.c" "connection_setup.c"
    INCLUDE_DIRS "."

    REQUIRES
        fsm
        esp_driver_gpio
        nvs_flash
        esp_wifi
        esp_netif

    PRIV_REQUIRES
        esp_common
)