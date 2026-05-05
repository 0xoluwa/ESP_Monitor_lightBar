/**
 * @file config.h
 * @brief Top-level hardware pin assignments for the light-bar application.
 */
#ifndef __SRC_CONFIG_H__
#define __SRC_CONFIG_H__

/** @brief GPIO pin connected to the WS2812 LED strip data line. */
#define LED_STRIP_PIN 2

#define LED_STRIP_COUNT 76

/** @brief GPIO pin for the power toggle button. */
#define POWER_BUTTON_PIN 5

/** @brief GPIO pin for the color-temperature preset cycle button. */
#define PRESET_TEMP_PIN 21

#define LED_STRIP_POWER_PIN 21


#define MIN_BRIGHTNESS_INDEX   (uint8_t ) 50
#define MAX_BRIGHTNESS_INDEX   (uint8_t ) 255
#define DEFAULT_BRIGHTNESS_INDEX    (uint8_t ) 50


#define MIN_RANGE (uint8_t ) 0
#define MAX_RANGE (uint8_t ) 100

#define MIN_TEMP_INDEX (uint8_t ) 15
#define MAX_TEMP_INDEX (uint8_t ) 70
#define DEFAULT_TEMP_INDEX (uint8_t ) 50

#define TEMP_INDEX_PRESET_1 (uint8_t ) 20
#define TEMP_INDEX_PRESET_2 (uint8_t ) 50
#define TEMP_INDEX_PRESET_3  (uint8_t ) 80

#define ANIMATION_PERIOD 20000
#define STORAGE_WRITE_PERIOD 1000000

#define ESPNOW_CHANNEL 1

#define BUTTON_DEBOUNCE_US 10000

#endif /* __SRC_CONFIG_H__ */
