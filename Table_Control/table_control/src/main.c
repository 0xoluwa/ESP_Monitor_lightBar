#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "knob.h"
#include "config.h"
#include "esp_timer.h"
#include "controller.h"
#include "driver/gpio.h"

#define TRIGGER_FREQUENCY_MS 5
#define LONG_PRESS_TIME 2000
#define SHORT_PRESS_TIME 10

static TimerHandle_t knob_timer_handle;
static encoder_handle_t knob_handle;
static controller device;

#define POST_INTERVAL_MS 500     // Desired reporting interval
#define TICKS_TO_WAIT (POST_INTERVAL_MS / TRIGGER_FREQUENCY_MS)

static void knob_cb(TimerHandle_t xTimer) {
    static uint32_t tick_counter = 0;
    
    encoder_handle_tick(&knob_handle);
    tick_counter++;

    if (tick_counter >= TICKS_TO_WAIT) {
        int16_t delta = encoder_read_and_clear(&knob_handle);

        if (delta != 0) {
            post_knob_count(&device, delta);
        }

        tick_counter = 0; // Reset the window
    }
}

static void knob_setup(){
    encoder_fsm_ctor(&knob_handle, KNOB_CLK_PIN, KNOB_DATA_PIN);
    encoder_fsm_init(&knob_handle);

    TickType_t frequency = pdMS_TO_TICKS(TRIGGER_FREQUENCY_MS);
    xTimerCreate("knob", frequency, pdTRUE, &knob_timer_handle, knob_cb);
}

static void device_setup(){
    controller_ctor(&device);
    controller_init(&device, "main_device");
}

static void IRAM_ATTR knob_button_isr(void* arg) {
    uint64_t current_time = esp_timer_get_time();
    static uint64_t press_start_time = 0;
    
    int level = gpio_get_level((gpio_num_t)KNOB_BUTTON_PIN);

    if (level == 1) { 
        press_start_time = current_time;
    } else {
        if (press_start_time == 0) return; // Ignore release if we missed the press

        uint64_t duration = current_time - press_start_time;

        if (duration >= LONG_PRESS_TIME) {
            post_knob_button(&device, LONG_PRESS);
        } else if (duration >= SHORT_PRESS_TIME) {
            post_knob_button(&device, SHORT_PRESS);
        }
        
        press_start_time = 0;
    }
}

static void knob_button_setup() {
    gpio_config_t knob_button_gpio_config = {
        .pin_bit_mask = (1ULL << KNOB_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE // Triggers when button is pressed (to High)
    };

    gpio_config(&knob_button_gpio_config);

    // Install ISR service only if not already installed
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    

    // Add the handler for this specific pin
    gpio_isr_handler_add((gpio_num_t)KNOB_BUTTON_PIN, knob_button_isr, NULL);
}

void app_main() {
    device_setup();
    knob_setup();
    knob_button_setup();
}