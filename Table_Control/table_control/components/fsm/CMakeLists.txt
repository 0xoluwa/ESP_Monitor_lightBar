idf_component_register(
    SRCS "fsm.c" "timer_evt.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_timer
        freertos
)