/**
 * @file fsm.h
 * @brief Hierarchical Finite State Machine (HSM) framework – core interface.
 *
 * Each FSM instance owns a FreeRTOS task that blocks on an event queue.
 * When an event arrives the task calls the current state handler; a handler
 * returns one of the ::fsm_state codes to indicate what the framework should
 * do next.
 *
 * State handlers have the signature:
 * @code
 *   fsm_state my_state(fsm *me, fsm_event *event);
 * @endcode
 *
 * Transition macros ─ call these from inside a state handler:
 *  - TRAN()   – transition to a new state.
 *  - SUPER()  – delegate an unhandled event to a parent state.
 *
 * Lifecycle pseudo-events ─ delivered automatically by the framework:
 *  - SIG_ENTRY – state just became active.
 *  - SIG_EXIT  – state is about to become inactive.
 *  - SIG_INIT  – initial transition hook after entry.
 */

#ifndef __FSM_H__
#define __FSM_H__

#include "fsm_events.h"
#include "fsm_port.h"


/* ── Return codes from state handlers ────────────────────────────────────── */

/**
 * @brief Values returned by a state handler to communicate intent to the
 *        dispatch loop.
 */
typedef enum{
    STATE_IGNORED,      /**< Event was not handled; no action taken.            */
    STATE_HANDLED = 1,  /**< Event was fully consumed; no transition needed.    */
    STATE_TRANSITION,   /**< A TRAN() was executed; run exit/entry sequence.    */
    SUPER_TRANSITION,   /**< Reserved for hierarchical super-state delegation.  */
    CHILD_TRANSITION,   /**< Reserved for child-initiated transitions.          */
    MAX_STATE           /**< Sentinel – not a valid return value.               */
} fsm_state;


/* ── Core types ───────────────────────────────────────────────────────────── */

typedef struct FSM fsm;

/**
 * @brief State handler function pointer type.
 * @param me    Pointer to the owning FSM instance (cast to the concrete type
 *              inside the handler).
 * @param event Pointer to the incoming event.
 * @return One of the ::fsm_state codes.
 */
typedef fsm_state (*state_handler)(fsm *me, fsm_event *event);

/**
 * @brief Base FSM control block.
 *
 * Embed this as the first member of every concrete state machine struct so
 * that the concrete pointer can be safely cast to @c fsm* and back:
 * @code
 *   struct MY_FSM {
 *       fsm super;   // must be first
 *       int my_data;
 *   };
 * @endcode
 */
struct FSM{
    state_handler    state;               /**< Pointer to the currently active state handler. */
    FSM_QUEUE_HANDLE queue_;              /**< Event queue for this FSM instance.             */
    FSM_TASK_HANDLE  task_;               /**< FreeRTOS task handle running the dispatch loop.*/
    uint16_t         event_structure_size_; /**< Size in bytes of the concrete event struct.  */
};

/**
 * @brief Pre-built reserved events for SIG_ENTRY, SIG_EXIT, and SIG_INIT.
 *
 * Indexed by the corresponding ::fsm_signal value.  The dispatch loop posts
 * these directly to state handlers without going through the queue.
 */
extern fsm_event RESERVED_EVENT[];


/* ── Transition macros ────────────────────────────────────────────────────── */

/**
 * @brief Transition to a new state.
 *
 * Sets the FSM's current state pointer to @p STATE and returns
 * ::STATE_TRANSITION so the dispatch loop executes the exit/entry sequence.
 *
 * @param STATE State handler function to transition to.
 */
#define TRAN(STATE) (((fsm*) me)->state = ((state_handler) STATE), STATE_TRANSITION)

/**
 * @brief Delegate event processing to a parent (super) state.
 *
 * Calls @p STATE with the current event and returns its result, allowing
 * hierarchical event bubbling.
 *
 * @param STATE Parent state handler function.
 * @param EVENT Pointer to the event being processed.
 */
#define SUPER(STATE, EVENT) ((STATE) (me, EVENT))

/**
 * @brief Deliver SIG_EXIT to @p STATE.
 *
 * Asserts that @p STATE is NOT the current state (exit is called on the
 * state being left) then invokes the handler with a SIG_EXIT event.
 *
 * @param STATE State handler that is being exited.
 */
#define STATE_EXIT(STATE)   if (STATE == ((fsm *) me)->state) FSM_ASSERT(false);               \
                            else{                                                               \
                                (STATE) ((fsm *) me, &RESERVED_EVENT[SIG_EXIT]);                 \
                            }

/**
 * @brief Deliver SIG_ENTRY to @p STATE.
 *
 * Asserts that @p STATE IS the current state (entry is called on the newly
 * active state) then invokes the handler with a SIG_ENTRY event.
 *
 * @param STATE State handler that is being entered.
 */
#define STATE_ENTRY(STATE)  if (STATE != ((fsm *) me)->state) FSM_ASSERT(false);              \
                            else{                                                              \
                                (STATE) ((fsm *) me, &RESERVED_EVENT[SIG_ENTRY]);               \
                            }

/**
 * @brief Deliver SIG_INIT to @p STATE.
 *
 * Asserts that @p STATE is the current state then invokes it with a
 * SIG_INIT event to allow an initial transition.
 *
 * @param STATE State handler to initialise.
 */
#define STATE_INIT(STATE)   if (STATE != ((fsm *) me)->state) FSM_ASSERT(false);               \
                            else{                                                               \
                                (STATE) ((fsm *) me, &RESERVED_EVENT[SIG_INIT]);                \
                            }


/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Construct an FSM instance.
 *
 * Creates the event queue.  Must be called before ::fsm_init.
 *
 * @param me          Pointer to the FSM control block.
 * @param queue_depth Maximum number of events that can be queued at once.
 * @param event_size  Size in bytes of the concrete event struct used by this FSM.
 */
void fsm_ctor(fsm *me, uint8_t queue_depth, uint16_t event_size);

/**
 * @brief Initialise and start an FSM.
 *
 * Sets the initial state handler, spawns the dispatch task, and posts a
 * SIG_INIT event to kick off the first transition.
 *
 * @param me             Pointer to the FSM control block (already constructed).
 * @param task_name      FreeRTOS task name string (used in debugging).
 * @param entry_function Initial state handler; must not be NULL.
 */
void fsm_init(fsm *me, const char * task_name, state_handler entry_function);

/**
 * @brief Post an event to an FSM from task context.
 *
 * Non-blocking: returns immediately if the queue is full.
 *
 * @param me    Target FSM.
 * @param event Pointer to the event to copy into the queue.
 * @return true if the event was enqueued, false if the queue was full.
 */
bool fsm_post(fsm *me, fsm_event const *event);

/**
 * @brief Post an event to an FSM from an ISR context.
 *
 * Non-blocking and ISR-safe.  May trigger a context switch via
 * portYIELD_FROM_ISR if a higher-priority task is unblocked.
 *
 * @param me    Target FSM.
 * @param event Pointer to the event to copy into the queue.
 */
void IRAM_ATTR fsm_post_from_isr(fsm *me, fsm_event const * event);

/**
 * @brief FSM dispatch loop (FreeRTOS task entry point).
 *
 * Blocks waiting for events on the FSM queue and drives the state machine.
 * Created internally by ::fsm_init; do not call directly.
 *
 * @param pv Pointer to the ::fsm control block passed as the task parameter.
 */
void fsm_dispatch(void *pv);


#endif
