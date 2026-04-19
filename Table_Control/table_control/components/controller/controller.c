#include "controller.h"
#include "connection_setup.h"
#include "config.h"
#include "esp_sleep.h"
#include "esp_log.h"

static const char *TAG = "controller";

#define CONN_RETRY_TICKS  1000U   /* 1 s at 1 ms tick resolution */

/* ── LED colour constants (R, G, B) ─────────────────────────────────────── */
#define LED_RED        80,  0,  0
#define LED_GREEN       0, 60,  0
#define LED_BLUE        0,  0, 80
#define LED_AMBER      80, 30,  0   /* warm — preset / CCT mode              */
#define LED_COOL_WHITE 40, 50, 80   /* cool — brightness mode                */
#define LED_WHITE      80, 80, 80   /* TX flash                              */
#define LED_OFF         0,  0,  0


/* ── LED strip helpers ───────────────────────────────────────────────────── */

static void strip_setup(controller *me){
    led_strip_config_t cfg = {
        .strip_gpio_num         = STRIP_DATA_PIN,
        .max_leds               = CTRL_NUM_LEDS,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags                  = { .invert_out = 0 },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src        = RMT_CLK_SRC_DEFAULT,
        .resolution_hz  = 10 * 1000 * 1000,   /* 10 MHz */
        .flags          = { .with_dma = 0 },
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt_cfg, &me->strip));
    led_strip_clear(me->strip);
    led_strip_refresh(me->strip);
}

static void strip_set_all(controller *me, uint8_t r, uint8_t g, uint8_t b){
    for (int i = 0; i < CTRL_NUM_LEDS; i++)
        led_strip_set_pixel(me->strip, i, r, g, b);
    led_strip_refresh(me->strip);
}

static void strip_clear(controller *me){
    led_strip_clear(me->strip);
    led_strip_refresh(me->strip);
}


/* ── post helpers ────────────────────────────────────────────────────────── */

void post_knob_count(controller *me, int knob_count){
    ESP_LOGI(TAG, "knob delta=%d", knob_count);
    controller_event evt = {
        .super.signal = SIG_KNOB,
        .knob_count   = knob_count,
    };
    fsm_post_nonblock((fsm *)me, (fsm_event *)&evt);
}

void post_knob_button(controller *me, button_duration press_duration){
    ESP_DRAM_LOGI(TAG, "button %s", press_duration == LONG_PRESS ? "LONG" : "SHORT");
    controller_event evt = {
        .super.signal         = SIG_KNOB_BTN_PRESS,
        .knob_button_duration = press_duration,
    };
    fsm_post_from_isr((fsm *)me, (fsm_event *)&evt);
}


/* ── ctor / init ─────────────────────────────────────────────────────────── */

void controller_ctor(controller *me){
    me->pin.knob_clk_pin   = KNOB_CLK_PIN;
    me->pin.knob_data_pin  = KNOB_DATA_PIN;
    me->pin.knob_btn_pin   = KNOB_BUTTON_PIN;
    me->pin.strip_data_pin = STRIP_DATA_PIN;

    strip_setup(me);
    ESP_LOGI(TAG, "LED strip ready on GPIO %d", STRIP_DATA_PIN);

    fsm_ctor((fsm *)me, QUEUE_DEPTH, sizeof(controller_event));
    fsm_time_event_ctor(&me->conn_timer, (fsm *)me, TIMEOUT_SIG);
    fsm_time_event_ctor(&me->idle_timer, (fsm *)me, SLEEP_SIG);
    me->active_mode_ = (state_handler)brightness_state;
    ESP_LOGI(TAG, "controller constructed");
}

void controller_init(controller *me, const char *controller_name){
    ESP_LOGI(TAG, "starting FSM task: %s", controller_name);
    fsm_init((fsm *)me, controller_name, (state_handler)entry_handler);
}


/* ── entry pseudostate ───────────────────────────────────────────────────── */

fsm_state entry_handler(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_INIT: {
            esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
            if (cause == ESP_SLEEP_WAKEUP_GPIO)
                ESP_LOGI(TAG, "woke from deep sleep");
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
            strip_clear(me);

            const uint64_t wake_mask = (1ULL << KNOB_BUTTON_PIN)
                                     | (1ULL << KNOB_CLK_PIN);
            esp_deep_sleep_enable_gpio_wakeup(wake_mask, ESP_GPIO_WAKEUP_GPIO_LOW);
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
            ESP_LOGI(TAG, "[connecting] LED=red, waiting for ESP-NOW ready");
            strip_set_all(me, LED_RED);
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
            fsm_time_event_disarm(&me->conn_timer);
            strip_clear(me);
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
            ESP_LOGI(TAG, "[idle] armed 30 s sleep timer");
            strip_set_all(me, LED_GREEN);   /* solid green = awake and ready */
            fsm_time_event_arm(&me->idle_timer, IDLE_TIMEOUT_TICKS, 0);
            break;

        case SIG_INIT:
            return TRAN(top_main_state);

        case SIG_EXIT:
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
            ESP_LOGI(TAG, "[brightness] mode active");
            break;

        case SIG_KNOB: {
            controller_event *ce = (controller_event *)event;
            ESP_LOGI(TAG, "[brightness] knob delta=%d → TX", ce->knob_count);
            ce->knob_current_signal = BRIGTHNESS;
            fsm_post_nonblock((fsm *)me, event);
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
            fsm_post_nonblock((fsm *)me, event);
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
            strip_set_all(me, LED_WHITE);
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
            ESP_LOGI(TAG, "[tx] POWER seq=%u", pkt.seq);
            send_packet(&pkt);
            controller_event done = { .super.signal = TX_DONE_SIG };
            fsm_post_nonblock((fsm *)me, (fsm_event *)&done);
            break;
        }

        case TX_DONE_SIG:
            ESP_LOGI(TAG, "[tx] done → idle");
            return TRAN(idle_state);

        case SIG_EXIT:
            strip_set_all(me, LED_GREEN);   /* restore ready indicator after TX flash */
            break;

        default:
            return SUPER(awake_state, event);
    }
    return STATE_HANDLED;
}
