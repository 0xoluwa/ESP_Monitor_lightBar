/**
 * @file connection_setup.h
 * @brief ESP-NOW wireless link to the light bar – public interface.
 *
 * Defines the packed over-the-air packet format (::app_pkt_t) shared between
 * the table controller and the light bar, and exposes the two functions
 * needed to bring the link up and queue outgoing packets.
 *
 * Packet types:
 *  - ::PKT_BRIGHTNESS_EVENT – knob delta for brightness adjustment.
 *  - ::PKT_PRESET_EVENT     – knob delta for colour-temperature adjustment.
 *  - ::PKT_KNOB_BUTTON      – button state for power toggle.
 */

#ifndef __CONNECTION_SETUP_H__
#define __CONNECTION_SETUP_H__

#include <stdint.h>
#include "controller.h"

/** @brief ESP-NOW WiFi channel used for all unicast packets to the light bar. */
#define ESPNOW_CHANNEL 1

/**
 * @brief Packet type identifiers sent over ESP-NOW.
 *
 * Packed to a single byte to minimise over-the-air payload size.
 */
typedef enum __attribute__((packed)){
    PKT_BRIGHTNESS_EVENT = 0x01, /**< Knob delta → light bar brightness.       */
    PKT_PRESET_EVENT     = 0x02, /**< Knob delta → light bar colour temperature.*/
    PKT_KNOB_BUTTON      = 0x03, /**< Button duration → light bar power toggle. */
} pkt_type_t;

/**
 * @brief Over-the-air application packet (packed, no padding).
 *
 * The union payload is interpreted according to @c type:
 *  - ::PKT_BRIGHTNESS_EVENT or ::PKT_PRESET_EVENT → @c knob_delta is valid.
 *  - ::PKT_KNOB_BUTTON                            → @c knob_button_state is valid.
 */
typedef struct __attribute__((packed)){
    pkt_type_t type;             /**< Packet type; selects the active union member.          */
    uint8_t    seq;              /**< Rolling sequence counter for duplicate detection.       */
    union{
        int16_t knob_delta;      /**< Signed encoder delta (::PKT_BRIGHTNESS_EVENT / PRESET).*/
        uint8_t knob_button_state; /**< ::button_duration value (::PKT_KNOB_BUTTON).         */
    };
} app_pkt_t;

/**
 * @brief Rolling sequence number, incremented by the controller on each send.
 *
 * Starts at 0 at boot and wraps at 255.  Exposed so controller.c can stamp
 * packets without needing a getter function.
 */
extern uint8_t s_seq;

/**
 * @brief Initialise the ESP-NOW link.
 *
 * Brings up NVS, netif, WiFi (STA mode, channel ::ESPNOW_CHANNEL), and
 * ESP-NOW, registers the send callback, adds the light bar as a peer, and
 * creates the sender task and queue.
 *
 * @param me Controller instance used as the target for send-status events
 *           (::CONNECTED_SIG / ::DISCONNECTED_SIG).
 */
void espnow_init(controller *me);

/**
 * @brief Enqueue a packet for transmission to the light bar.
 *
 * Non-blocking: if the send queue is full the packet is silently dropped
 * and a warning is logged.  The actual esp_now_send() call happens in the
 * dedicated sender task.
 *
 * @param pkt Pointer to the packet to copy into the send queue.
 */
void send_packet(const app_pkt_t *pkt);

#endif
