/*
 * main.cpp — Lightbar application entry point
 *
 * Wires together:
 *   1. GPIO interrupts for power and preset buttons
 *   2. lightbar_controller FSM (handles ESP-NOW internally)
 *
 * Pin assignments (Seeed XIAO ESP32-C3 — adjust as needed):
 *   PIN_POWER_BTN   GPIO_NUM_3   power toggle button (active LOW, pull-up)
 *   PIN_PRESET_BTN  GPIO_NUM_4   preset cycle button (active LOW, pull-up)
 *   PIN_LED_DATA    GPIO_NUM_2   WS2812 data line (SPI MOSI)
 */

extern "C" {
#include "controller.h"
}
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "main";

// ─── Pin definitions ─────────────────────────────────────────────────────────
static constexpr gpio_num_t PIN_POWER_BTN  = GPIO_NUM_3;
static constexpr gpio_num_t PIN_PRESET_BTN = GPIO_NUM_4;
static constexpr gpio_num_t PIN_LED_DATA   = GPIO_NUM_2;

// ─── Global controller instance ──────────────────────────────────────────────
static lightbar_controller g_lb;

// ─── ISR handlers ─────────────────────────────────────────────────────────────
static void IRAM_ATTR power_btn_isr(void *arg) {
    lightbar_post_power_isr((lightbar_controller *)arg);
}

static void IRAM_ATTR preset_btn_isr(void *arg) {
    lightbar_post_preset_isr((lightbar_controller *)arg);
}

// ─── GPIO setup ──────────────────────────────────────────────────────────────
static void gpio_setup(void) {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PIN_POWER_BTN) | (1ULL << PIN_PRESET_BTN);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_NEGEDGE;    // trigger on falling edge (button press)
    gpio_config(&io);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_POWER_BTN,  power_btn_isr,  &g_lb);
    gpio_isr_handler_add(PIN_PRESET_BTN, preset_btn_isr, &g_lb);
}

// ─── Entry point ─────────────────────────────────────────────────────────────
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Lightbar starting");

    // 1. Construct controller (NVS init, LED strip init, FSM queue)
    lightbar_ctor(&g_lb, PIN_POWER_BTN, PIN_PRESET_BTN, PIN_LED_DATA);

    // 2. Install GPIO ISRs for local buttons
    gpio_setup();

    // 3. Start ESP-NOW receiver and FSM task (posts initial SIG_INIT)
    lightbar_init(&g_lb, "lightbar");

    ESP_LOGI(TAG, "Lightbar FSM started — app_main returning");
    // FreeRTOS scheduler keeps the FSM task running.
}
