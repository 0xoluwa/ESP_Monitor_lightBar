#ifndef __FSM_H__
#define __FSM_H__

#include "fsm_events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "timer_channel.h"

typedef enum : uint8_t{
    STATE_IGNORED,
    STATE_HANDLED = 1,
    STATE_TRANSITION
} fsm_state;


typedef struct FSM fsm;

typedef fsm_state (*state_handler)(fsm *me, fsm_event *event);

struct FSM{
    state_handler state;
    QueueHandle_t queue_;
    TaskHandle_t  task_;
    timer_t      *Timers;       // array of TIMER_CHANNEL_MAX timer slots
    size_t        event_size_;   // byte width of each queue item (set by fsm_ctor)
};


#define TRAN(STATE) (me->state = (STATE), STATE_TRANSITION)


void fsm_ctor(fsm *me, uint8_t queue_depth, uint32_t event_size);
void fsm_init(fsm *me, const char * task_name, state_handler entry_function);
BaseType_t fsm_post(fsm *me, fsm_event const *event);
BaseType_t fsm_post_from_isr(fsm *me, fsm_event const *event);
void fsm_dispatch(void *pv);


void fsm_timer_arm(fsm * me, fsm_event const * event, timer_channel_t channel, uint64_t frequency, uint64_t tick);
void fsm_timer_disarm(fsm *me, timer_channel_t);
void fsm_timer_update_frequency(fsm *me, timer_channel_t channel, uint64_t frequency);
void fsm_timer_update_tick(fsm *me, timer_channel_t channel, uint64_t tick);
void fsm_timer_callback(TimerHandle_t xTimer);




#endif
