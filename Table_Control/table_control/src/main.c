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

#define KNOB_POLL_MS     2      
#define KNOB_FLUSH_MS    100      // net delta transmitted every this many ms (debounce window)
#define LONG_PRESS_US    2000000    // 2 s in microseconds
#define SHORT_PRESS_US     50000    // 50 ms debounce floor

static TimerHandle_t    knob_timer_handle;
static encoder_handle_t knob_handle;
controller       device;

// ─── Encoder poll timer ───────────────────────────────────────────────────────
// Runs every 10 ms. Sends any accumulated delta immediately — no batching.
// 1 encoder detent = 1 frame on the light bar with negligible latency.

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

static void knob_setup(void) {
    // Configure encoder pins as inputs with pull-ups before reading them.
    // The encoder library does NOT configure GPIOs itself.
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

// ─── Button ISR ───────────────────────────────────────────────────────────────
// Measures press duration on release edge.
// SHORT_PRESS → toggle brightness/preset mode.
// LONG_PRESS  → send power packet to light bar.

static void IRAM_ATTR knob_button_isr(void *arg) {
    static int64_t press_start_us = 0;
    int64_t now   = esp_timer_get_time();
    int     level = gpio_get_level((gpio_num_t)KNOB_BUTTON_PIN);

    if (level == 0) {
        // Falling edge — button pressed
        press_start_us = now;
    } else {
        // Rising edge — button released
        if (press_start_us == 0) return;    // missed the press, ignore
        int64_t duration = now - press_start_us;
        press_start_us = 0;

        if (duration >= LONG_PRESS_US)
            post_knob_button(&device, LONG_PRESS);
        else if (duration >= SHORT_PRESS_US)
            post_knob_button(&device, SHORT_PRESS);
        // else: noise / glitch shorter than debounce floor — discard
    }
}

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

// ─── Entry point ──────────────────────────────────────────────────────────────

void app_main(void) {
    ESP_LOGI(TAG, "=== table controller booting ===");
    fsm_tick_init(1000);
    ESP_LOGI(TAG, "tick timer started (1 ms)");
    controller_ctor(&device);
    controller_init(&device, "controller");
    knob_setup();
    knob_button_setup();
}
