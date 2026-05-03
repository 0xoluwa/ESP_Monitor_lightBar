# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS main.c
    INCLUDE_DIRS .
    REQUIRES
        freertos
        nvs_flash
        esp_wifi
        esp_netif
        esp_driver_gpio
        esp_driver_ledc
)
