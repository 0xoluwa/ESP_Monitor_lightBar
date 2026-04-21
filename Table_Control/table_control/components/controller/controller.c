#include "controller.h"
#include "connection_setup.h"
#include "config.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char *TAG = "controller";

#define CONN_RETRY_TICKS  1000U   /* 1 s at 1 ms tick resolution */

void rgb_led_init(){
    ledc_timer_config_t timer_cfg_ = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg_));

    ledc_channel_config_t led_cfg_channel = {
        .gpio_num = (gpio_num_t) LED_RED_PIN,
        .speed_mode =  LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&led_cfg_channel));

    led_cfg_channel.gpio_num = (gpio_num_t) LED_GREEN_PIN;
    led_cfg_channel.channel = LEDC_CHANNEL_1;
    ESP_ERROR_CHECK(ledc_channel_config(&led_cfg_channel));

    led_cfg_channel.gpio_num = (gpio_num_t) LED_BLUE_PIN;
    led_cfg_channel.channel = LEDC_CHANNEL_2;
    ESP_ERROR_CHECK(ledc_channel_config(&led_cfg_channel));

    ESP_ERROR_CHECK(ledc_fade_func_install(0));
}

void power_led(active_led led, led_operation operation){
    ledc_channel_t led_channel;
    uint32_t target_duty;

    switch (led) {
        case RED_LED:
            led_channel = LEDC_CHANNEL_0;
            break;
        case GREEN_LED:
            led_channel = LEDC_CHANNEL_1;
            break;
        case BLUE_LED:
            led_channel = LEDC_CHANNEL_2;
            break;
        default:
            configASSERT(0);
            break;
    }

    switch (operation){
        case POWER_DOWN:
            target_duty = 0;
            break;

        case POWER_ON:
            target_duty = LED_PWM_DUTY;
            break;

        default:
            configASSERT(0);
            break;
    }

    ESP_ERROR_CHECK(ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, led_channel, target_duty, LED_FADE_TIME_MS));
    ESP_ERROR_CHECK(ledc_fade_start(LEDC_LOW_SPEED_MODE, led_channel, LEDC_FADE_NO_WAIT));
}



/* ── post helpers ────────────────────────────────────────────────────────── */

void post_knob_count(controller *me, int knob_count){
    /* NOTE: called from FreeRTOS timer service task — no ESP_LOGI here, */
    controller_event evt = {
        .super.signal = SIG_KNOB,
        .knob_count   = knob_count,
    };
    fsm_post((fsm *)me, (fsm_event *)&evt);
}

void post_knob_button(controller *me, button_duration press_duration){
    controller_event evt = {
        .super.signal         = SIG_KNOB_BTN_PRESS,
        .knob_button_duration = press_duration,
    };
    fsm_post((fsm *)me, (fsm_event *)&evt);
}


/* ── ctor / init ─────────────────────────────────────────────────────────── */

void controller_ctor(controller *me){
    fsm_ctor((fsm *)me, QUEUE_DEPTH, sizeof(controller_event));
    fsm_time_event_ctor(&me->conn_timer, (fsm *)me, TIMEOUT_SIG);
    fsm_time_event_ctor(&me->idle_timer, (fsm *)me, SLEEP_SIG);
    me->active_mode_ = (state_handler)brightness_state;
    ESP_LOGI(TAG, "controller constructed");
}

void controller_init(controller *me, const char *controller_name){
    rgb_led_init();
    ESP_LOGI(TAG, "starting FSM task: %s", controller_name);
    fsm_init((fsm *)me, controller_name, (state_handler)entry_handler);
}


/* ── entry pseudostate ───────────────────────────────────────────────────── */

fsm_state entry_handler(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_INIT: {
            esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
            if (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_GPIO)
                ESP_LOGI(TAG, "woke from deep sleep (cause=%d)", cause);
            else
                ESP_LOGI(TAG, "cold boot");
            return TRAN(connecting_state);
        }
    }
    return STATE_IGNORED;
}


/* ── sleeping_state ──────────────────────────────────────────────────────── */

fsm_state sleeping_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            ESP_LOGI(TAG, "entering deep sleep");

            /* ESP32 WROOM: only RTC GPIOs can wake from deep sleep.
             * GPIO2 (button) is RTC-capable; GPIO3 (CLK) is not.
             * EXT0 wakes on a single GPIO going LOW (button pressed).  */
            esp_sleep_enable_ext0_wakeup((gpio_num_t)KNOB_BUTTON_PIN, 0);
            esp_deep_sleep_start();
            break;
    }
    return STATE_IGNORED;
}


/* ── awake_state ─────────────────────────────────────────────────────────── */

fsm_state awake_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SLEEP_SIG:
            return TRAN(sleeping_state);
        case DISCONNECTED_SIG:
            return TRAN(connecting_state);
    }
    return STATE_IGNORED;
}


/* ── connecting_state ────────────────────────────────────────────────────── */

