idf_component_register(
    SRCS "knob.c" 
    INCLUDE_DIRS "."

    REQUIRES 
        esp_driver_gpio
)