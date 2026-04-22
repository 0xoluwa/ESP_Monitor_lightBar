#include "controller.h"
#include "config.h"


lightbar_controller device;

static void IRAM_ATTR power_btn_isr(void * arg) {
    post_power_button_isr((lightbar_controller *) arg);
}

static void IRAM_ATTR preset_btn_isr(void * arg) {
    post_color_temp_button((lightbar_controller *) arg);
}

static void gpio_setup(void) {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PRESET_TEMP_PIN) | (1ULL << POWER_BUTTON_PIN);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_NEGEDGE;    // trigger on falling edge (button press)

    ESP_ERROR_CHECK(gpio_config(&io));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(POWER_BUTTON_PIN,  power_btn_isr,  &device));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PRESET_TEMP_PIN, preset_btn_isr, &device));
}


void app_main(){
    lightbar_ctor(&device, LED_STRIP_PIN);
    gpio_setup();
    lightbar_init(&device, "device");
}