/**
 * @file config.h
 * @brief Top-level hardware pin assignments for the light-bar application.
 */
#ifndef __SRC_CONFIG_H__
#define __SRC_CONFIG_H__

/** @brief GPIO pin connected to the WS2812 LED strip data line. */
#define LED_STRIP_PIN    2

/** @brief GPIO pin for the power toggle button. */
#define POWER_BUTTON_PIN 5

/** @brief GPIO pin for the color-temperature preset cycle button. */
#define PRESET_TEMP_PIN  21

#endif /* __SRC_CONFIG_H__ */
