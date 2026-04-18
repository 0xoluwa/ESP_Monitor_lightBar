#include "controller.h"
#include "connection_setup.h"
#include "config.h"
#include "esp_sleep.h"
#include "esp_log.h"

static const char *TAG = "controller";

#define CONN_RETRY_TICKS  1000U   /* 1 s at 1 ms tick resolution */


/* ── helpers ─────────────────────────────────────────────────────────────── */

void post_knob_count(controller *me, int knob_count){
    controller_event evt = {
        .super.signal = SIG_KNOB,
        .knob_count   = knob_count,
    };
    /* called from FreeRTOS timer task — non-blocking so we never stall that task */
    fsm_post_nonblock((fsm *)me, (fsm_event *)&evt);
}

void post_knob_button(controller *me, button_duration press_duration){
    controller_event evt = {
        .super.signal         = SIG_KNOB_BTN_PRESS,
        .knob_button_duration = press_duration,
    };
    /* called from GPIO ISR */
    fsm_post_from_isr((fsm *)me, (fsm_event *)&evt);
}


/* ── ctor / init ─────────────────────────────────────────────────────────── */

void controller_ctor(controller *me){
    fsm_ctor((fsm *)me, QUEUE_DEPTH, sizeof(controller_event));
    fsm_time_event_ctor(&me->conn_timer, (fsm *)me, TIMEOUT_SIG);
    fsm_time_event_ctor(&me->idle_timer, (fsm *)me, SLEEP_SIG);
    me->active_mode_ = (state_handler)brightness_state; /* default mode */
}

void controller_init(controller *me, const char *controller_name){
    fsm_init((fsm *)me, controller_name, (state_handler)entry_handler);
}


/* ── entry pseudostate ───────────────────────────────────────────────────── */

fsm_state entry_handler(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_INIT: {
            esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
            if (cause == ESP_SLEEP_WAKEUP_GPIO) {
                ESP_LOGI(TAG, "woke from deep sleep");
            } else {
                ESP_LOGI(TAG, "cold boot");
            }
            return TRAN(connecting_state);
        }
    }
    return STATE_IGNORED;
}


/* ── sleeping_state ──────────────────────────────────────────────────────── */

fsm_state sleeping_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY: {
            ESP_LOGI(TAG, "entering deep sleep");

            /* Wake on knob button (active-low, pullup) OR knob CLK toggling LOW.
             * ESP32-C3 uses esp_deep_sleep_enable_gpio_wakeup instead of ext1. */
            const uint64_t wake_mask = (1ULL << KNOB_BUTTON_PIN)
                                     | (1ULL << KNOB_CLK_PIN);
            esp_deep_sleep_enable_gpio_wakeup(wake_mask, ESP_GPIO_WAKEUP_GPIO_LOW);

            /* SIG_EXIT will never fire — deep sleep is a restart */
            esp_deep_sleep_start();
            break;  /* unreachable, silences compiler */
        }

        /* AWAKE_SIG and SIG_EXIT are unreachable: the device restarts on wakeup.
         * entry_handler detects ESP_SLEEP_WAKEUP_EXT1 and routes to connecting_state. */
    }
    return STATE_IGNORED;
}


/* ── awake_state  (top-level parent of connecting / idle / tx) ───────────── */

fsm_state awake_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            break;

        case SLEEP_SIG:
            return TRAN(sleeping_state);

        case DISCONNECTED_SIG:
            return TRAN(connecting_state);

        case SIG_EXIT:
            break;
    }
    return STATE_IGNORED;
}


/* ── connecting_state ────────────────────────────────────────────────────── */

fsm_state connecting_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            /* TODO: turn red LED on */
            fsm_time_event_arm(&me->conn_timer, CONN_RETRY_TICKS, 0);
            break;

        case SIG_INIT:
            /* skip straight to idle if we are already paired */
            // if (is_connected()) return TRAN(idle_state);
            break;

        case CONNECTED_SIG:
            return TRAN(idle_state);

        case TIMEOUT_SIG:
            /* retry: rearm and re-attempt pairing */
            fsm_time_event_rearm(&me->conn_timer, CONN_RETRY_TICKS);
            /* TODO: trigger ESP-NOW re-pairing / broadcast */
            break;

        case SIG_EXIT:
            fsm_time_event_disarm(&me->conn_timer);
            /* TODO: turn red LED off */
            break;

        default:
            return SUPER(awake_state, event);
    }
    return STATE_HANDLED;
}


