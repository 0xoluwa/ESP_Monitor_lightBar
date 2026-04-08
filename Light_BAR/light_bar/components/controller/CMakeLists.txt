idf_component_register(
    SRCS "controller.c"
    INCLUDE_DIRS "."

    REQUIRES
        fsm
        led_strip
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_driver_gpio
)
