#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include "fsm.h"
#include "driver/gpio.h"


#define QUEUE_DEPTH 10

typedef struct controller_evt{
    fsm_event super;
    union {
        int knob_count;
    }signal_data;
}controller_event;

enum controller_signal : uint8_t {
    SIG_KNOB_BTN_PRESS = SIG_USER_CODE,     // encoder SW pressed (debounced)
    SIG_KNOB,
    SIG_BRIGHT_BTN_PRESS,
    SIG_PRESET_BTN_PRESS,
    SIG_CUSTOM_BTN_PRESS,
    SIG_MAX
};

fsm_state top_main_state(fsm *me, fsm_event *event);
fsm_state brightness_state(fsm *me, fsm_event *event);
fsm_state preset_state(fsm *me, fsm_event *event);

fsm_state custom_state(fsm *me, fsm_event *event);
fsm_state custom_red(fsm *me, fsm_event *event);
fsm_state custom_green(fsm *me, fsm_event *event);
fsm_state custom_blue(fsm *me, fsm_event *event);

fsm_state entry_handler (fsm *me, fsm_event *event);

typedef struct CONTROLLER controller;

void controller_ctor(controller * me);
void controller_init(controller * me, const char* controller_name);

void post_knob_count(controller * me, int knob_count);
void post_bright_btn(controller * me);
void post_preset_btn(controller * me);
void post_custom_btn(controller * me);



struct CONTROLLER{
    fsm super;

    struct {
        gpio_num_t knob_clk_pin;
        gpio_num_t knob_data_pin;
        gpio_num_t strip_data_pin;
        gpio_num_t brightness_btn_pin;
        gpio_num_t preset_btn_pin;
        gpio_num_t custom_btn_pin;
    } pin;
};



#endif