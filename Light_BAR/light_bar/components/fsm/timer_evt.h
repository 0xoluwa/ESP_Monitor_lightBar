/**
 * @file timer_evt.h
 * @brief Software timer events for the FSM framework.
 *
 * Provides one-shot and periodic timers that post ::fsm_event messages to
 * their owning FSM when they expire.  All timers share a single hardware
 * periodic interrupt driven by fsm_tick_init().
 *
 * ## Typical usage
 * @code
 * fsm_time_event my_timer;
 * fsm_time_event_ctor(&my_timer, (fsm *)&my_sm, MY_SIGNAL);
 *
 * // one-shot: fire once after 20 ticks
 * fsm_time_event_arm(&my_timer, 20, 0);
 *
 * // periodic: fire every 20 ticks
 * fsm_time_event_arm(&my_timer, 20, 20);
 *
 * // reset the countdown (re-inserts if already expired)
 * fsm_time_event_rearm(&my_timer, 20);
 *
 * // cancel
 * fsm_time_event_disarm(&my_timer);
 * @endcode
 */
#ifndef __TIMER_EVT__
#define __TIMER_EVT__

#include "fsm.h"
#include <stdbool.h>

/** @brief Forward declaration. */
typedef struct fsm_time_event_ fsm_time_event;

/**
 * @brief Software timer that delivers an FSM event on expiry.
 *
 * Participates in an intrusive singly-linked list of all armed timers.
 * The ::fsm_event base **must** remain the first member so the struct can be
 * cast to `fsm_event *` when posting to the owning FSM.
 */
struct fsm_time_event_ {
    fsm_event       super;         /**< IS-A fsm_event — must be first.               */
    fsm_time_event *next;          /**< Next node in the armed intrusive linked list.  */
    void           *state_machine; /**< Owning FSM; target of the expiry post.         */
    uint64_t        down_counter;  /**< Ticks remaining until next expiry.             */
    uint64_t        interval;      /**< 0 = one-shot; > 0 = periodic reload value.     */
};

/** @brief Head of the singly-linked list of all currently armed timers. */
extern fsm_time_event *time_event_list_head;

/**
 * @brief Bind a time event to its owning FSM and set its signal.
 *
 * Must be called once before any arm or disarm operations.
 *
 * @param me     Pointer to the time event to initialise.
 * @param owner  FSM that will receive the event when it expires.
 * @param signal Signal value written into the event on expiry.
 */
void fsm_time_event_ctor(fsm_time_event *me, fsm *owner, uint8_t signal);

/**
 * @brief Arm a time event and insert it into the active list.
 *
 * The timer must **not** already be armed (asserted).
 *
 * @param me       Pointer to the time event.
 * @param nTicks   Ticks until first expiry (must be ≥ 1).
 * @param interval Reload value for periodic mode; pass 0 for one-shot.
 */
void fsm_time_event_arm(fsm_time_event *me, uint64_t nTicks, uint64_t interval);

/**
 * @brief Reset the countdown without unlinking and relinking the timer.
 *
 * If the timer has already expired or was never armed it is re-inserted at
 * the head of the list.  The original @c interval is preserved.
 *
 * @param me     Pointer to the time event.
 * @param nTicks New countdown value (must be ≥ 1).
 * @return `true`  — timer was still in the list (counter reset in place).
 * @return `false` — timer was re-inserted (had expired or was not armed).
 */
bool fsm_time_event_rearm(fsm_time_event *me, uint64_t nTicks);

/**
 * @brief Remove a time event from the armed list.
 *
 * Safe to call even when the timer is not currently armed.
 *
 * @param me Pointer to the time event to disarm.
 */
void fsm_time_event_disarm(fsm_time_event *me);

/**
 * @brief Decrement all armed timer counters and post expired events.
 *
 * Called from the periodic hardware ISR set up by fsm_tick_init().
 * Phase 1 (under ISR critical section): walk the list, decrement counters,
 * collect expired timers, unlink one-shots and reload periodics.
 * Phase 2 (no lock held): post the expired events via fsm_post_from_isr().
 */
void fsm_tick(void);

/**
 * @brief Create and start the periodic esp_timer that drives fsm_tick().
 *
 * Uses ESP_TIMER_ISR dispatch to minimise jitter.  Must be called once
 * before any timers are armed.
 *
 * @param period_us Tick period in microseconds (e.g. 1000 for a 1 ms tick).
 */
void fsm_tick_init(uint64_t period_us);

/**
 * @brief Stop and delete the underlying esp_timer.
 *
 * Safe to call only when no timers remain armed.
 */
void fsm_tick_deinit(void);

#endif /* __TIMER_EVT__ */
