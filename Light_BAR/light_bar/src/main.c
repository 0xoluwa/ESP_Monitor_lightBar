/**
 * @file main.c
 * @brief Application entry point.
 *
 * Constructs the light-bar controller, configures GPIO interrupts for the
 * physical buttons, and hands control to the FSM dispatch task.
 */
#include "controller.h"
#include "config.h"

/** @brief Singleton controller instance; address passed to ISR handlers. */
lightbar_controller device;

/**
 * @brief ISR for the power toggle button GPIO.
 *
 * Fires on the falling edge (button press) and posts ::SIG_POWER to the FSM.
 *
 * @param arg Pointer to the ::lightbar_controller instance.
 */
static void IRAM_ATTR power_btn_isr(void *arg)
{
    post_power_button_isr((lightbar_controller *)arg);
}

/**
 * @brief ISR for the color-temperature preset button GPIO.
 *
 * Fires on the falling edge (button press) and posts ::SIG_COLOR_TEMP_PRESET
 * to the FSM.
 *
 * @param arg Pointer to the ::lightbar_controller instance.
 */
static void IRAM_ATTR preset_btn_isr(void *arg)
{
    post_color_temp_button((lightbar_controller *)arg);
}

/**
 * @brief Configure the power and preset button GPIOs.
 *
 * Both pins are set as inputs with internal pull-ups and falling-edge
 * interrupts.  ISR handlers are registered after the ISR service is installed.
 */
static void gpio_setup(void)
{
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PRESET_TEMP_PIN) | (1ULL << POWER_BUTTON_PIN);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_NEGEDGE; /* trigger on falling edge (button press) */

    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(POWER_BUTTON_PIN, power_btn_isr,  &device));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PRESET_TEMP_PIN,  preset_btn_isr, &device));
}

/**
 * @brief FreeRTOS application entry point.
 *
 * Constructs and initialises the light-bar controller then returns; the FSM
 * task created by lightbar_init() continues to run indefinitely.
 */
void app_main(void)
{
    lightbar_ctor(&device, LED_STRIP_PIN);
    gpio_setup();
    lightbar_init(&device, "device");
}
