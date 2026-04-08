#ifndef TIMER_CHANNEL_H
#define TIMER_CHANNEL_H

#include "stdint.h"
#include "freertos/timers.h"
#include "fsm_events.h"

typedef enum __attribute__((packed)) {
    TIMER_CHANNEL_0,
    TIMER_CHANNEL_1,
    TIMER_CHANNEL_2,
    TIMER_CHANNEL_3,
    TIMER_CHANNEL_4,
    TIMER_CHANNEL_5,
    TIMER_CHANNEL_6,
    TIMER_CHANNEL_7,
    TIMER_CHANNEL_8,
    TIMER_CHANNEL_9,
    TIMER_CHANNEL_10,
    TIMER_CHANNEL_MAX
} timer_channel_t;


typedef struct{
    TimerHandle_t   timer_handle;
    timer_channel_t channel;
    uint32_t        frequency;
    uint32_t        tick;
    uint8_t         event_data[10]; // full event payload, size set by fsm_ctor
    void           *owner; // back-pointer to the owning fsm (set by fsm_timer_arm)
} timer_t;

void timer_ctor(timer_t *me, timer_channel_t channel, uint32_t frequency);

#endif // TIMER_CHANNEL_H
