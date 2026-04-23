/**
 * @file config.h
 * @brief Compile-time configuration constants for the light-bar controller.
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__

/** @brief Number of addressable LEDs in the strip. */
#define LIGHTBAR_NUM_LEDS           76

/** @brief Number of entries in ::color_temp_lookup (indices 0 – CCT_TABLE_SIZE-1). */
#define CCT_TABLE_SIZE              65

/** @brief Number of entries in the brightness gamma LUT (indices 0 – 100 inclusive). */
#define BRT_TABLE_SIZE              101

/** @brief Animation tick period in milliseconds (50 Hz update rate). */
#define ANIM_TICK_PERIOD_MS         20

/** @brief Depth of the FSM event queue. */
#define QUEUE_DEPTH                 20

/** @brief Maximum allowable color-temperature frame index. */
#define MAX_COLOR_TEMP_FRAME        64

/** @brief Minimum allowable color-temperature frame index. */
#define MIN_COLOR_TEMP_FRAME        20

/** @brief Maximum allowable brightness frame index (full scale). */
#define MAX_BRIGHTNESS_FRAME        255

/** @brief Minimum allowable brightness frame index (prevents a full-off condition in the ON state). */
#define MIN_BRIGHTNESS_FRAME        50

/** @brief Default brightness frame index applied on power-on when no NVS value is found. */
#define BRIGHTNESS_FRAME_DEFAULT    125

/** @brief Default color-temperature frame index applied on power-on when no NVS value is found. */
#define COLOR_TEMP_FRAME_DEFAULT    40

/** @brief Scale factor applied to raw knob brightness deltas received over ESP-NOW. */
#define BRIGHTNESS_MULTIPLIER_PATCH 2

#endif /* __CONFIG_H__ */
