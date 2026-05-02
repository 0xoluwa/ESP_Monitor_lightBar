#include "config.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs_flash.h"
#include "lookup_table.h"
#include "esp_timer.h"
#include "driver/gpio.h"


TaskHandle_t led_task_handle = NULL;

QueueHandle_t led_queue;

esp_timer_handle_t led_anim_timer;
esp_timer_handle_t storage_write_timer;

nvs_handle_t storage_handle;

static void gpio_setup(void);

static void espnow_init(void);
static void recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);

const char * temp_index_key = "temp_index";
const char * brightness_index_key = "brightness_index";

void led_animation_callback(void * args);
void storage_write_callback(void * args);

static inline void timeout_init(esp_timer_handle_t *timer_handle, esp_timer_cb_t user_callback, const char * timer_name){
  esp_timer_create_args_t timer_cfg = {
    .callback = user_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = timer_name,
    .skip_unhandled_events = true
  };

  ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, timer_handle));
}

static inline void storage_init(){
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(nvs_open("LED STORAGE", NVS_READWRITE, &storage_handle));
}

static int clip_range(int num, int min, int max){
  if (num < min) return min;
  else if (num > max) return max;
  else return num;
}

/**
 * @brief Packet type discriminator carried in every ::app_pkt_t frame.
 */
typedef enum __attribute__((packed)) {
    PKT_BRIGHTNESS_EVENT = 0x01, /**< Rotary-encoder brightness step. */
    PKT_COLOR_TEMP_EVENT = 0x02, /**< Rotary-encoder CCT step. */
    PKT_KNOB_BUTTON      = 0x03, /**< Knob push-button press. */
} pkt_type_t;

/**
 * @brief Fixed-size ESP-NOW application packet.
 *
 * Packed to ensure the sender and receiver agree on the exact byte layout
 * regardless of compiler padding rules.
 */
typedef struct __attribute__((packed)) {
    pkt_type_t type; /**< Identifies the event carried by this packet. */
    uint8_t    seq;  /**< Sequence number (reserved for future deduplication). */
    union {
        int16_t knob_delta;        /**< Signed encoder step — valid for PKT_BRIGHTNESS_EVENT and PKT_COLOR_TEMP_EVENT. */
        uint8_t knob_button_state; /**< Button state — valid for PKT_KNOB_BUTTON. */
    };
} app_pkt_t;

typedef enum {
  ANIM_TICK_SIG,
  POWER_SIG,
  KNOB_SIG,
  COLOR_TEMP_SIG,
  STORAGE_SIG,
  MAX_SIG
} signal;

typedef struct {
  signal event_sig;
  int brightness_index;
  int color_temp_index;
} led_message_t;

typedef enum {
  ON,
  OFF,
  MAX_POWER_STATE
} power_state;

rgb_t cct_apply_brightness(rgb_t cct_rgb, uint8_t brightness) {
    return (rgb_t){
        .red = (uint8_t)((cct_rgb.red * brightness + 127) / 255),
        .green = (uint8_t)((cct_rgb.green * brightness + 127) / 255),
        .blue = (uint8_t)((cct_rgb.blue * brightness + 127) / 255),
    };
}

void led_task(void *pvParameters);

