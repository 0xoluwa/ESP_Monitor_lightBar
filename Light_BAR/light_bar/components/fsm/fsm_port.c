/**
 * @file fsm_port.c
 * @brief FreeRTOS port layer — global spinlock definition.
 */
#include "fsm_port.h"

/** @brief Spinlock protecting the shared timer list across tasks and ISRs. */
portMUX_TYPE fsm_evt_mux = portMUX_INITIALIZER_UNLOCKED;
