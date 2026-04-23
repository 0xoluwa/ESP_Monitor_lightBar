/**
 * @file fsm_events.h
 * @brief Base FSM event structure.
 *
 * All application-specific event structs must embed ::fsm_event as their
 * first member (the "super" pattern) so they can be safely up-cast to and
 * down-cast from the base type without breaking strict-aliasing rules:
 * @code
 *   typedef struct {
 *       fsm_event super;      // must be first
 *       int       my_payload;
 *   } my_event_t;
 * @endcode
 */

#ifndef __FSM_EVENTS_H__
#define __FSM_EVENTS_H__

#include "fsm_signals.h"

/**
 * @brief Minimum event type carried through every FSM queue.
 *
 * @note Embed this struct as the *first* member of any derived event type.
 */
typedef struct fsm_event_{
    uint8_t signal; /**< Signal identifier – one of ::fsm_signal or a user-defined value. */
} fsm_event;


#endif