void led_task(void *pvParameters) {
  uint8_t current_brightness_index = 0;
  uint8_t current_color_temp_index = 0;
  uint8_t target_brightness_index = 0;
  uint8_t target_color_temp_index = 0;
  power_state power_state_ = OFF;
  rgb_t base_color;
  base_color.red = color_temp_lookup[current_color_temp_index][0];
  base_color.green = color_temp_lookup[current_color_temp_index][1];
  base_color.blue = color_temp_lookup[current_color_temp_index][2];

  led_message_t led_message = {0};
  led_strip_handle_t led_strip = NULL;

  led_strip_config_t strip_config = {
      .strip_gpio_num = LED_STRIP_PIN,
      .max_leds = LED_STRIP_COUNT,
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

  timeout_init(&led_anim_timer, led_animation_callback, "ANIMATION TIMER");
  timeout_init(&storage_write_timer, storage_write_callback, "STORAGE WRITE");

  esp_err_t ret;
  ret = nvs_get_u8(storage_handle, temp_index_key, &target_color_temp_index);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    target_color_temp_index = DEFAULT_TEMP_INDEX;
    ESP_ERROR_CHECK(nvs_set_u8(storage_handle, temp_index_key, DEFAULT_TEMP_INDEX));
    ESP_ERROR_CHECK(nvs_commit(storage_handle));
  }
  else ESP_ERROR_CHECK(ret);

  ret = nvs_get_u8(storage_handle, brightness_index_key, &target_brightness_index);
  if (ret == ESP_ERR_NVS_NOT_FOUND){
    target_brightness_index = DEFAULT_BRIGHTNESS_INDEX;
    ESP_ERROR_CHECK(nvs_set_u8(storage_handle, brightness_index_key, DEFAULT_BRIGHTNESS_INDEX));
    ESP_ERROR_CHECK(nvs_commit(storage_handle));
  }
  else ESP_ERROR_CHECK(ret);

  while (1) {
    xQueueReceive(led_queue, &led_message, portMAX_DELAY);
    switch (led_message.event_sig) {
    case POWER_SIG:{
      esp_err_t stop_ret = esp_timer_stop(storage_write_timer);
      if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(stop_ret);

      if (power_state_ == ON){
        target_brightness_index = 0;

        power_state_ = OFF;
      }else{
        power_state_ = ON;

        ESP_ERROR_CHECK(nvs_get_u8(storage_handle, temp_index_key, &target_color_temp_index));
        ESP_ERROR_CHECK(nvs_get_u8(storage_handle, brightness_index_key, &target_brightness_index));
      }

      if ((current_brightness_index != target_brightness_index) || (current_color_temp_index != target_color_temp_index)){
        xQueueSend(led_queue, &((led_message_t) {.event_sig = ANIM_TICK_SIG}), 0);
      }

      break;
    }

    case KNOB_SIG:{
      if (power_state_ == OFF) break;
      
      target_brightness_index = clip_range((target_brightness_index + led_message.brightness_index), MIN_BRIGHTNESS_INDEX, MAX_BRIGHTNESS_INDEX);
      target_color_temp_index = clip_range((target_color_temp_index + led_message.color_temp_index), MIN_TEMP_INDEX, MAX_TEMP_INDEX);

      if ((current_brightness_index != target_brightness_index) || (current_color_temp_index != target_color_temp_index)){
        xQueueSend(led_queue, &((led_message_t) {.event_sig = ANIM_TICK_SIG}), 0);
        
        esp_err_t stop_ret = esp_timer_stop(storage_write_timer);
        if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(stop_ret);
        ESP_ERROR_CHECK(esp_timer_start_once(storage_write_timer, STORAGE_WRITE_PERIOD));
      }
      break;
    }

    case COLOR_TEMP_SIG:{
      if (power_state_ == OFF) break;
      if ((target_color_temp_index < TEMP_INDEX_PRESET_1) || (target_color_temp_index >= TEMP_INDEX_PRESET_3)) target_color_temp_index = TEMP_INDEX_PRESET_1;
      else if ((target_color_temp_index >= TEMP_INDEX_PRESET_1) && (target_color_temp_index < TEMP_INDEX_PRESET_2)) target_color_temp_index = TEMP_INDEX_PRESET_2;
      else if ((target_color_temp_index >= TEMP_INDEX_PRESET_2) && (target_color_temp_index < TEMP_INDEX_PRESET_3)) target_color_temp_index = TEMP_INDEX_PRESET_3;

      if ((current_brightness_index != target_brightness_index) || (current_color_temp_index != target_color_temp_index)){
        xQueueSend(led_queue, &((led_message_t) {.event_sig = ANIM_TICK_SIG}), 0);
        esp_err_t stop_ret = esp_timer_stop(storage_write_timer);
        if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(stop_ret);
        ESP_ERROR_CHECK(esp_timer_start_once(storage_write_timer, STORAGE_WRITE_PERIOD));
      }

      break;
    }

    case ANIM_TICK_SIG:{
      bool refresh = false;
      
      if (current_color_temp_index != target_color_temp_index){
        if (current_color_temp_index < target_color_temp_index) current_color_temp_index++;
        else if (current_color_temp_index > target_color_temp_index) current_color_temp_index--;

        base_color.red = color_temp_lookup[current_color_temp_index][0];
        base_color.green = color_temp_lookup[current_color_temp_index][1];
        base_color.blue = color_temp_lookup[current_color_temp_index][2];

        refresh = true;
      }

      if (current_brightness_index != target_brightness_index){
        if (current_brightness_index < target_brightness_index) current_brightness_index++;
        else if (current_brightness_index > target_brightness_index) current_brightness_index--;
        refresh = true;
      }

      if (refresh){
        rgb_t final_color = cct_apply_brightness(base_color, current_brightness_index);
        for(int i = 0; i < LED_STRIP_COUNT; i++){ 
          ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, final_color.red, final_color.green, final_color.blue));
        }

        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        esp_err_t stop_ret = esp_timer_stop(led_anim_timer);
        if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(stop_ret);
        ESP_ERROR_CHECK(esp_timer_start_once(led_anim_timer, ANIMATION_PERIOD));
      }
      break;
    }

    case STORAGE_SIG:{
      if (power_state_ == OFF) break;
      ESP_ERROR_CHECK(nvs_set_u8(storage_handle, temp_index_key, target_color_temp_index));
      ESP_ERROR_CHECK(nvs_set_u8(storage_handle, brightness_index_key, target_brightness_index));
      ESP_ERROR_CHECK(nvs_commit(storage_handle));
      break;
    }
    default:
      break;
    }
  }
}

