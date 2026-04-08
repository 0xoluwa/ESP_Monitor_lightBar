#include "controller.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_err.h"
#include "connection_setup.h"

#define WIFI_CFG_CHANNEL 1

void controller_ctor(controller * me){
    fsm *fsm_pointer = (fsm *) me;
    fsm_ctor(fsm_pointer, QUEUE_DEPTH, sizeof(controller_event));
}

void controller_init(controller * me, const char* controller_name){
    espnow_init();
    wait_for_connection();
    fsm *fsm_pointer = (fsm *) me;
    fsm_init(fsm_pointer, controller_name, entry_handler);
}

fsm_state entry_handler (fsm *me, fsm_event *event){
    return TRAN(brightness_state);
}

fsm_state top_main_state(fsm *me, fsm_event *event){
    switch(event->signal){
        case SIG_KNOB_BTN_PRESS:
            app_pkt_t packet = {
                .type = PKT_BRIGHTNESS_EVENT,
                .seq = s_seq++,
                .knob_button_state = LONG_PRESS
            };
            send_packet(&packet);

            return STATE_HANDLED;

        default:
            return STATE_IGNORED;
    }

}

fsm_state brightness_state(fsm *me, fsm_event *event){
    fsm_state state;
    int knob_count;
    uint8_t knob_button_state;

    switch(event->signal){      
        case SIG_KNOB:
            knob_count = ((controller_event *) event)->knob_count;

            app_pkt_t packet = {
                .type = PKT_BRIGHTNESS_EVENT,
                .seq = s_seq++,
                .knob_delta = knob_count
            };

            send_packet(&packet);

            state = STATE_HANDLED;
            break;
        
        case SIG_KNOB_BTN_PRESS:
            knob_button_state = ((controller_event *) event)->knob_button_duration;
            if (knob_button_state == SHORT_PRESS) state = TRAN(preset_state);
            else state = top_main_state(me, event);
            break;

        default:
            state = top_main_state(me, event);
            break;
    }

    return state;
}

fsm_state preset_state(fsm *me, fsm_event *event){
    fsm_state state;
    int knob_count;
    uint8_t knob_button_state;

    switch(event->signal){
        case SIG_KNOB:
            knob_count = ((controller_event *) event)->knob_count;

            app_pkt_t packet = {
                .type = PKT_PRESET_EVENT,
                .seq = s_seq++,
                .knob_delta = knob_count
            };

            send_packet(&packet);
        
            state = STATE_HANDLED;
            break;

        case SIG_KNOB_BTN_PRESS:
            knob_button_state = ((controller_event *) event)->knob_button_duration;
            if (knob_button_state == SHORT_PRESS) state = TRAN(brightness_state);
            else state = top_main_state(me, event);
            break;

        default:
            state = top_main_state(me, event);
            break;
    }

    return state;
}

void post_knob_count(controller * me, int knob_count){
    fsm *me_fsm = (fsm *) me;

    controller_event event = {
        .super = {SIG_KNOB},
        .knob_count = knob_count
    };

    fsm_post(me_fsm, (fsm_event *) &event);
}

void post_knob_button(controller *me, button_duration press_duration){
    fsm *me_fsm = (fsm *) me;

    controller_event event = {
        .super = {SIG_KNOB},
        .knob_button_duration = press_duration
    };

    fsm_post_from_isr(me_fsm, (fsm_event *) &event);
}
