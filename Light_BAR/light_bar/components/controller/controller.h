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


extern const uint16_t color_temp_lookup[65][2];


// ─── Signals ─────────────────────────────────────────────────────────────────
enum lightbar_signal {
    SIG_POWER      = SIG_USER_CODE,
    SIG_COLOR_TEMP_PRESET,
    SIG_COLOR_TEMP,
    SIG_BRIGHTNESS,
    // 50 Hz tick from FreeRTOS timer → drives smooth interpolation
    SIG_ANIM_TICK,
    SIG_MAX
};

typedef struct{
    fsm_event super;    // MUST be first member; contains uint8_t signal
    int16_t   delta;    // encoder delta; 0 for button-press signals
} lightbar_event;

// ─── Forward declaration ─────────────────────────────────────────────────────
typedef struct LIGHTBAR_CONTROLLER lightbar_controller;

// ─── Controller struct ────────────────────────────────────────────────────────
struct LIGHTBAR_CONTROLLER {
    fsm super;   
    fsm_time_event  anim_timer;   

    gpio_num_t led_pin;
    led_strip_handle_t strip_handle;

    int brt_target_frame;
    int cct_target_frame; 

    int brt_curr_frame;         
    int cct_cur_frame;       

    // NVS handle (kept open for the lifetime of the controller)
    nvs_handle_t nvs;
};

// ─── Public API ──────────────────────────────────────────────────────────────
void lightbar_ctor(lightbar_controller *me, gpio_num_t led_pin);
void lightbar_init(lightbar_controller *me, const char *task_name);

void post_power_button(lightbar_controller *me);
void post_power_button_isr(lightbar_controller *me);
void post_color_temp_button(lightbar_controller *me);
void post_color_temp_delta(lightbar_controller *me, int delta);
void post_brightness_delta(lightbar_controller *me, int delta);




#endif // __LIGHTBAR_CONTROLLER_H__
