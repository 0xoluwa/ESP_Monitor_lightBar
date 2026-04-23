/**
 * @file fsm_port.c
 * @brief FSM portability layer – definition of the shared critical-section mutex.
 *
 * ::fsm_evt_mux is used by the FSM timer module to guard the armed-timer
 * linked list against concurrent access from task and ISR contexts.
 */

#include "fsm_port.h"

/** @brief Spinlock protecting all armed-timer list operations. */
portMUX_TYPE fsm_evt_mux = portMUX_INITIALIZER_UNLOCKED;
