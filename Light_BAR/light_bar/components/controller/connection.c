/**
 * @file connection.c
 * @brief ESP-NOW receive callback and Wi-Fi initialisation.
 *
 * Receives fixed-size ::app_pkt_t frames from the table-control remote and
 * forwards them as FSM events to the light-bar controller.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>
#include "connection.h"

#define TAG "connection"

/** @brief Controller instance set during espnow_init(); used by the receive callback. */
static lightbar_controller *user_device = NULL;

static void recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);

void espnow_init(lightbar_controller *device)
{
    user_device = device;

    /* NVS flash init occurred in the controller before this call. */
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

/**
 * @brief ESP-NOW receive callback.
 *
 * Validates the packet length and forwards the payload to the controller as
 * the appropriate FSM event.  Packets with an unexpected length or unknown
 * type are silently discarded.
 *
 * @param esp_now_info Metadata about the received frame (sender MAC, RSSI, etc.).
 * @param data         Raw payload bytes.
 * @param data_len     Length of @p data in bytes.
 */
static void recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    if (data_len != sizeof(app_pkt_t)) return;

    const app_pkt_t *msg = (const app_pkt_t *)data;
    switch (msg->type) {
        case PKT_BRIGHTNESS_EVENT:
            post_brightness_delta(user_device, msg->knob_delta * BRIGHTNESS_MULTIPLIER_PATCH);
            break;

        case PKT_COLOR_TEMP_EVENT:
            post_color_temp_delta(user_device, msg->knob_delta);
            break;

        case PKT_KNOB_BUTTON:
            post_power_button(user_device);
            break;

        default:
            return;
    }
}
