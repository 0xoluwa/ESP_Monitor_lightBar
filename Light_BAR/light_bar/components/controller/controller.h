/**
 * @file controller.h
 * @brief Public API for the light-bar controller.
 *
 * The controller is a flat FSM (built on the ::fsm framework) that manages
 * LED brightness, correlated colour temperature (CCT), smooth animation,
 * NVS persistence, and power state.
 */
#ifndef __LIGHTBAR_CONTROLLER_H__
#define __LIGHTBAR_CONTROLLER_H__

#include "fsm.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "nvs.h"
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "timer_evt.h"

/**
 * @brief HSV look-up table mapping CCT frame index to hue/saturation pairs.
 *
 * Indexed by CCT frame (0 – CCT_TABLE_SIZE-1). Each row is
 * { hue_16bit, saturation_16bit }, ready for led_strip_set_pixel_hsv_16().
 */
extern const uint16_t color_temp_lookup[65][2];

/**
 * @brief Application-level signals dispatched to the light-bar FSM.
 *
 * Values start at ::SIG_USER_CODE so they do not collide with the
 * framework-reserved signals (ENTRY / EXIT / INIT).
 */
enum lightbar_signal {
    SIG_POWER             = SIG_USER_CODE, /**< Power toggle (button or remote). */
    SIG_COLOR_TEMP_PRESET,                 /**< Cycle through three CCT presets. */
    SIG_COLOR_TEMP,                        /**< Continuous CCT adjustment carrying a signed delta. */
    SIG_BRIGHTNESS,                        /**< Continuous brightness adjustment carrying a signed delta. */
    SIG_ANIM_TICK,                         /**< 50 Hz animation tick driving smooth interpolation. */
    SIG_MAX                                /**< Sentinel — total number of application signals. */
};

/**
 * @brief Event structure used by all light-bar signals.
 *
 * Extends ::fsm_event with a signed delta field consumed by
 * ::SIG_COLOR_TEMP and ::SIG_BRIGHTNESS events.
 */
typedef struct {
    fsm_event super; /**< Base event — **must** be the first member. */
    int16_t   delta; /**< Encoder step count; 0 for button-press signals. */
} lightbar_event;

/** @brief Forward declaration of the controller structure. */
typedef struct LIGHTBAR_CONTROLLER lightbar_controller;

/**
 * @brief Light-bar controller state-machine object.
 *
 * Embeds ::fsm as its first member so the controller pointer can be cast to
 * `fsm *` freely throughout the FSM framework.
 */
struct LIGHTBAR_CONTROLLER {
    fsm            super;      /**< Base FSM — **must** be the first member. */
    fsm_time_event anim_timer; /**< Periodic timer that fires ::SIG_ANIM_TICK. */

    gpio_num_t         led_pin;      /**< GPIO driving the LED strip data line. */
    led_strip_handle_t strip_handle; /**< Handle to the LED strip driver. */

    int brt_target_frame; /**< Desired brightness frame index (animation destination). */
    int cct_target_frame; /**< Desired CCT frame index (animation destination). */

    int brt_curr_frame; /**< Current brightness frame index (interpolating toward target). */
    int cct_cur_frame;  /**< Current CCT frame index (interpolating toward target). */

    nvs_handle_t nvs; /**< NVS namespace handle kept open for the controller's lifetime. */
};

/**
 * @brief Construct a ::lightbar_controller.
 *
 * Initialises the embedded FSM and records the LED GPIO pin.  Must be called
 * before lightbar_init().
 *
 * @param me      Pointer to the controller instance to initialise.
 * @param led_pin GPIO number connected to the LED strip.
 */
void lightbar_ctor(lightbar_controller *me, gpio_num_t led_pin);

/**
 * @brief Initialise and start the light-bar controller.
 *
 * Sets up the LED strip driver, opens NVS, initialises ESP-NOW, starts the
 * FSM tick, and creates the FSM dispatch task.  Must be called after
 * lightbar_ctor().
 *
 * @param me        Pointer to the controller instance.
 * @param task_name FreeRTOS task name string.
 */
void lightbar_init(lightbar_controller *me, const char *task_name);

/**
 * @brief Post a power-toggle event from task context.
 * @param me Pointer to the controller instance.
 */
void post_power_button(lightbar_controller *me);

/**
 * @brief Post a power-toggle event from ISR context.
 * @param me Pointer to the controller instance.
 */
void post_power_button_isr(lightbar_controller *me);

/**
 * @brief Post a color-temperature preset cycle event from ISR context.
 * @param me Pointer to the controller instance.
 */
void post_color_temp_button(lightbar_controller *me);

/**
 * @brief Post a continuous color-temperature adjustment event from task context.
 * @param me    Pointer to the controller instance.
 * @param delta Signed step count from the rotary encoder.
 */
void post_color_temp_delta(lightbar_controller *me, int delta);

/**
 * @brief Post a continuous brightness adjustment event from task context.
 * @param me    Pointer to the controller instance.
 * @param delta Signed step count from the rotary encoder.
 */
void post_brightness_delta(lightbar_controller *me, int delta);

#endif /* __LIGHTBAR_CONTROLLER_H__ */
