#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include "fsm.h"
#include "timer_evt.h"
#include "driver/gpio.h"


#define QUEUE_DEPTH         20
#define IDLE_TIMEOUT_TICKS  30000U   /* 30 s at 1 ms tick resolution */

typedef enum {
    SHORT_PRESS,
    LONG_PRESS
} button_duration;

typedef enum {
    RED_LED,
    BLUE_LED,
    GREEN_LED
} active_led;

typedef enum{
    POWER_DOWN,
    POWER_ON
} led_operation;

typedef enum{
    BRIGHTNESS = 0,
    COLOR_TEMP = 1
} knob_transmit_signal_t;

typedef struct controller_evt {
    fsm_event super;
    union {
        int knob_count;
        button_duration knob_button_duration;
    };
}controller_event;

enum controller_signal {
    SIG_KNOB_BTN_PRESS = SIG_USER_CODE,     // encoder SW pressed (debounced)
    SIG_KNOB,
    SLEEP_SIG,
    CONNECTED_SIG,
    DISCONNECTED_SIG,
    SIG_MAX
};

typedef struct CONTROLLER controller;

struct CONTROLLER{
    fsm super;
    fsm_time_event  idle_timer;   /* inactivity → deep sleep timer       — posts SLEEP_SIG   */
    knob_transmit_signal_t knob_button_press_state_;
};

void controller_ctor(controller * me);
void controller_init(controller * me, const char* controller_name);

void post_knob_count(controller * me, int knob_count);
void post_knob_button(controller *me, button_duration press_duration);



#endif