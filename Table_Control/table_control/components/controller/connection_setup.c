#include "connection_setup.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include "controller.h"

#define TAG "connection"

static const uint8_t LIGHTBAR_MAC[ESP_NOW_ETH_ALEN] = {0xB4, 0xBF, 0xE9, 0x15, 0xA6, 0xD4};

extern controller device;

uint8_t s_seq = 0;

static QueueHandle_t s_send_queue;

static void send_cb(const uint8_t *mac, esp_now_send_status_t status) {
    (void) mac;
    if (status == ESP_NOW_SEND_SUCCESS) {
        fsm_event evt = {
            .signal = CONNECTED_SIG 
        };

        fsm_post((fsm *) &device, &evt);

        ESP_LOGI(TAG, "send ACK ✓");
    } else {
        fsm_event evt = {
            .signal = DISCONNECTED_SIG 
        };

        fsm_post((fsm *) &device, &evt);
        ESP_LOGW(TAG, "send FAIL — lightbar not ACKing (wrong channel or not running?)");
    }
}

static void sender_task(void *pv){
    app_pkt_t pkt;
    for (;;) {
        xQueueReceive(s_send_queue, &pkt, portMAX_DELAY);
        esp_err_t err = esp_now_send(LIGHTBAR_MAC, (const uint8_t *)&pkt, sizeof(pkt));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "seq=%u send failed: %s", pkt.seq, esp_err_to_name(err));
        }
    }
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
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));   /* disable power-save — ESP-NOW needs radio always-on */

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));

    esp_now_peer_info_t peer = {
        .channel = 0,       // 0 = use current WiFi channel (channel 1 set above)
        .encrypt = false,
        .ifidx   = ESP_IF_WIFI_STA
    };
    memcpy(peer.peer_addr, LIGHTBAR_MAC, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    s_send_queue = xQueueCreate(8, sizeof(app_pkt_t));
    configASSERT(s_send_queue);
    configASSERT(xTaskCreate(sender_task, "espnow_sender", 4096, NULL, 1, NULL));
}

void send_packet(const app_pkt_t *pkt){
    if (xQueueSend(s_send_queue, pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "seq=%u send queue full, dropped", pkt->seq);
    }
}
