#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_now.h"
#include "config.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include <string.h>
#include "driver/rtc_io.h"

#define TAG "connection"

typedef enum __attribute__((packed)){
    PKT_BRIGHTNESS_EVENT = 0x01,
    PKT_TEMP_EVENT     = 0x02,
    PKT_KNOB_BUTTON      = 0x03,
} pkt_type_t;

typedef struct __attribute__((packed)){
    pkt_type_t type;
    uint8_t    seq;
    union{
        int16_t knob_delta;
        uint8_t knob_button_state;
    };
} app_pkt_t;
static void send_packet(app_pkt_t * const pkt);

typedef enum{
    BRIGHT,
    TEMP
} knob_state_t;

typedef enum{
    KNOB_DELTA_SIG,
    BTN_SHORT_PRESS,
    BTN_LONG_PRESS,
    ARM_LONG_BTN_SIG,
    DISARM_LONG_BTN_SIG,
    ENTER_SLEEP_SIG,
    MAX_SIGNAL
} signal_t;

typedef struct{
    signal_t signal;
    int16_t knob_delta;
} knob_message_t;


static uint8_t volatile s_seq = 0;
static QueueHandle_t espnow_send_handle_q = NULL;
static QueueHandle_t controller_queue = NULL;

static TaskHandle_t controller_handle;

esp_timer_handle_t knob_timer_handle;
esp_timer_handle_t sleep_timer_handle;
esp_timer_handle_t long_press_handle;

/*CALLBACKS and ISR functions*/
static void knob_callback(void * args);
static void sleep_mode_callback(void * args);
static void knob_button_isr(void *arg);

// Setup functions
static void espnow_setup();
static void knob_button_setup(void);
static void post_long_press_event(void * args);
static void encoder_pins_setup(void);
static void timeout_setup(esp_timer_handle_t *timer_handle, esp_timer_cb_t call_back, const char * timer_name);


static void restart_time_event(esp_timer_handle_t *timer_handle, uint64_t period);
static inline void encoder_handle_tick(void);
static void controller_task(void *param);

void app_main(void){
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    controller_queue = xQueueCreate(20, sizeof(knob_message_t));
    configASSERT(controller_queue != NULL);

    encoder_pins_setup();
    knob_button_setup();

    xTaskCreate(controller_task, "controller", 8192, NULL, 2, &controller_handle);
}

