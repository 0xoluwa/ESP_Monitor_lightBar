idf_component_register(
    SRCS "controller.c" "connection_setup.c"
    INCLUDE_DIRS "."

    REQUIRES
        fsm
        led_strip
        esp_driver_gpio
        esp_driver_rmt
        esp_driver_ledc
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_timer

    PRIV_REQUIRES
        esp_common
)