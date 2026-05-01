#include "config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs_flash.h"
#include "lookup_table.h"
#include "esp_timer.h"


TaskHandle_t led_task_handle = NULL;
TaskHandle_t storage_task_handle = NULL;

QueueHandle_t led_queue;
QueueHandle_t storage_queue;

static int clip_range(int num, int min, int max){
  if (num < min) return min;
  else if (num > max) return max;
  else return num;
}

typedef enum {
  ANIM_TICK_SIG,
  POWER_SIG,
  KNOB_SIG,
  COLOR_TEMP_SIG,
  MAX_SIG
} signal;

typedef struct {
  signal event_sig;
  int brightness_index;
  int color_temp_index;
  bool power_state;
} led_message_t;

typedef enum {
  ON,
  OFF,
  MAX_POWER_STATE
} power_state;

typedef struct {
  int brightness_index;
  int color_temp_index;
} storage_message_t;

void led_task(void *pvParameters);
void storage_task(void *pvParameters);

void app_main(void) {}

void led_task(void *pvParameters) {
  int current_brightness_index = 0;
  int current_color_temp_index = 0;
  int target_brightness_index = 0;
  int target_color_temp_index = 0;
  power_state power_state_ = OFF;

  led_message_t led_message = {0};
  led_strip_handle_t led_strip = NULL;

  led_strip_config_t strip_config = {
      .strip_gpio_num = LED_STRIP_PIN,
      .max_leds = LED_STRIP_LED_COUNT,
      .led_model = LED_MODEL_WS2812,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
      .flags = {
          .invert_out = false, // don't invert the output signal
      }};

  // LED strip backend configuration: SPI
  led_strip_spi_config_t spi_config = {.clk_src = SPI_CLK_SRC_DEFAULT,
                                       .spi_bus = SPI2_HOST,
                                       .flags = {
                                           .with_dma = true,
                                       }};
  ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));

  while (1) {
    xQueueReceive(led_queue, &led_message, portMAX_DELAY);
    switch (led_message.event_sig) {
    case POWER_SIG:{
      if (power_state_ == ON){
        target_brightness_index = 0;
        target_color_temp_index = 0;

        power_state_ = OFF;
      }else{
        power_state_ = ON;
      }

      if ((current_brightness_index != target_brightness_index) || (current_color_temp_index != target_color_temp_index)){
        xQueueSend(led_queue, &((led_message_t) {.event_sig = ANIM_TICK_SIG}), 0);
      }

      break;
    }

    case KNOB_SIG:{
      target_brightness_index += led_message.brightness_index;
      target_color_temp_index += led_message.color_temp_index;
      target_brightness_index = clip_range(target_brightness_index, MIN_BRIGHTNESS_INDEX, MAX_BRIGHTNESS_INDEX);
      target_color_temp_index = clip_range(target_color_temp_index, MIN_TEMP_INDEX, MAX_TEMP_INDEX);

      if ((current_brightness_index != target_brightness_index) || (current_color_temp_index != target_color_temp_index)){
        xQueueSend(led_queue, &((led_message_t) {.event_sig = ANIM_TICK_SIG}), 0);
      }
      break;
    }

    case COLOR_TEMP_SIG:{
      if ((target_color_temp_index < TEMP_INDEX_PRESET_1) || (target_color_temp_index > TEMP_INDEX_PRESET_3)) target_color_temp_index = TEMP_INDEX_PRESET_1;
      else if ((target_color_temp_index >= TEMP_INDEX_PRESET_1) && (target_color_temp_index < TEMP_INDEX_PRESET_2)) target_color_temp_index = TEMP_INDEX_PRESET_2;
      else if ((target_color_temp_index >= TEMP_INDEX_PRESET_2) && (target_color_temp_index < TEMP_INDEX_PRESET_3)) target_color_temp_index = TEMP_INDEX_PRESET_3;

      if ((current_brightness_index != target_brightness_index) || (current_color_temp_index != target_color_temp_index)){
        xQueueSend(led_queue, &((led_message_t) {.event_sig = ANIM_TICK_SIG}), 0);
      }

      break;
    }

    case ANIM_TICK_SIG:

      break;

    default:
      break;
    }
  }
}

void storage_task(void *pvParameters) {

  while (1) {
  }
}
