/**
 * @file main.c
 * @brief Application entry point for the table controller.
 *
 * Owns hardware initialisation and hands off all runtime behaviour to the
 * controller FSM and FreeRTOS callbacks.  The boot sequence is:
 *
 *  1. fsm_tick_init()     – start the 1 ms hardware timer that drives all
 *                           FSM software timers (including the idle timer).
 *  2. controller_ctor()   – construct the controller FSM and idle timer event.
 *  3. controller_init()   – configure RGB LEDs, bring up ESP-NOW, start FSM task.
 *  4. knob_setup()        – configure encoder GPIOs and start the poll timer.
 *  5. knob_button_setup() – configure button GPIO and install the edge ISR.
 *
 * After app_main returns the FreeRTOS scheduler runs the FSM task and the
 * knob poll timer indefinitely.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "knob.h"
#include "config.h"
#include "esp_timer.h"
#include "controller.h"
#include "connection_setup.h"
#include "timer_evt.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "main";

/** @brief FreeRTOS poll timer period in milliseconds. */
#define KNOB_POLL_MS     2

/** @brief Accumulated delta is flushed to the FSM every this many ms (debounce window). */
#define KNOB_FLUSH_MS    100

/** @brief Minimum press duration in microseconds to be classified as a long press. */
#define LONG_PRESS_US    2000000    /* 2 s */

/** @brief Minimum press duration in microseconds for a valid short press (debounce floor). */
#define SHORT_PRESS_US     50000   /* 50 ms */

/** @brief Handle for the FreeRTOS software timer that drives the encoder poll. */
static TimerHandle_t    knob_timer_handle;

/** @brief Encoder state machine handle. */
static encoder_handle_t knob_handle;

/** @brief Global controller FSM instance. */
controller       device;


/* ── Encoder poll timer ───────────────────────────────────────────────────── */

/**
 * @brief FreeRTOS timer callback – poll the encoder and flush accumulated delta.
 *
 * Called every ::KNOB_POLL_MS milliseconds by the FreeRTOS timer task.
 * Ticks the encoder state machine on every call; flushes the accumulated
 * delta to the controller FSM once per ::KNOB_FLUSH_MS window.
 *
 * A flush window of 100 ms means up to 50 poll samples are batched before
 * a ::SIG_KNOB event is posted, providing natural debouncing without
 * introducing noticeable latency for the user.
 *
 * @param xTimer FreeRTOS timer handle (unused).
 */
static void knob_cb(TimerHandle_t xTimer) {
    static uint32_t last_flush_ms = 0;

    encoder_handle_tick(&knob_handle);

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if ((now_ms - last_flush_ms) >= KNOB_FLUSH_MS) {
        last_flush_ms = now_ms;
        int16_t delta = (int16_t)encoder_read_and_clear(&knob_handle);
        if (delta != 0) {
            post_knob_count(&device, delta);
        }
    }
}

/**
 * @brief Configure encoder GPIO pins and start the poll timer.
 *
 * Sets both encoder pins as digital inputs with internal pull-ups enabled.
 * The encoder library does not configure GPIOs itself; they must be ready
 * before ::encoder_fsm_init reads the initial pin state.
 *
 * Creates and starts a periodic FreeRTOS timer that fires every
 * ::KNOB_POLL_MS milliseconds to call ::knob_cb.
 */
static void knob_setup(void) {
    gpio_config_t enc_cfg = {
        .pin_bit_mask = (1ULL << KNOB_CLK_PIN) | (1ULL << KNOB_DATA_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&enc_cfg);

    encoder_fsm_ctor(&knob_handle, KNOB_CLK_PIN, KNOB_DATA_PIN);
    encoder_fsm_init(&knob_handle);

    knob_timer_handle = xTimerCreate("knob",
                                     pdMS_TO_TICKS(KNOB_POLL_MS),
                                     pdTRUE, NULL, knob_cb);
    xTimerStart(knob_timer_handle, 0);
}


/* ── Button ISR ───────────────────────────────────────────────────────────── */

/**
 * @brief GPIO ISR – measures button press duration and classifies it.
 *
 * Triggered on both edges of the button GPIO (active-low with pull-up).
 *
 *  - **Falling edge** (level = 0): record the press start time.
 *  - **Rising edge**  (level = 1): compute duration and classify:
 *    - < ::SHORT_PRESS_US   → noise/glitch; discarded.
 *    - >= ::SHORT_PRESS_US  → ::SHORT_PRESS posted to the controller.
 *    - >= ::LONG_PRESS_US   → ::LONG_PRESS posted to the controller.
 *
 * @param arg Unused GPIO ISR argument.
 */
static void IRAM_ATTR knob_button_isr(void *arg) {
    static int64_t press_start_us = 0;
    int64_t now   = esp_timer_get_time();
    int     level = gpio_get_level((gpio_num_t)KNOB_BUTTON_PIN);

    if (level == 0) {
        /* Falling edge – record press start */
        press_start_us = now;
    } else {
        /* Rising edge – compute and classify duration */
        if (press_start_us == 0) return;    /* missed the press edge – ignore */
        int64_t duration = now - press_start_us;
        press_start_us = 0;

        if (duration >= LONG_PRESS_US)
            post_knob_button(&device, LONG_PRESS);
        else if (duration >= SHORT_PRESS_US)
            post_knob_button(&device, SHORT_PRESS);
        /* else: glitch shorter than debounce floor – discard */
    }
}

/**
 * @brief Configure the button GPIO and install the edge-triggered ISR.
 *
 * Sets ::KNOB_BUTTON_PIN as an input with an internal pull-up and
 * configures it to trigger on any edge so that both press and release
 * transitions are captured for duration measurement.
 *
 * Installs the GPIO ISR service (shared across all GPIO ISRs) and
 * registers ::knob_button_isr for this pin.
 */
static void knob_button_setup(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << KNOB_BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&cfg);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    gpio_isr_handler_add((gpio_num_t)KNOB_BUTTON_PIN, knob_button_isr, NULL);
}


/* ── Entry point ──────────────────────────────────────────────────────────── */

/**
 * @brief FreeRTOS application entry point.
 *
 * Performs the full hardware and software initialisation sequence then
 * returns, leaving the scheduler to drive the FSM task and timer callbacks.
 */
void app_main(void) {
    ESP_LOGI(TAG, "=== table controller booting ===");
    fsm_tick_init(1000);
    ESP_LOGI(TAG, "tick timer started (1 ms)");
    controller_ctor(&device);
    controller_init(&device, "controller");
    knob_setup();
    knob_button_setup();
}
