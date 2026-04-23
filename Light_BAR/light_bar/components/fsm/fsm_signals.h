/**
 * @file fsm_signals.h
 * @brief Reserved signal codes used internally by the FSM framework.
 *
 * User-defined signals must start at ::SIG_USER_CODE to avoid colliding with
 * the framework's own ENTRY / EXIT / INIT signals.
 */
#ifndef __FSM_SIGNALS_H__
#define __FSM_SIGNALS_H__

#include <stdint.h>

/**
 * @brief FSM framework reserved signals.
 *
 * These are dispatched automatically by the framework during state
 * transitions.  Application signals must begin at ::SIG_USER_CODE.
 */
enum fsm_signal {
    SIG_ENTRY = 0, /**< Dispatched when a state is entered. */
    SIG_EXIT,      /**< Dispatched when a state is exited. */
    SIG_INIT,      /**< Dispatched after entry for initialisation / internal transition. */
    SIG_USER_CODE  /**< First value available for application-defined signals. */
};

#endif /* __FSM_SIGNALS_H__ */
