/**
 * @file timer_evt.h
 * @brief FSM software timer events backed by the ESP-IDF high-resolution timer.
 *
 * A ::fsm_time_event is a specialised ::fsm_event that is automatically
 * posted to its owning FSM when a countdown reaches zero.  All active timers
 * are tracked in a singly-linked list that is walked by the periodic ISR
 * callback (::fsm_tick).
 *
 * Typical usage:
 * @code
 *   // 1. Declare as a member of your FSM struct.
 *   fsm_time_event my_timeout;
 *
 *   // 2. Construct once at startup.
 *   fsm_time_event_ctor(&my_timeout, (fsm *)me, MY_TIMEOUT_SIG);
 *
 *   // 3. Arm on state entry (one-shot example).
 *   fsm_time_event_arm(&my_timeout, 5000, 0);   // 5000 ticks, one-shot
 *
 *   // 4. Disarm on state exit if no expiry has occurred.
 *   fsm_time_event_disarm(&my_timeout);
 * @endcode
 */

#ifndef __TIMER_EVT__
#define __TIMER_EVT__

#include "fsm.h"
#include <stdbool.h>

/** @brief Forward declaration of the time event struct. */
typedef struct fsm_time_event_ fsm_time_event;

/**
 * @brief Software timer event node.
 *
 * Inherits from ::fsm_event (super must be first) so the struct can be
 * passed directly to fsm_post_from_isr() without casting the payload.
 */
struct fsm_time_event_ {
    fsm_event       super;          /**< IS-A fsm_event – carries the signal; must be first.  */
    fsm_time_event *next;           /**< Intrusive linked-list pointer to the next armed timer.*/
    void           *state_machine;  /**< Owning FSM that receives the event on expiry.         */
    uint64_t        down_counter;   /**< Remaining ticks before expiry.                        */
    uint64_t        interval;       /**< Reload value: 0 = one-shot, >0 = periodic.            */
};

/**
 * @brief Head of the global armed-timer linked list.
 *
 * Protected by ::fsm_evt_mux; walk only inside a critical section.
 */
extern fsm_time_event *time_event_list_head;

/**
 * @brief Bind a time event to its owning FSM and assign its signal.
 *
 * Call once at startup, before any arm/disarm operation.
 *
 * @param me     Time event to initialise.
 * @param owner  FSM that will receive the event on expiry.
 * @param signal Signal identifier posted when the timer fires.
 */
void fsm_time_event_ctor(fsm_time_event *me, fsm *owner, uint8_t signal);

/**
 * @brief Arm a time event (insert into the active list).
 *
 * @param me       Time event to arm.  Must not already be armed.
 * @param nTicks   Countdown value in ticks (must be >= 1).
 * @param interval Periodic reload value in ticks; 0 for a one-shot timer.
 */
void fsm_time_event_arm(fsm_time_event *me, uint64_t nTicks, uint64_t interval);

/**
 * @brief Reset a time event's countdown, re-inserting it if it had expired.
 *
 * The original @c interval from ::fsm_time_event_arm is preserved.
 *
 * @param me     Time event to rearm.
 * @param nTicks New countdown value in ticks (must be >= 1).
 * @return true  if the timer was already in the list (still running),
 *         false if it had expired or been disarmed and was re-inserted.
 */
bool fsm_time_event_rearm(fsm_time_event *me, uint64_t nTicks);

/**
 * @brief Cancel a time event, removing it from the armed list.
 *
 * Safe to call even if the timer has already expired or was never armed.
 *
 * @param me Time event to disarm.
 */
void fsm_time_event_disarm(fsm_time_event *me);

/**
 * @brief Periodic tick handler – decrement counters and post expired events.
 *
 * Called from the ESP_TIMER_ISR callback at the configured tick period.
 * Expired one-shot timers are removed from the list; periodic timers are
 * reloaded with their interval.  Events are posted via ::fsm_post_from_isr
 * after the critical section is released.
 */
void IRAM_ATTR fsm_tick(void);

/**
 * @brief Initialise and start the periodic hardware tick timer.
 *
 * Creates an @c ESP_TIMER_ISR periodic timer that drives ::fsm_tick.
 * Must be called once before any FSM timer is armed.
 *
 * @param period_us Tick period in microseconds (e.g. 1000 for a 1 ms tick).
 */
void fsm_tick_init(uint64_t period_us);

/**
 * @brief Stop and delete the hardware tick timer.
 *
 * Safe to call only when no FSM timer events are actively armed.
 */
void fsm_tick_deinit(void);

#endif
