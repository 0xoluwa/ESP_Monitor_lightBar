#ifndef __KNOB_H__
#define __KNOB_H__

#include "stdint.h"
#include "driver/gpio.h"





typedef enum {
    ENC_S0 = 0, // A=0 B=0
    ENC_S1 = 1, // A=0 B=1
    ENC_S2 = 2, // A=1 B=0
    ENC_S3 = 3, // A=1 B=1
} enc_state_t;

// [prev_state][curr_state] → delta
static const int8_t FSM[4][4] = {
//  curr:  S0   S1   S2   S3
/* S0 */  { 0,  -1,  +1,  99 },
/* S1 */  {+1,   0,  99,  -1 },
/* S2 */  {-1,  99,   0,  +1 },
/* S3 */  {99,  +1,  -1,   0 },
};

typedef struct {
    enc_state_t state;
    int32_t     delta;       // accumulated since last read
    uint32_t    error_count; // illegal transitions caught
    gpio_num_t pinA;
    gpio_num_t pinB;
} encoder_handle_t;


static encoder_handle_t me_knob;

void encoder_fsm_ctor(encoder_handle_t *me, gpio_num_t clk_pin, gpio_num_t dat_pin){
    me->pinA = clk_pin;
    me->pinB = dat_pin;
}

void encoder_fsm_init(encoder_handle_t *me) {
    uint8_t a = gpio_get_level(me->pinA);
    uint8_t b = gpio_get_level(me->pinB);
    me->state       = (enc_state_t)((a << 1) | b);
    me->delta       = 0;
    me->error_count = 0;
}

void encoder_handle_tick(encoder_handle_t *me) {
    uint8_t a = gpio_get_level(me->pinA);
    uint8_t b = gpio_get_level(me->pinB);
    enc_state_t next = (enc_state_t)((a << 1) | b);

    int8_t step = FSM[me->state][next];

    if (step == 99) {
        me->error_count++;
        return;
    }

    me->delta += step;
    me->state  = next;
}

int32_t encoder_read_and_clear(encoder_handle_t *me) {
    int32_t d = me->delta;
    me->delta = 0;
    return d;
}

#endif