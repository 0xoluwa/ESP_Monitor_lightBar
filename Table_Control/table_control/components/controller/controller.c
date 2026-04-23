/**
 * @file controller.c
 * @brief Device controller FSM – state handlers and helper functions.
 *
 * Implements the hierarchical state machine described in controller.h.
 * All state handlers follow the state_handler signature and communicate
 * back to the framework via the ::fsm_state return codes.
 */

#include "controller.h"
#include "connection_setup.h"
#include "config.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char *TAG = "controller";

/* ── Forward declarations ─────────────────────────────────────────────────── */
static fsm_state entry_handler(controller *me, fsm_event *event);
static fsm_state awake_state(controller *me, fsm_event *event);
static fsm_state sleeping_state(controller *me, fsm_event *event);
static fsm_state tx_state(controller *me, fsm_event *event);


/* ── RGB LED helpers ──────────────────────────────────────────────────────── */

/**
 * @brief Configure the LEDC peripheral for the three RGB channels.
 *
 * Creates a single 8-bit, 1 kHz LEDC timer and binds channels 0–2 to the
 * red, green, and blue LED pins respectively.  Installs the fade ISR so
 * that ::power_led can use hardware-accelerated fading.
 */
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

/**
 * @brief Start a PWM fade on one RGB LED channel.
 *
 * Maps the @p led selector to its LEDC channel, sets the target duty
 * (0 or ::LED_PWM_DUTY), and starts a non-blocking fade over
 * ::LED_FADE_TIME_MS milliseconds.
 *
 * @param led       Channel to control (::RED_LED, ::GREEN_LED, ::BLUE_LED).
 * @param operation ::POWER_ON to fade up; ::POWER_DOWN to fade to 0.
 */
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


/* ── Post helpers ─────────────────────────────────────────────────────────── */

/**
 * @brief Enqueue a ::SIG_KNOB event from task context.
 *
 * @param me         Controller FSM.
 * @param knob_count Signed delta (positive = CW, negative = CCW).
 */
void post_knob_count(controller *me, int knob_count){
    controller_event evt = {
        .super.signal = SIG_KNOB,
        .knob_count   = knob_count,
    };
    fsm_post((fsm *)me, (fsm_event *)&evt);
}

/**
 * @brief Enqueue a ::SIG_KNOB_BTN_PRESS event from an ISR.
 *
 * @param me             Controller FSM.
 * @param press_duration ::SHORT_PRESS or ::LONG_PRESS.
 */
void IRAM_ATTR post_knob_button(controller *me, button_duration press_duration){
    controller_event evt = {
        .super.signal         = SIG_KNOB_BTN_PRESS,
        .knob_button_duration = press_duration,
    };
    fsm_post_from_isr((fsm *)me, (fsm_event *)&evt);
}


/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/**
 * @brief Construct the controller FSM instance.
 *
 * Creates the event queue, constructs the idle timer event bound to
 * ::SLEEP_SIG, and sets the initial knob mode to ::BRIGHTNESS.
 *
 * @param me Controller instance to construct.
 */
void controller_ctor(controller *me){
    fsm_ctor((fsm *)me, QUEUE_DEPTH, sizeof(controller_event));
    fsm_time_event_ctor(&me->idle_timer, (fsm *)me, SLEEP_SIG);
    me->knob_button_press_state_ = BRIGHTNESS;
    ESP_LOGI(TAG, "controller constructed");
}

/**
 * @brief Initialise hardware peripherals and start the FSM task.
 *
 * Must be called after ::controller_ctor.  Brings up the RGB LED LEDC
 * driver and the ESP-NOW link, then spawns the FSM dispatch task.
 *
 * @param me              Controller instance.
 * @param controller_name FreeRTOS task name for the dispatch task.
 */
void controller_init(controller *me, const char *controller_name){
    rgb_led_init();
    espnow_init(me);
    ESP_LOGI(TAG, "starting FSM task: %s", controller_name);
    fsm_init((fsm *)me, controller_name, (state_handler)entry_handler);
}


/* ── State handlers ───────────────────────────────────────────────────────── */

/**
 * @brief Initial pseudo-state – determines first real state on SIG_INIT.
 *
 * Logs whether the device woke from deep sleep (EXT0 / GPIO wakeup) or
 * performed a cold boot, then immediately transitions to tx_state.
 *
 * @param me    Controller instance.
 * @param event Incoming event (only SIG_INIT is handled here).
 * @return ::STATE_TRANSITION on SIG_INIT; ::STATE_IGNORED otherwise.
 */
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

/**
 * @brief Deep-sleep entry state.
 *
 * On SIG_ENTRY: enables EXT0 wakeup on the button GPIO (active low) and
 * calls esp_deep_sleep_start().  The function does not return; the next
 * execution restarts from app_main.
 *
 * @param me    Controller instance.
 * @param event Incoming event.
 * @return ::STATE_IGNORED (SIG_ENTRY does not return normally).
 */
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

/**
 * @brief Super-state for all active (non-sleeping) behaviour.
 *
 * Acts as a catch-all parent for tx_state.  Handles ::SLEEP_SIG to
 * transition the machine into sleeping_state.  All other events are
 * returned as ::STATE_IGNORED for further bubbling.
 *
 * @param me    Controller instance.
 * @param event Incoming event.
 * @return ::STATE_TRANSITION on ::SLEEP_SIG; ::STATE_IGNORED otherwise.
 */
fsm_state awake_state(controller *me, fsm_event *event){
    switch (event->signal){
        case SLEEP_SIG:
            return TRAN(sleeping_state);
    }
    return STATE_IGNORED;
}

/**
 * @brief Active transmission state – handles all normal user interactions.
 *
 * | Signal              | Action                                                        |
 * |---------------------|---------------------------------------------------------------|
 * | SIG_ENTRY           | Turn on blue LED; arm 30 s idle timer.                        |
 * | SIG_KNOB            | Build and send a brightness or CCT packet; rearm idle timer.  |
 * | SIG_KNOB_BTN_PRESS  | Short: toggle mode + rearm timer. Long: send power packet.   |
 * | CONNECTED_SIG       | Blue LED on, red LED off (ACK received).                      |
 * | DISCONNECTED_SIG    | Blue LED off, red LED on (no ACK).                            |
 * | SIG_EXIT            | Disarm idle timer; turn off blue LED.                         |
 * | (unhandled)         | Delegate to awake_state via SUPER().                          |
 *
 * @param me    Controller instance.
 * @param event Incoming event.
 * @return ::STATE_HANDLED for handled signals; result of awake_state for unhandled ones.
 */
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
