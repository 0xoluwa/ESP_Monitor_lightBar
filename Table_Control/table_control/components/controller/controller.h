#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include "fsm.h"
#include "timer_evt.h"
#include "driver/gpio.h"
#include "led_strip.h"


#define QUEUE_DEPTH         20
#define IDLE_TIMEOUT_TICKS  30000U   /* 30 s at 1 ms tick resolution */

typedef enum : uint8_t {
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
    BRIGTHNESS = 0,
    COLOR_TEMP
} knob_transmit_signal_t;

typedef struct controller_evt {
    fsm_event super;
    union {
        int knob_count;
        uint8_t knob_button_duration;
    };

    knob_transmit_signal_t knob_current_signal;
}controller_event;

enum controller_signal : uint8_t {
    SIG_KNOB_BTN_PRESS = SIG_USER_CODE,     // encoder SW pressed (debounced)
    SIG_KNOB,
    AWAKE_SIG,
    SLEEP_SIG,
    DISCONNECTED_SIG,
    IDLE_SIG,
    TIMEOUT_SIG,
    TX_DONE_SIG,
    SIG_MAX
};

typedef struct CONTROLLER controller;

void controller_ctor(controller * me);
void controller_init(controller * me, const char* controller_name);

fsm_state entry_handler (controller *me, fsm_event *event);
fsm_state connecting_state(controller * me, fsm_event * event);

fsm_state awake_state(controller * me, fsm_event * event);
fsm_state sleeping_state(controller * me, fsm_event * event);
fsm_state idle_state(controller * me, fsm_event * event);
fsm_state tx_state(controller *me, fsm_event * event);


fsm_state top_main_state(controller *me, fsm_event *event);
fsm_state brightness_state(controller *me, fsm_event *event);
fsm_state preset_state(controller *me, fsm_event *event);



void post_knob_count(controller * me, int knob_count);
void post_knob_button(controller *me, button_duration press_duration);

struct CONTROLLER{
    fsm super;

    gpio_num_t strip_data_pin;
    led_strip_handle_t strip;
    fsm_time_event  conn_timer;   /* connection retry / timeout timer    — posts TIMEOUT_SIG */
    fsm_time_event  idle_timer;   /* inactivity → deep sleep timer       — posts SLEEP_SIG   */
    state_handler   active_mode_; /* last active brightness/preset state — restored on re-entry */
};



#endif