void led_animation_callback(void *args){
  xQueueSend(led_queue, &((led_message_t) {.event_sig = ANIM_TICK_SIG}), 0);
}

void storage_write_callback(void * args){
  xQueueSend(led_queue, &((led_message_t) {.event_sig = STORAGE_SIG}), 0);
}

void espnow_init(void)
{
    /* NVS flash init occurred in main before this call. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); /* disable power-save — ESP-NOW needs radio always-on */

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));
}

static void recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    if (data_len != sizeof(app_pkt_t)) return;
    const app_pkt_t *msg = (const app_pkt_t *)data;
    led_message_t message = {0};
    switch (msg->type) {
        case PKT_BRIGHTNESS_EVENT:
          message.brightness_index = msg->knob_delta;
          message.event_sig = KNOB_SIG;
          xQueueSend(led_queue, &message, 0);
          break;

        case PKT_COLOR_TEMP_EVENT:
          message.color_temp_index = msg->knob_delta;
          message.event_sig = KNOB_SIG;
          xQueueSend(led_queue, &message, 0);
          break;

        case PKT_KNOB_BUTTON:
          message.event_sig = POWER_SIG;
          xQueueSend(led_queue, &message, 0);
          break;

        default:
          break;
    }
}

static void IRAM_ATTR power_btn_isr(void *arg)
{
    static int64_t last_trigger_us = 0;
    int64_t now = esp_timer_get_time();

    if ((now - last_trigger_us) < BUTTON_DEBOUNCE_US) return;   // 50 ms guard
    last_trigger_us = now;

    xQueueSendFromISR(led_queue, &((led_message_t){.event_sig = POWER_SIG}), NULL);
}

static void IRAM_ATTR preset_btn_isr(void *arg)
{
    static int64_t last_trigger_us_preset = 0;
    int64_t now = esp_timer_get_time();

    if ((now - last_trigger_us_preset) < BUTTON_DEBOUNCE_US) return;   // 50 ms guard
    last_trigger_us_preset = now;

    xQueueSendFromISR(led_queue, &((led_message_t){.event_sig = COLOR_TEMP_SIG}), NULL);
}

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
    ESP_ERROR_CHECK(gpio_isr_handler_add(POWER_BUTTON_PIN, power_btn_isr,  NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PRESET_TEMP_PIN,  preset_btn_isr, NULL));
}

void app_main(void) {
  storage_init();
  led_queue = xQueueCreate(20, sizeof(led_message_t));
  configASSERT(led_queue != NULL);
  espnow_init();
  gpio_setup();

  xTaskCreate(
    &led_task,
    "led task",
    8192,
    NULL,
    2,
    &led_task_handle
  );
}
