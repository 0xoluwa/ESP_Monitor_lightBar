/**
 * @file fsm_signals.h
 * @brief Reserved signal values used internally by the FSM framework.
 *
 * Every FSM event carries a signal identifier.  The values defined here are
 * consumed by the framework itself (entry, exit, and init pseudo-transitions).
 * Application-defined signals must start at ::SIG_USER_CODE or higher to
 * avoid collisions with these reserved values.
 */

#ifndef __FSM_SIGNALS_H__
#define __FSM_SIGNALS_H__

#include "stdint.h"

/**
 * @brief FSM lifecycle signal identifiers.
 *
 * Values below ::SIG_USER_CODE are reserved for framework use.
 * User-defined signal enumerations should begin:
 * @code
 *   enum my_signals {
 *       SIG_MY_FIRST = SIG_USER_CODE,
 *       SIG_MY_SECOND,
 *       ...
 *   };
 * @endcode
 */
enum fsm_signal {
    SIG_ENTRY = 0,   /**< Delivered once when a state is entered.              */
    SIG_EXIT,        /**< Delivered once just before a state is exited.        */
    SIG_INIT,        /**< Delivered after entry to trigger an initial transition.*/
    SIG_USER_CODE    /**< First value available for application-defined signals. */
};


#endif
