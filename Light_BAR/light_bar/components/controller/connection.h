/**
 * @file connection.h
 * @brief ESP-NOW wireless receive interface for the light-bar controller.
 *
 * Defines the over-the-air packet format and the initialisation function used
 * by the table-control remote to drive brightness, CCT, and power state.
 */
#ifndef __CONNECTION_SETUP_H__
#define __CONNECTION_SETUP_H__

#include <stdint.h>
#include "controller.h"

/** @brief Wi-Fi channel used for ESP-NOW communication. */
#define ESPNOW_CHANNEL 1

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

/**
 * @brief Initialise Wi-Fi in station mode and register the ESP-NOW receive callback.
 *
 * NVS flash must already be initialised (handled by the controller) before
 * calling this function.
 *
 * @param device Pointer to the controller instance that will receive posted events.
 */
void espnow_init(lightbar_controller *device);

#endif /* __CONNECTION_SETUP_H__ */
