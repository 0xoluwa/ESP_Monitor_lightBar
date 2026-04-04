/*
 * main.cpp — application entry point
 *
 * Responsibilities:
 *   1. Configure KY-040 encoder GPIO pins
 *   2. Install ISR handlers (CLK, SW pins)
 *   3. Instantiate and start LedAO
 *   4. Return — FreeRTOS scheduler keeps everything running
 *
 * Pin assignments (ESP32-C3 Supermini — adjust for your board):
 *   PIN_CLK  GPIO4   encoder clock output (A channel)
 *   PIN_DT   GPIO5   encoder data  output (B channel)
 *   PIN_SW   GPIO6   encoder push-button (active LOW)
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "esp_log.h"
#include "esp_timer.h"      // esp_timer_get_time() — safe in ISR context
#include "ESP32Encoder/src/InterruptEncoder.h"

#include "led_ao.hpp" // pulls in ao_v2.hpp, events.hpp, signals.hpp

static const char *TAG = "main";

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------

static constexpr gpio_num_t PIN_CLK = GPIO_NUM_3;
static constexpr gpio_num_t PIN_DT  = GPIO_NUM_4;
static constexpr gpio_num_t PIN_SW  = GPIO_NUM_10;

// ---------------------------------------------------------------------------
// Button debounce threshold
// 50 ms: long enough to suppress contact bounce, short enough to feel instant
// ---------------------------------------------------------------------------

static constexpr int64_t DEBOUNCE_US = 1000 * 500;

// ---------------------------------------------------------------------------
// Global AO instance — static storage duration, lives for the whole program
// ---------------------------------------------------------------------------

static LedAO g_ledAO;



// ---------------------------------------------------------------------------
// ISR: encoder rotation
//
// Triggered on every FALLING edge of CLK (A channel).
// At that moment DT (B channel) is sampled to determine direction:
//
//   CLK falls while DT is still HIGH → clockwise     (CW)
//   CLK falls while DT is already LOW → counter-clockwise (CCW)
//
// This is the single-edge quadrature decode — simple, sufficient for a
// hand-operated encoder at low RPM. For fast mechanical encoders or motors
// use both edges (ANYEDGE) and a full 4-state table.
//
// IRAM_ATTR: function placed in IRAM so it runs even when flash cache is
// busy (e.g. during NVS writes or OTA). All ISRs on ESP-IDF must have this.
// ---------------------------------------------------------------------------

#define micros() esp_timer_get_time()
#define digitalRead(pin) gpio_get_level((gpio_num_t)pin)

InterruptEncoder knob;

void IRAM_ATTR encoderAISR(void * arg) {
    InterruptEncoder * object = (InterruptEncoder *)arg;
    static int64_t previousUS = 0;
    int64_t start = micros();
    int64_t duration = start - previousUS;
    Signal sig = SIG_MAX;
    BaseType_t woken = pdFALSE;

    if (duration >= DEBOUNCE_US) {
        previousUS = start;
        object->microsTimeBetweenTicks = duration;
        object->aState = digitalRead(object->apin);
        object->bState = digitalRead(object->bpin);


        if (object->aState == object->bState){
            //object->count = object->count + 1;
            sig = SIG_KNOB_CW;
        }
        else{
            sig = SIG_KNOB_CCW;
            //object->count = object->count - 1;
        }

        const Event  e   = { sig };

        g_ledAO.postFromISR(e, &woken);
    }

    // If posting unblocked a higher-priority task, yield immediately so that
        // task runs as soon as the ISR exits — this is what keeps latency low.
    portYIELD_FROM_ISR(woken);
}

// ---------------------------------------------------------------------------
// ISR: encoder push-button
//
// Triggered on FALLING edge (button pressed, pin pulled LOW).
// Software debounce via timestamp comparison — no timer required.
//
// Note on the static local `lastUs`:
//   Safe on ESP32-C3 (single-core RISC-V).
//   On dual-core ESP32/S3 two cores could race on this variable.
//   Fix for dual-core: use a portMUX_TYPE spinlock or an atomic<int64_t>.
// ---------------------------------------------------------------------------

static void IRAM_ATTR buttonISR(void * /*arg*/) {
    static int64_t lastUs = 0;

    const int64_t now = esp_timer_get_time();
    if ((now - lastUs) < DEBOUNCE_US) {
        return; // bounce — discard
    }
    lastUs = now;

    const Event e = { SIG_BTN_PRESS };

    BaseType_t woken = pdFALSE;
    g_ledAO.postFromISR(e, &woken);

    portYIELD_FROM_ISR(woken);
}

// ---------------------------------------------------------------------------
// GPIO configuration
//
// gpio_config_t is a struct; we configure pins in two passes:
//   Pass 1 — CLK + DT  (input, pull-up, no interrupt yet)
//   Pass 2 — SW        (input, pull-up, falling-edge interrupt)
//
// CLK gets its interrupt type set separately (gpio_set_intr_type) after the
// first gpio_config call so DT is not accidentally given an interrupt too.
// ---------------------------------------------------------------------------

static void configureButtonGPIO() {
    gpio_config_t io = {};

    // --- SW: input with internal pull-up, falling-edge interrupt ---
    io.pin_bit_mask  = (1ULL << PIN_SW);
    io.mode          = GPIO_MODE_INPUT;
    io.pull_up_en    = GPIO_PULLUP_ENABLE;
    io.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io.intr_type     = GPIO_INTR_NEGEDGE;
    gpio_config(&io);
}

// ---------------------------------------------------------------------------
// app_main — ESP-IDF entry point
//
// extern "C" is mandatory: ESP-IDF's startup code calls app_main() by name
// using a C linkage symbol. Without extern "C", the C++ compiler mangles the
// name and the linker cannot find it.
//
// app_main is allowed to return on ESP-IDF — the FreeRTOS scheduler continues
// running all tasks that have been created. It does NOT need to loop forever.
// ---------------------------------------------------------------------------

void gpio_filter_setup(gpio_num_t gpio_num){
    gpio_glitch_filter_handle_t filter_handle;

    gpio_pin_glitch_filter_config_t filter_config = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = gpio_num
    };

    gpio_new_pin_glitch_filter(&filter_config, &filter_handle);
    gpio_glitch_filter_enable(filter_handle); 
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting encoder/NeoPixel AO demo");
    gpio_filter_setup(PIN_CLK);
    gpio_filter_setup(PIN_DT);
    gpio_filter_setup(PIN_SW);
     
    knob.attach(PIN_CLK, PIN_DT);

    // 1. Install the shared GPIO ISR service.
    //    Must be called once before any gpio_isr_handler_add().
    //    The '0' argument is the ESP_INTR_FLAG — 0 selects a default level
    //    interrupt allocated from IRAM automatically.
    //ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // 2. Configure all GPIO pins
    configureButtonGPIO();

    // 3. Register per-pin ISR handlers
    //    These are called by the shared ISR service dispatcher when the
    //    corresponding pin triggers. Much cheaper than separate ISR vectors.
    //ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_CLK, encoderISR, nullptr));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_SW,  buttonISR,  nullptr));

    // 4. Start the Active Object — spawns the FreeRTOS task.
    //    After this call the AO is live: it receives the initial SIG_ENTRY
    //    and sits blocking on its queue waiting for encoder events.
    g_ledAO.start("LedAO", 8192, 5);

    ESP_LOGI(TAG, "AO started — app_main returning");

    // app_main returns here. The scheduler keeps LedAO's task running.
    // No vTaskDelay(portMAX_DELAY) needed — we have nothing else to do.
}