/**
 * @file fsm.h
 * @brief Lightweight flat finite state machine (FSM) framework.
 *
 * Each FSM instance runs in its own FreeRTOS task.  State handler functions
 * receive events from a queue and return one of the ::fsm_state values to
 * indicate whether the event was consumed, ignored, or triggered a transition.
 *
 * ## Usage pattern
 * 1. Embed ::fsm as the **first** member of your concrete state-machine struct.
 * 2. Call fsm_ctor() then fsm_init() at startup.
 * 3. Post events with fsm_post() (task context) or fsm_post_from_isr() (ISR).
 */
#ifndef __FSM_H__
#define __FSM_H__

#include "fsm_events.h"
#include "fsm_port.h"

/**
 * @brief Return codes from state handler functions.
 */
typedef enum {
    STATE_IGNORED    = 0, /**< Event was not handled; caller may propagate to superstate. */
    STATE_HANDLED    = 1, /**< Event was handled; no transition required. */
    STATE_TRANSITION,     /**< A TRAN() was executed; dispatch loop will run EXIT/ENTRY. */
    SUPER_TRANSITION,     /**< Reserved for hierarchical state machine extensions. */
    CHILD_TRANSITION,     /**< Reserved for hierarchical state machine extensions. */
    MAX_STATE             /**< Sentinel — total number of return-code values. */
} fsm_state;

/** @brief Forward declaration of the FSM base structure. */
typedef struct FSM fsm;

/**
 * @brief Prototype for all state handler functions.
 *
 * @param me    Pointer to the FSM base; cast to the concrete type inside the handler.
 * @param event Pointer to the event being dispatched.
 * @return      One of the ::fsm_state values.
 */
typedef fsm_state (*state_handler)(fsm *me, fsm_event *event);

/**
 * @brief Core FSM base object.
 *
 * Must be the **first** member of every concrete state-machine struct so the
 * concrete pointer can be cast to `fsm *` without violating strict aliasing.
 */
struct FSM {
    state_handler    state;                 /**< Currently active state handler function. */
    FSM_QUEUE_HANDLE queue_;                /**< FreeRTOS queue holding pending events. */
    FSM_TASK_HANDLE  task_;                 /**< FreeRTOS task running the dispatch loop. */
    uint16_t         event_structure_size_; /**< Size of the concrete event struct in bytes. */
};

/**
 * @brief Pre-allocated reserved events for ENTRY, EXIT, and INIT signals.
 *
 * Indexed by ::fsm_signal: [0] = SIG_ENTRY, [1] = SIG_EXIT, [2] = SIG_INIT.
 */
extern fsm_event RESERVED_EVENT[];

/**
 * @brief Trigger a state transition to @p STATE.
 *
 * Stores the new state handler and returns ::STATE_TRANSITION so the dispatch
 * loop performs the EXIT / ENTRY / INIT sequence.
 */
#define TRAN(STATE) (((fsm *)me)->state = ((state_handler)(STATE)), STATE_TRANSITION)

/**
 * @brief Delegate event handling to a superstate (hierarchical extension point).
 */
#define SUPER(STATE, EVENT) ((STATE)(me, EVENT))

/**
 * @brief Dispatch SIG_EXIT to @p STATE, asserting it is no longer the active state.
 * @note Used by the dispatch loop internally; not for application code.
 */
#define STATE_EXIT(STATE)  \
    if ((STATE) == ((fsm *)me)->state) FSM_ASSERT(false); \
    else { (STATE)((fsm *)me, &RESERVED_EVENT[SIG_EXIT]); }

/**
 * @brief Dispatch SIG_ENTRY to @p STATE, asserting it is the active state.
 * @note Used by the dispatch loop internally; not for application code.
 */
#define STATE_ENTRY(STATE) \
    if ((STATE) != ((fsm *)me)->state) FSM_ASSERT(false); \
    else { (STATE)((fsm *)me, &RESERVED_EVENT[SIG_ENTRY]); }

/**
 * @brief Dispatch SIG_INIT to @p STATE, asserting it is the active state.
 * @note Used by the dispatch loop internally; not for application code.
 */
#define STATE_INIT(STATE)  \
    if ((STATE) != ((fsm *)me)->state) FSM_ASSERT(false); \
    else { (STATE)((fsm *)me, &RESERVED_EVENT[SIG_INIT]); }

/**
 * @brief Construct the FSM base object and allocate the event queue.
 *
 * Must be called before fsm_init().
 *
 * @param me          Pointer to the ::fsm instance (first member of the concrete object).
 * @param queue_depth Maximum number of events the queue can hold simultaneously.
 * @param event_size  Size in bytes of the concrete event structure.
 */
void fsm_ctor(fsm *me, uint8_t queue_depth, uint16_t event_size);

/**
 * @brief Initialise the FSM and start its dispatch task.
 *
 * Creates the FreeRTOS task and posts the initial SIG_INIT event.
 *
 * @param me             Pointer to the ::fsm instance.
 * @param task_name      FreeRTOS task name string.
 * @param entry_function Initial (pseudo-)state handler; typically transitions to the real start state.
 */
void fsm_init(fsm *me, const char *task_name, state_handler entry_function);

/**
 * @brief Enqueue an event from task context (non-blocking).
 *
 * @param me    Pointer to the ::fsm instance.
 * @param event Pointer to the event to post; the event is copied into the queue.
 * @return      `true` if the event was enqueued, `false` if the queue was full.
 */
bool fsm_post(fsm *me, fsm_event const *event);

/**
 * @brief Enqueue an event from ISR context with automatic task yield.
 *
 * Triggers a context switch if a higher-priority task is unblocked.
 *
 * @param me    Pointer to the ::fsm instance.
 * @param event Pointer to the event to post; the event is copied into the queue.
 */
void fsm_post_from_isr(fsm *me, fsm_event const *event);

/**
 * @brief FSM dispatch loop — intended to run as a FreeRTOS task.
 *
 * Blocks on the event queue and dispatches each event to the active state
 * handler.  On ::STATE_TRANSITION the loop executes SIG_EXIT on the old
 * state, SIG_ENTRY on the new state, then SIG_INIT; if INIT causes another
 * transition the cycle repeats.
 *
 * @param pv Pointer to the ::fsm instance, passed as the xTaskCreate parameter.
 */
void fsm_dispatch(void *pv);

#endif /* __FSM_H__ */