fsm_state connecting_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            power_led(RED_LED, POWER_ON);
            ESP_LOGI(TAG, "[connecting] LED=red, waiting for ESP-NOW ready");
            fsm_time_event_arm(&me->conn_timer, CONN_RETRY_TICKS, 0);
            break;

        case SIG_INIT:
            // if (is_connected()) return TRAN(idle_state);
            break;

        case TIMEOUT_SIG:
            // ESP-NOW is fire-and-forget — no handshake needed.
            // 1 s red LED on boot is just a visual "ready" indicator.
            ESP_LOGI(TAG, "[connecting] ESP-NOW ready → idle");
            return TRAN(idle_state);

        case SIG_EXIT:
            power_led(RED_LED, POWER_DOWN);
            fsm_time_event_disarm(&me->conn_timer);
            break;

        default:
            return SUPER(awake_state, event);
    }
    return STATE_HANDLED;
}


/* ── idle_state ──────────────────────────────────────────────────────────── */

fsm_state idle_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            power_led(GREEN_LED, POWER_ON);
            ESP_LOGI(TAG, "[idle] armed 30 s sleep timer");
            fsm_time_event_arm(&me->idle_timer, IDLE_TIMEOUT_TICKS, 0);
            break;

        case SIG_INIT:
            return TRAN(top_main_state);

        case SIG_EXIT:
            power_led(GREEN_LED, POWER_DOWN);
            fsm_time_event_disarm(&me->idle_timer);
            break;

        default:
            return SUPER(awake_state, event);
    }
    return STATE_HANDLED;
}


/* ── top_main_state ──────────────────────────────────────────────────────── */

fsm_state top_main_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:   /* composite state — no physical entry action */
        case SIG_EXIT:    /* composite state — no physical exit action  */
            return STATE_HANDLED;

        case SIG_INIT:
            return TRAN(me->active_mode_);

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;
            if (ce->knob_button_duration == LONG_PRESS){
                ESP_LOGI(TAG, "[top_main] long press → TX power toggle");
                fsm_post((fsm *)me, event);
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
            ESP_LOGI(TAG, "[brightness] mode active");
            break;

        case SIG_KNOB: {
            controller_event *ce = (controller_event *)event;
            ESP_LOGI(TAG, "[brightness] knob delta=%d → TX", ce->knob_count);
            ce->knob_current_signal = BRIGTHNESS;
            fsm_post((fsm *)me, event);
            return TRAN(tx_state);
        }

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;
            if (ce->knob_button_duration == SHORT_PRESS){
                ESP_LOGI(TAG, "[brightness] short press → preset mode");
                return TRAN(preset_state);
            }
            return SUPER(top_main_state, event);  // LONG_PRESS → delegate up
        }

        case SIG_INIT:
        case SIG_EXIT:
            return STATE_HANDLED;

        default:
            return SUPER(top_main_state, event);
    }
    return STATE_HANDLED;
}


/* ── preset_state ────────────────────────────────────────────────────────── */

fsm_state preset_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            me->active_mode_ = (state_handler)preset_state;
            ESP_LOGI(TAG, "[preset] mode active");
            break;

        case SIG_KNOB: {
            controller_event *ce = (controller_event *)event;
            ESP_LOGI(TAG, "[preset] knob delta=%d → TX", ce->knob_count);
            ce->knob_current_signal = COLOR_TEMP;
            fsm_post((fsm *)me, event);
            return TRAN(tx_state);
        }

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;
            if (ce->knob_button_duration == SHORT_PRESS){
                ESP_LOGI(TAG, "[preset] short press → brightness mode");
                return TRAN(brightness_state);
            }
            return SUPER(top_main_state, event);  // LONG_PRESS → delegate up
        }

        case SIG_INIT:
        case SIG_EXIT:
            return STATE_HANDLED;

        default:
            return SUPER(top_main_state, event);
    }
    return STATE_HANDLED;
}


/* ── tx_state ────────────────────────────────────────────────────────────── */

fsm_state tx_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            power_led(BLUE_LED, POWER_ON);
            break;

        case SIG_KNOB: {
            controller_event *ce = (controller_event *)event;
            bool is_brt = (ce->knob_current_signal == BRIGTHNESS);
            app_pkt_t pkt = {
                .seq        = s_seq++,
                .knob_delta = (int16_t)ce->knob_count,
                .type       = is_brt ? PKT_BRIGHTNESS_EVENT : PKT_PRESET_EVENT,
            };
            ESP_LOGI(TAG, "[tx] %s delta=%d seq=%u",
                     is_brt ? "BRIGHTNESS" : "CCT", pkt.knob_delta, pkt.seq);
            send_packet(&pkt);
            
            ESP_LOGI(TAG, "[tx] done → idle");
            return TRAN(idle_state);
            break;
        }

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;
            app_pkt_t pkt = {
                .seq               = s_seq++,
                .type              = PKT_KNOB_BUTTON,
                .knob_button_state = ce->knob_button_duration,
            };
            ESP_LOGI(TAG, "[tx] POWER seq=%u", pkt.seq);
            send_packet(&pkt);

            ESP_LOGI(TAG, "[tx] done → idle");
            return TRAN(idle_state);
            break;
        }
            
        case SIG_EXIT:
            power_led(BLUE_LED, POWER_DOWN);
            break;

        default:
            return SUPER(awake_state, event);
    }
    return STATE_HANDLED;
}