static void controller_task(void *param){
    knob_state_t knob_state = BRIGHT;
    knob_message_t message = {0};

    espnow_setup();

    timeout_setup(&knob_timer_handle, knob_callback, "Knob timer");
    ESP_ERROR_CHECK(esp_timer_start_periodic(knob_timer_handle, KNOB_POLL_PERIOD_US));

    timeout_setup(&sleep_timer_handle, sleep_mode_callback, "sleep timer");
    ESP_ERROR_CHECK(esp_timer_start_once(sleep_timer_handle, SLEEP_PERIOD_US));

    timeout_setup(&long_press_handle, post_long_press_event, "long press");

    for (;;){
        xQueueReceive(controller_queue, &message, portMAX_DELAY);
        switch (message.signal){
            case KNOB_DELTA_SIG : {
                int knob_delta = message.knob_delta;
                pkt_type_t signal_type = (knob_state == BRIGHT)? PKT_BRIGHTNESS_EVENT : PKT_TEMP_EVENT;
                app_pkt_t pkt = {
                    .type = signal_type,
                    .knob_delta = knob_delta
                };

                send_packet(&pkt);
                ESP_LOGI("debug", "Packet sent from KNOB DELTA SIGNAL, with value, %d", knob_delta);
                restart_time_event(&sleep_timer_handle, SLEEP_PERIOD_US);
                break;
            }

            case BTN_SHORT_PRESS : {
                knob_state = (knob_state == BRIGHT)? TEMP : BRIGHT;
                restart_time_event(&sleep_timer_handle, SLEEP_PERIOD_US);
                break;
            }

            case BTN_LONG_PRESS : {
                app_pkt_t pkt = {
                    .type = PKT_KNOB_BUTTON,
                    .knob_button_state = 1
                };

                ESP_LOGI("debug", "Packet sent from BUTTON LONG PRESS");
                send_packet(&pkt);
                restart_time_event(&sleep_timer_handle, SLEEP_PERIOD_US);
                break;
            }

            case ARM_LONG_BTN_SIG: {
                ESP_ERROR_CHECK(esp_timer_start_once(long_press_handle, KNOB_BTN_LONG_PRESS_PERIOD));
                break;
            }

            case DISARM_LONG_BTN_SIG: {
                esp_err_t ret = esp_timer_stop(long_press_handle);
                if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) ESP_ERROR_CHECK(ret);
                restart_time_event(&sleep_timer_handle, SLEEP_PERIOD_US);
                break;
            }

            case ENTER_SLEEP_SIG: {
                int dt_level = gpio_get_level(KNOB_DT_PIN);
                int wake_level = (dt_level == 1) ? 0 : 1;
                ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup((gpio_num_t)KNOB_DT_PIN, wake_level));
                ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup((1ULL << KNOB_BTN_PIN), ESP_EXT1_WAKEUP_ALL_LOW));
                ESP_ERROR_CHECK(rtc_gpio_pullup_en((gpio_num_t)KNOB_BTN_PIN));
                ESP_ERROR_CHECK(rtc_gpio_pulldown_dis((gpio_num_t)KNOB_BTN_PIN));
                ESP_ERROR_CHECK(rtc_gpio_pullup_en((gpio_num_t)KNOB_DT_PIN));
                ESP_ERROR_CHECK(rtc_gpio_pulldown_dis((gpio_num_t)KNOB_DT_PIN));
                esp_deep_sleep_start();
                break;
            }

            default:
                break;

        }
    }
}

static void post_long_press_event(void * args){
    knob_message_t message = {.signal = BTN_LONG_PRESS};
    xQueueSend(controller_queue, &message, 0);
}

static void timeout_setup(esp_timer_handle_t *timer_handle, esp_timer_cb_t call_back, const char * timer_name){
    esp_timer_create_args_t timer_cfg = {
        .callback = call_back,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = timer_name,
        .skip_unhandled_events = true
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, timer_handle));
}

static void restart_time_event(esp_timer_handle_t *timer_handle, uint64_t period){
    esp_err_t stop_ret = esp_timer_restart(*timer_handle, period);
    if (stop_ret == ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(esp_timer_start_once(*timer_handle, period));
    else if (stop_ret != ESP_OK) ESP_ERROR_CHECK(stop_ret);
}

static void knob_callback(void * args){
    encoder_handle_tick();
}

static void sleep_mode_callback(void *args) {
    knob_message_t message = {.signal = ENTER_SLEEP_SIG};
    xQueueSend(controller_queue, &message, 0);
}

typedef enum {
    ENC_S0 = 0, // A=0 B=0
    ENC_S1 = 1, // A=0 B=1 
    ENC_S2 = 2, // A=1 B=0
    ENC_S3 = 3, // A=1 B=1 – detent position; counts are registered here.
} enc_state_t;

static const int8_t knob_lookup[4][4] = {
//  curr:  S0   S1   S2   S3
/* S0 */  { 0,   0,   0,  99 },
/* S1 */  { 0,   0,  99,  -1 },  // was -1
/* S2 */  { 0,  99,   0,  +1 },  // was +1
/* S3 */  {99,   0,   0,   0 },
};

static volatile enc_state_t previous_state;

static inline void encoder_handle_tick(void) {
    static int16_t knob_delta_ = 0;

    uint8_t a = gpio_get_level(KNOB_DT_PIN);
    uint8_t b = gpio_get_level(KNOB_CLK_PIN);

    enc_state_t next_state = (enc_state_t)((a << 1) | b);
    int8_t step = knob_lookup[previous_state][next_state];

    if (step != 99) {
        knob_delta_ += step;
    }
    previous_state = next_state;

    static uint64_t last_flush_us = 0;
    uint64_t now_us = esp_timer_get_time();

    if ((now_us - last_flush_us) >= KNOB_DELTA_FLUSH_US) {
        last_flush_us = now_us;
        if (knob_delta_ != 0) {
            knob_message_t message = {.signal = KNOB_DELTA_SIG, .knob_delta = knob_delta_};
            xQueueSend(controller_queue, &message, 0);
            knob_delta_ = 0;
        }
    }
}

static void knob_button_setup(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << KNOB_BTN_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&cfg);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)KNOB_BTN_PIN, knob_button_isr, NULL));
}

