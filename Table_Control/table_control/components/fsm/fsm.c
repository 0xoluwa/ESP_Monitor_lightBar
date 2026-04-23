/**
 * @file fsm.c
 * @brief FSM framework – lifecycle functions and event dispatch loop.
 *
 * Implements the core run-to-completion event processing loop.  After
 * dequeuing an event, the loop calls the current state handler; if the
 * handler returns ::STATE_TRANSITION, the loop performs the exit/entry/init
 * sequence and repeats until the state stabilises.
 */

#include "fsm.h"

/**
 * @brief Pre-built events for the three framework lifecycle signals.
 *
 * Indexed by ::SIG_ENTRY (0), ::SIG_EXIT (1), ::SIG_INIT (2).
 * Allocated up to (but not including) ::SIG_USER_CODE.
 */
fsm_event RESERVED_EVENT[SIG_USER_CODE] = {
    {.signal = SIG_ENTRY},
    {.signal = SIG_EXIT},
    {.signal = SIG_INIT}
};


/**
 * @brief Construct an FSM – allocate the event queue.
 *
 * @param me          FSM control block to initialise.
 * @param queue_depth Capacity of the event queue (number of events).
 * @param event_size  Size in bytes of one event item.
 */
void fsm_ctor(fsm *me, uint8_t queue_depth, uint16_t event_size){
    me->queue_ = FSM_CREATE_QUEUE(queue_depth, event_size);
    FSM_ASSERT(me->queue_);
    me->event_structure_size_ = event_size;
}

/**
 * @brief Start an FSM – set initial state, spawn task, post SIG_INIT.
 *
 * @param me             FSM control block (constructed with ::fsm_ctor).
 * @param task_name      FreeRTOS task name used in trace and debug output.
 * @param entry_function The first state handler to invoke on SIG_INIT.
 */
void fsm_init(fsm *me, const char * task_name, state_handler entry_function){
    FSM_ASSERT(entry_function);
    me->state = entry_function;

    FSM_ASSERT(FSM_TASK_CREATE(&fsm_dispatch, task_name, 8192, me, &me->task_));

    uint8_t event[me->event_structure_size_]; //adjusted for event size
    ((fsm_event *) event)->signal = SIG_INIT;

    fsm_post(me, (fsm_event *) event);
}

/**
 * @brief Enqueue an event from task context (non-blocking).
 *
 * @param me    Target FSM.
 * @param event Event to copy into the queue.
 * @return true if enqueued; false if the queue was full.
 */
bool fsm_post(fsm *me, fsm_event const * event){
    return FSM_QUEUE_SEND(me->queue_, event);
}

/**
 * @brief Enqueue an event from an ISR context (non-blocking, ISR-safe).
 *
 * Issues portYIELD_FROM_ISR if a higher-priority task was unblocked.
 *
 * @param me    Target FSM.
 * @param event Event to copy into the queue.
 */
void IRAM_ATTR fsm_post_from_isr(fsm *me, fsm_event const * event){
    FSM_QUEUE_SEND_FROM_ISR(me->queue_, event);
}

/**
 * @brief Run-to-completion dispatch loop (FreeRTOS task body).
 *
 * Blocks on the event queue.  For each event:
 *  1. Calls the current state handler.
 *  2. If ::STATE_TRANSITION is returned, drives the exit/entry/init sequence
 *     and repeats until the machine stabilises in a non-transitioning state.
 *
 * Never returns – runs for the lifetime of the task.
 *
 * @param pv Pointer to the owning ::fsm instance.
 */
void fsm_dispatch(void *pv){
    fsm *me = (fsm *) pv;
    uint8_t *event_buffer = malloc(me->event_structure_size_);
    configASSERT(event_buffer);
    state_handler  current_state_;
    state_handler  previous_state_;

    for (;;) {
        FSM_QUEUE_RECIEVE(me->queue_, event_buffer);
        current_state_ = me->state;

        fsm_state result = (*(me->state))(me, (fsm_event *) event_buffer);

        while (result == STATE_TRANSITION) {
            previous_state_ = current_state_;
            current_state_ = me->state;

            STATE_EXIT(previous_state_);
            STATE_ENTRY(current_state_);

            /* deliver SIG_INIT to the newly active state */
            if (current_state_ != me->state) FSM_ASSERT(false);
            else{
                result = (*current_state_) ((fsm *) me, &RESERVED_EVENT[SIG_INIT]);
            }
        }
    }
}