/* ── idle_state  (container — green LED on, dives into top_main_state) ───── */

fsm_state idle_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            /* TODO: turn green LED on */
            fsm_time_event_arm(&me->idle_timer, IDLE_TIMEOUT_TICKS, 0);
            break;

        case SIG_INIT:
            return TRAN(top_main_state);

        case SIG_EXIT:
            fsm_time_event_disarm(&me->idle_timer);
            /* TODO: turn green LED off */
            break;

        default:
            return SUPER(awake_state, event);
    }
    return STATE_HANDLED;
}


/* ── top_main_state  (restores the last active brightness/preset mode) ───── */

fsm_state top_main_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_INIT:
            /* return to whichever mode was last active */
            return TRAN(me->active_mode_);

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;
            if (ce->knob_button_duration == LONG_PRESS) {
                /* re-post so tx_state receives it and sends the power packet */
                fsm_post_nonblock((fsm *)me, event);
                return TRAN(tx_state);
            }
            break;
        }

        default:
            return SUPER(idle_state, event);
    }
    return STATE_HANDLED;
}


/* ── brightness_state ────────────────────────────────────────────────────── */

fsm_state brightness_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            me->active_mode_ = (state_handler)brightness_state;
            /* TODO: indicate brightness mode (e.g. LED colour / blink) */
            break;

        case SIG_KNOB: {
            controller_event *ce = (controller_event *)event;
            ce->knob_current_signal = BRIGTHNESS;
            fsm_post_nonblock((fsm *)me, event);
            return TRAN(tx_state);
        }

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;
            if (ce->knob_button_duration == SHORT_PRESS)
                return TRAN(preset_state);
            /* LONG_PRESS falls through to parent (awake → sleep) */
            break;
        }

        case SIG_EXIT:
            break;

        default:
            return SUPER(top_main_state, event);
    }
    return STATE_HANDLED;
}


/* ── preset_state  (color temperature mode) ─────────────────────────────── */

fsm_state preset_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            me->active_mode_ = (state_handler)preset_state;
            /* TODO: indicate color-temp mode */
            break;

        case SIG_KNOB: {
            controller_event *ce = (controller_event *)event;
            ce->knob_current_signal = COLOR_TEMP;
            fsm_post_nonblock((fsm *)me, event);
            return TRAN(tx_state);
        }

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;
            if (ce->knob_button_duration == SHORT_PRESS)
                return TRAN(brightness_state);
            break;
        }

        case SIG_EXIT:
            break;

        default:
            return SUPER(top_main_state, event);
    }
    return STATE_HANDLED;
}


/* ── tx_state ────────────────────────────────────────────────────────────── */

fsm_state tx_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            /* TODO: pulse blue LED */
            break;

        case SIG_KNOB: {
            controller_event *ce = (controller_event *)event;
            app_pkt_t pkt = {
                .seq        = s_seq++,
                .knob_delta = (int16_t)ce->knob_count,
                .type       = (ce->knob_current_signal == BRIGTHNESS)
                                ? PKT_BRIGHTNESS_EVENT
                                : PKT_PRESET_EVENT,
            };
            send_packet(&pkt);

            /* self-post TX_DONE so the transition happens in the next dispatch cycle */
            controller_event done = { .super.signal = TX_DONE_SIG };
            fsm_post_nonblock((fsm *)me, (fsm_event *)&done);
            break;
        }

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;
            app_pkt_t pkt = {
                .seq               = s_seq++,
                .type              = PKT_KNOB_BUTTON,
                .knob_button_state = ce->knob_button_duration,
            };
            send_packet(&pkt);

            controller_event done = { .super.signal = TX_DONE_SIG };
            fsm_post_nonblock((fsm *)me, (fsm_event *)&done);
            break;
        }

        case TX_DONE_SIG:
            return TRAN(idle_state);

        case SIG_EXIT:
            /* TODO: turn blue LED off */
            break;

        default:
            return SUPER(awake_state, event);
    }
    return STATE_HANDLED;
}