static void encoder_pins_setup(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << KNOB_CLK_PIN) | (1ULL << KNOB_DT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    gpio_config(&cfg);

    uint8_t a = gpio_get_level(KNOB_DT_PIN);
    uint8_t b = gpio_get_level(KNOB_CLK_PIN);

    previous_state = (enc_state_t)((a << 1) | b);
}

static void IRAM_ATTR knob_button_isr(void *arg) {
    static bool isLongBtnTimeoutArmed = false;
    BaseType_t taskWoken = pdFALSE;
    int64_t now = esp_timer_get_time();
    static int64_t last_press = 0;
    int level = gpio_get_level(KNOB_BTN_PIN);

    if (level == 0){
        if ((now - last_press) < KNOB_BTN_DEBOUNCE_US) return; 
        last_press = now;
        if (!isLongBtnTimeoutArmed){
            knob_message_t message = {.signal = ARM_LONG_BTN_SIG};
            xQueueSendFromISR(controller_queue, &message, &taskWoken);
            isLongBtnTimeoutArmed = true;
        }
    }
    else{
        if (isLongBtnTimeoutArmed) {
            knob_message_t msg = {.signal = DISARM_LONG_BTN_SIG};
            xQueueSendFromISR(controller_queue, &msg, &taskWoken);
            isLongBtnTimeoutArmed = false;
        }
        int64_t difference = (now - last_press);
        if ((difference >= KNOB_BTN_DEBOUNCE_US) && (difference < SHORT_BTN_MAX_PERIOD)) {
            knob_message_t message = {0};
            message = (knob_message_t) {.signal = BTN_SHORT_PRESS};
            xQueueSendFromISR(controller_queue, &message, &taskWoken);
            isLongBtnTimeoutArmed = false;
        }
    }

    if (taskWoken) portYIELD_FROM_ISR(taskWoken);
}

static void send_cb(const uint8_t *mac, esp_now_send_status_t status) {
    (void) mac;
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "send ACK ✓");
    } else {
        ESP_LOGW(TAG, "send FAIL — lightbar not ACKing (wrong channel or not running?)");
    }
}

static void sender_task(void *pv){
    app_pkt_t pkt;
    for (;;) {
        xQueueReceive(espnow_send_handle_q, &pkt, portMAX_DELAY);
        esp_err_t err = esp_now_send(LIGHTBAR_MAC, (const uint8_t *)&pkt, sizeof(pkt));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "seq=%u send failed: %s", pkt.seq, esp_err_to_name(err));
        }
    }
}

static void espnow_setup(){
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));   /* disable power-save — ESP-NOW needs radio always-on */

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));

    esp_now_peer_info_t peer = {
        .channel = ESPNOW_CHANNEL,
        .encrypt = false,
        .ifidx   = ESP_IF_WIFI_STA
    };
    memcpy(peer.peer_addr, LIGHTBAR_MAC, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    espnow_send_handle_q = xQueueCreate(8, sizeof(app_pkt_t));
    configASSERT(espnow_send_handle_q);
    configASSERT(xTaskCreate(sender_task, "espnow_sender", 8192, NULL, 1, NULL));
}

static void send_packet(app_pkt_t * const pkt){
    pkt->seq = s_seq++;
    if (xQueueSend(espnow_send_handle_q, pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "seq=%u send queue full, dropped", pkt->seq);
    }
}


