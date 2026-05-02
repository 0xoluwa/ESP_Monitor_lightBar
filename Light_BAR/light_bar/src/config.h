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


#define MIN_BRIGHTNESS_INDEX    10
#define MAX_BRIGHTNESS_INDEX    200
#define DEFAULT_BRIGHTNESS_INDEX    130

#define MIN_TEMP_INDEX  20
#define MAX_TEMP_INDEX  70
#define DEFAULT_TEMP_INDEX  30

#define TEMP_INDEX_PRESET_1 30
#define TEMP_INDEX_PRESET_2 45
#define TEMP_INDEX_PRESET_3 60

#define ANIMATION_PERIOD 20000
#define STORAGE_WRITE_PERIOD 1000000

#define ESPNOW_CHANNEL 1

#define BUTTON_DEBOUNCE_US 10000

#endif /* __SRC_CONFIG_H__ */
