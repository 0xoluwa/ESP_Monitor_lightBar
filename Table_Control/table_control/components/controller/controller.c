#include "controller.h"
#include "connection_setup.h"
#include "esp_log.h"

#define WIFI_CFG_CHANNEL 1
#define DEBUG "debug_controller"

void controller_ctor(controller * me){
    fsm *fsm_pointer = (fsm *) me;
    fsm_ctor(fsm_pointer, QUEUE_DEPTH, sizeof(controller_event));
}

void controller_init(controller * me, const char* controller_name){
    fsm *fsm_pointer = (fsm *) me;
    fsm_init(fsm_pointer, controller_name, (state_handler) entry_handler);
}

fsm_state entry_handler (controller *me, fsm_event *event){
    fsm *me_fsm = (fsm *) me;
    return TRAN(brightness_state);
}

fsm_state top_main_state(controller *me, fsm_event *event){
    switch(event->signal){
        case SIG_KNOB_BTN_PRESS:

            app_pkt_t packet = {
                .type = PKT_BRIGHTNESS_EVENT,
                .seq = s_seq++,
                .knob_button_state = LONG_PRESS
            };

            send_packet(&packet);

            ESP_LOGI(DEBUG, "KNOB_BUTTON_LONG_PRESSED from brightness state");

            return STATE_HANDLED;

        default:
            return STATE_IGNORED;
    }

}

fsm_state brightness_state(controller *me, fsm_event *event){
    fsm *me_fsm = (fsm *) me;
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
            ESP_LOGI(DEBUG, "KNOB TURNED: brightness state with value: %d", knob_count);

            state = STATE_HANDLED;
            break;
        
        case SIG_KNOB_BTN_PRESS:
            knob_button_state = ((controller_event *) event)->knob_button_duration;
            if (knob_button_state == SHORT_PRESS){ 
                ESP_LOGI(DEBUG, "KNOB_BUTTON_SHORT_PRESSED from brightness state");
                state = TRAN(preset_state);
            }
            else {
                state = top_main_state(me, event);
            }

            break;

        default:
            state = top_main_state(me, event);
            break;
    }

    return state;
}

fsm_state preset_state(controller *me, fsm_event *event){
    fsm *me_fsm = (fsm *) me;
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
            ESP_LOGI(DEBUG, "KNOB TURNED: preset with value: %d", knob_count);
            state = STATE_HANDLED;
            break;

        case SIG_KNOB_BTN_PRESS:
            knob_button_state = ((controller_event *) event)->knob_button_duration;
            if (knob_button_state == SHORT_PRESS) {
                ESP_LOGI(DEBUG, "KNOB_BUTTON_PRESSED from preset state");
                state = TRAN(brightness_state);
            }
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
        .super = {SIG_KNOB_BTN_PRESS},
        .knob_button_duration = press_duration
    };

    fsm_post_from_isr(me_fsm, (fsm_event *) &event);
}
