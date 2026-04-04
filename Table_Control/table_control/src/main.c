#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "knob.h"
#include "config.h"
#include "esp_timer.h"
#include "controller.h"

#define TRIGGER_FREQUENCY_MS 5

static TimerHandle_t knob_timer_handle;
static encoder_handle_t knob_handle;
static controller device;

static void knob_cb(TimerHandle_t xTimer){
    static int16_t previousCount;
    int16_t currentCount;

    encoder_handle_tick(&knob_handle);
    currentCount = knob_handle.delta;

    if (currentCount != previousCount) {
        currentCount = encoder_read_and_clear(&knob_handle);
        post_knob_count(&device, currentCount);

        previousCount = currentCount;
    }
}

static void knob_setup(){
    encoder_fsm_ctor(&knob_handle, KNOB_CLK_PIN, KNOB_DATA_PIN);
    encoder_fsm_init(&knob_handle);

    TickType_t frequency = pdMS_TO_TICKS(TRIGGER_FREQUENCY_MS);
    xTimerCreate("knob", frequency, pdTRUE, NULL, knob_cb);
}

static void device_setup(){
    controller_ctor(&device);
    controller_init(&device, "main_device");
}
void app_main() {
    knob_setup();

}