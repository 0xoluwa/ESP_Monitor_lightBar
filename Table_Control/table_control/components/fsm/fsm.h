#ifndef __FSM_H__
#define __FSM_H__

#include "fsm_events.h"
#include "fsm_port.h"




typedef enum{
    STATE_IGNORED,
    STATE_HANDLED = 1,
    STATE_TRANSITION,
    SUPER_TRANSITION,
    CHILD_TRANSITION,
    MAX_STATE
} fsm_state;


typedef struct FSM fsm;

typedef fsm_state (*state_handler)(fsm *me, fsm_event *event);

struct FSM{
    state_handler state;
    FSM_QUEUE_HANDLE queue_;
    FSM_TASK_HANDLE  task_;
    uint16_t event_structure_size_;
};

extern fsm_event RESERVED_EVENT[];


#define TRAN(STATE) (((fsm*) me)->state = ((state_handler) STATE), STATE_TRANSITION)

#define SUPER(STATE, EVENT) ((STATE) (me, EVENT))

#define STATE_EXIT(STATE)   if (STATE == ((fsm *) me)->state) FSM_ASSERT(false);               \
                            else{                                                               \
                                (STATE) ((fsm *) me, &RESERVED_EVENT[SIG_EXIT]);                 \
                            }

#define STATE_ENTRY(STATE)  if (STATE != ((fsm *) me)->state) FSM_ASSERT(false);              \
                            else{                                                              \
                                (STATE) ((fsm *) me, &RESERVED_EVENT[SIG_ENTRY]);               \
                            }
                            
#define STATE_INIT(STATE)   if (STATE != ((fsm *) me)->state) FSM_ASSERT(false);               \
                            else{                                                               \
                                (STATE) ((fsm *) me, &RESERVED_EVENT[SIG_INIT]);                \
                            }


void fsm_ctor(fsm *me, uint8_t queue_depth, uint16_t event_size);
void fsm_init(fsm *me, const char * task_name, state_handler entry_function);
bool fsm_post(fsm *me, fsm_event const *event);
void IRAM_ATTR fsm_post_from_isr(fsm *me, fsm_event const * event);
void fsm_dispatch(void *pv);


#endif