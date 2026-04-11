#ifndef __CONNECTION_SETUP_H__
#define __CONNECTION_SETUP_H__

#include <stdint.h>

#define ESPNOW_CHANNEL 1

typedef enum __attribute__((packed)){
    PKT_BRIGHTNESS_EVENT = 0x01,
    PKT_PRESET_EVENT     = 0x02,
    PKT_KNOB_BUTTON      = 0x03,
    PKT_HEARTBEAT        = 0x04
} pkt_type_t;

typedef struct __attribute__((packed)){
    pkt_type_t type;
    uint8_t    seq;
    union{
        int16_t knob_delta;
        uint8_t knob_button_state;
    };
} app_pkt_t;

extern uint8_t s_seq;

void espnow_init(void);
void send_packet(const app_pkt_t *pkt);

#endif
