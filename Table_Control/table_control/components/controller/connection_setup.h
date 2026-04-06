#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include <stdio.h>
#include "esp_log.h"

#define ESPNOW_CHANNEL 1

#define TAG "connection"


static const uint8_t LIGHTBAR_MAC[ESP_NOW_ETH_ALEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};  //update mac address

typedef enum __attribute__((packed)){
    PKT_BRIGHTNESS_EVENT = 0x01,
    PKT_PRESET_EVENT = 0x02,
    PKT_KNOB_BUTTON = 0x03,
    PKT_HEARTBEAT = 0x04
} pkt_type_t;

typedef struct __attribute__((packed)){
    pkt_type_t type;
    uint8_t seq;
    union{
        int16_t knob_delta;
        uint8_t knob_button_state;
    };
} app_pkt_t;

static QueueHandle_t s_send_status_queue;
uint8_t s_seq = 0;

static void send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status){
    xQueueSendFromISR(s_send_status_queue, &status, NULL);
}

void espnow_init(void){
    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));

    esp_now_peer_info_t peer = {
        .channel = ESPNOW_CHANNEL,
        .encrypt = false,
        .ifidx = ESP_IF_WIFI_STA
    };

    memcpy(peer.peer_addr, LIGHTBAR_MAC, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
}

void send_packet(const app_pkt_t *pkt){
    esp_err_t err = esp_now_send(LIGHTBAR_MAC, (const uint8_t *)pkt, sizeof(*pkt));
    ESP_ERROR_CHECK(err);

    esp_now_send_status_t status;
    if(xQueueReceive(s_send_status_queue, &status, pdMS_TO_TICKS(100)) ==pdTRUE){

    }else{
        ESP_LOGW(TAG, "seq=%u send callback timeout", pkt->seq);
    }
}



