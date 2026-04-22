#include "controller.h"
#include "connection_setup.h"
#include "config.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char *TAG = "controller";

static fsm_state entry_handler(controller *me, fsm_event *event);
static fsm_state awake_state(controller * me, fsm_event * event);
static fsm_state sleeping_state(controller * me, fsm_event * event);
static fsm_state tx_state(controller *me, fsm_event * event);

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
    fsm_post_from_isr((fsm *)me, (fsm_event *)&evt);
}


void controller_ctor(controller *me){
    fsm_ctor((fsm *)me, QUEUE_DEPTH, sizeof(controller_event));
    fsm_time_event_ctor(&me->idle_timer, (fsm *)me, SLEEP_SIG);
    me->knob_button_press_state_ = BRIGHTNESS;
    ESP_LOGI(TAG, "controller constructed");
}

void controller_init(controller *me, const char *controller_name){
    rgb_led_init();
    espnow_init(me);
    ESP_LOGI(TAG, "starting FSM task: %s", controller_name);
    fsm_init((fsm *)me, controller_name, (state_handler)entry_handler);
}

fsm_state entry_handler(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_INIT: {
            esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
            if (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_GPIO)
                ESP_LOGI(TAG, "woke from deep sleep (cause=%d)", cause);
            else
                ESP_LOGI(TAG, "cold boot");
            return TRAN(tx_state);
        }
    }
    return STATE_IGNORED;
}

fsm_state sleeping_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SIG_ENTRY:
            ESP_LOGI(TAG, "entering deep sleep");

            esp_sleep_enable_ext0_wakeup((gpio_num_t)KNOB_BUTTON_PIN, 0);
            esp_deep_sleep_start();
            break;
    }
    return STATE_IGNORED;
}

fsm_state awake_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SLEEP_SIG:
            return TRAN(sleeping_state);
    }
    return STATE_IGNORED;
}

fsm_state tx_state(controller *me, fsm_event *event)
{
    switch (event->signal){
        case SIG_ENTRY:
            power_led(BLUE_LED, POWER_ON);
            ESP_LOGI(TAG, "[tx] armed 30 s sleep timer");
            fsm_time_event_arm(&me->idle_timer, IDLE_TIMEOUT_TICKS, 0);
            break;

        case SIG_KNOB: {
            controller_event *ce = (controller_event *)event;
            bool is_brt = (me->knob_button_press_state_ == BRIGHTNESS);

            app_pkt_t pkt = {
                .seq        = s_seq++,
                .knob_delta = (int16_t)ce->knob_count,
                .type       = is_brt ? PKT_BRIGHTNESS_EVENT : PKT_PRESET_EVENT,
            };

            ESP_LOGI(TAG, "[tx] %s delta=%d seq=%u",
                     is_brt ? "BRIGHTNESS" : "CCT", pkt.knob_delta, pkt.seq);
            send_packet(&pkt);

            fsm_time_event_rearm(&me->idle_timer, IDLE_TIMEOUT_TICKS);
            break;
        }

        case SIG_KNOB_BTN_PRESS: {
            controller_event *ce = (controller_event *)event;

            if (ce->knob_button_duration == SHORT_PRESS){
                me->knob_button_press_state_ = (me->knob_button_press_state_ == BRIGHTNESS) ? COLOR_TEMP : BRIGHTNESS;
                fsm_time_event_rearm(&me->idle_timer, IDLE_TIMEOUT_TICKS);
                break;
            }

            app_pkt_t pkt = {
                .seq               = s_seq++,
                .type              = PKT_KNOB_BUTTON,
                .knob_button_state = ce->knob_button_duration,
            };
            ESP_LOGI(TAG, "[tx] POWER seq=%u", pkt.seq);
            send_packet(&pkt);

            fsm_time_event_rearm(&me->idle_timer, IDLE_TIMEOUT_TICKS);
            break;
        }

        case DISCONNECTED_SIG:
            power_led(BLUE_LED, POWER_DOWN);
            power_led(RED_LED, POWER_ON);
            break;
        
        case CONNECTED_SIG:
            power_led(BLUE_LED, POWER_ON);
            power_led(RED_LED, POWER_DOWN);
            break;
            
        case SIG_EXIT:
            fsm_time_event_disarm(&me->idle_timer);
            power_led(BLUE_LED, POWER_DOWN);
            break;

        default:
            return SUPER(awake_state, event);
    }
    return STATE_HANDLED;
}