/**
 * @file fsm.c
 * @brief Flat finite state machine framework implementation.
 */
#include "fsm.h"

/**
 * @brief Pre-allocated events for the three framework-reserved signals.
 *
 * Avoids heap allocation for the most frequently dispatched events.
 * Indexed by ::fsm_signal: [0] SIG_ENTRY, [1] SIG_EXIT, [2] SIG_INIT.
 */
fsm_event RESERVED_EVENT[SIG_USER_CODE] = {
    { .signal = SIG_ENTRY },
    { .signal = SIG_EXIT  },
    { .signal = SIG_INIT  },
};

void fsm_ctor(fsm *me, uint8_t queue_depth, uint16_t event_size)
{
    me->queue_ = FSM_CREATE_QUEUE(queue_depth, event_size);
    FSM_ASSERT(me->queue_);
    me->event_structure_size_ = event_size;
}

void fsm_init(fsm *me, const char *task_name, state_handler entry_function)
{
    FSM_ASSERT(entry_function);
    me->state = entry_function;

    FSM_ASSERT(FSM_TASK_CREATE(&fsm_dispatch, task_name, 8192, me, &me->task_));

    /* Stack-allocate a zero-initialised event of the concrete size and post SIG_INIT. */
    uint8_t event[me->event_structure_size_];
    ((fsm_event *)event)->signal = SIG_INIT;
    fsm_post(me, (fsm_event *)event);
}

bool fsm_post(fsm *me, fsm_event const *event)
{
    return FSM_QUEUE_SEND(me->queue_, event);
}

void fsm_post_from_isr(fsm *me, fsm_event const *event)
{
    FSM_QUEUE_SEND_FROM_ISR(me->queue_, event);
}

/**
 * @brief FSM dispatch loop.
 *
 * Blocks on the event queue indefinitely.  For each received event the active
 * state handler is called.  While the handler returns ::STATE_TRANSITION the
 * loop:
 *  1. Dispatches SIG_EXIT to the previous state.
 *  2. Dispatches SIG_ENTRY to the new state.
 *  3. Dispatches SIG_INIT to the new state; a further transition restarts from 1.
 */
void fsm_dispatch(void *pv)
{
    fsm *me = (fsm *)pv;
    uint8_t *event_buffer = malloc(me->event_structure_size_);
    configASSERT(event_buffer);

    state_handler current_state_;
    state_handler previous_state_;

    for (;;) {
        FSM_QUEUE_RECIEVE(me->queue_, event_buffer);
        current_state_ = me->state;

        fsm_state result = (*me->state)(me, (fsm_event *)event_buffer);

        while (result == STATE_TRANSITION) {
            previous_state_ = current_state_;
            current_state_  = me->state;

            STATE_EXIT(previous_state_);
            STATE_ENTRY(current_state_);

            if (current_state_ != me->state) FSM_ASSERT(false);
            else result = (*current_state_)(me, &RESERVED_EVENT[SIG_INIT]);
        }
    }
}
