#ifndef __TIMER_EVT__
#define __TIMER_EVT__

#include "fsm.h"
#include <stdbool.h>

typedef struct fsm_time_event_ fsm_time_event;

struct fsm_time_event_ {
    fsm_event       super;          /* IS-A fsm_event — must be first        */
    fsm_time_event *next;           /* intrusive linked list of armed timers  */
    void           *state_machine;  /* owning FSM to post to on expiry        */
    uint64_t        down_counter;   /* ticks remaining                        */
    uint64_t        interval;       /* 0 = one-shot, >0 = periodic reload     */
};

extern fsm_time_event *time_event_list_head;

void fsm_time_event_ctor (fsm_time_event *me, fsm *owner, uint8_t signal);
void fsm_time_event_arm  (fsm_time_event *me, uint64_t nTicks, uint64_t interval);
bool fsm_time_event_rearm(fsm_time_event *me, uint64_t nTicks);
void fsm_time_event_disarm(fsm_time_event *me);
void fsm_tick(void);
void fsm_tick_init(uint64_t period_us);
void fsm_tick_deinit(void);

#endif
