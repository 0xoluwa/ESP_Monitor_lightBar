/**
 * @file fsm_events.h
 * @brief Base FSM event structure.
 *
 * All application-specific event structures must embed ::fsm_event as their
 * first member so they can be safely cast to `fsm_event *` and dispatched
 * through the framework.
 */
#ifndef __FSM_EVENTS_H__
#define __FSM_EVENTS_H__

#include "fsm_signals.h"

/**
 * @brief Base event type dispatched through the FSM queue.
 *
 * Application events extend this by embedding it as the first member,
 * enabling safe up/down casts between the base and derived types.
 */
typedef struct fsm_event_ {
    uint8_t signal; /**< Signal identifier — one of ::fsm_signal or an application value. */
} fsm_event;

#endif /* __FSM_EVENTS_H__ */
