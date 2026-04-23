/**
 * @file connection_setup.c
 * @brief ESP-NOW link initialisation and packet sender task.
 *
 * Architecture:
 *  - ::espnow_init brings up the full WiFi + ESP-NOW stack and creates a
 *    dedicated FreeRTOS sender task so the controller FSM task never blocks
 *    on radio operations.
 *  - ::send_packet posts packets to an 8-element queue consumed by the
 *    sender task.
 *  - ::send_cb (the ESP-NOW send callback) posts ::CONNECTED_SIG or
 *    ::DISCONNECTED_SIG back to the controller FSM to drive the RGB LED.
 */

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

#define TAG "connection"

/** @brief Hardcoded MAC address of the light bar ESP-NOW peer. */
static const uint8_t LIGHTBAR_MAC[ESP_NOW_ETH_ALEN] = {0xB4, 0xBF, 0xE9, 0x15, 0xA6, 0xD4};

/** @brief Controller instance to post send-status events back to. */
static controller *user_device = NULL;

/** @brief Rolling sequence counter stamped into every outgoing packet. */
uint8_t s_seq = 0;

/** @brief FreeRTOS queue buffering packets for the sender task. */
static QueueHandle_t s_send_queue;


/* ── ESP-NOW callbacks ────────────────────────────────────────────────────── */

/**
 * @brief ESP-NOW send callback – posts connection status to the controller FSM.
 *
 * Called by the ESP-NOW stack after the radio completes a send attempt.
 * Posts ::CONNECTED_SIG if the peer acknowledged the frame, or
 * ::DISCONNECTED_SIG if no ACK was received (peer not running or wrong
 * channel).
 *
 * @param mac    MAC address of the destination peer (unused).
 * @param status ESP-NOW send result code.
 */
static void send_cb(const uint8_t *mac, esp_now_send_status_t status) {
    (void) mac;
    if (status == ESP_NOW_SEND_SUCCESS) {
        controller_event evt = {
            .super.signal = CONNECTED_SIG
        };
        fsm_post((fsm *) user_device, (fsm_event *) &evt);
        ESP_LOGI(TAG, "send ACK ✓");
    } else {
        controller_event evt = {
            .super.signal = DISCONNECTED_SIG
        };
        fsm_post((fsm *) user_device, (fsm_event *) &evt);
        ESP_LOGW(TAG, "send FAIL — lightbar not ACKing (wrong channel or not running?)");
    }
}


/* ── Sender task ──────────────────────────────────────────────────────────── */

/**
 * @brief FreeRTOS task that serialises esp_now_send() calls.
 *
 * Blocks waiting for packets on @c s_send_queue.  Running esp_now_send()
 * in a dedicated task ensures the controller FSM task is never delayed by
 * radio operations, and prevents concurrent send calls which are not
 * supported by ESP-NOW.
 *
 * @param pv Unused task parameter.
 */
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


/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Bring up the ESP-NOW link and create the sender infrastructure.
 *
 * Initialisation sequence:
 *  1. NVS flash (required by the WiFi driver).
 *  2. Default event loop and netif.
 *  3. WiFi in STA mode on ::ESPNOW_CHANNEL with power-save disabled.
 *  4. ESP-NOW with ::send_cb registered.
 *  5. Light bar peer added (no encryption, STA interface).
 *  6. Send queue (8 elements) and sender task created.
 *
 * @param me Controller that will receive ::CONNECTED_SIG / ::DISCONNECTED_SIG.
 */
void espnow_init(controller *me){
    user_device = me;
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
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));   /* disable power-save – ESP-NOW needs the radio always on */

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));

    esp_now_peer_info_t peer = {
        .channel = 0,       /* 0 = inherit the current WiFi channel (channel 1 set above) */
        .encrypt = false,
        .ifidx   = ESP_IF_WIFI_STA
    };
    memcpy(peer.peer_addr, LIGHTBAR_MAC, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    s_send_queue = xQueueCreate(8, sizeof(app_pkt_t));
    configASSERT(s_send_queue);
    configASSERT(xTaskCreate(sender_task, "espnow_sender", 4096, NULL, 1, NULL));
}

/**
 * @brief Enqueue a packet for the sender task (non-blocking).
 *
 * If the queue is full the packet is dropped and a warning is logged.
 * The caller must not modify @p pkt after this call returns, as the queue
 * holds a copy.
 *
 * @param pkt Packet to enqueue; copied by value into the queue.
 */
void send_packet(const app_pkt_t *pkt){
    if (xQueueSend(s_send_queue, pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "seq=%u send queue full, dropped", pkt->seq);
    }
}
