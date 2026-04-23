/**
 * @file fsm_port.h
 * @brief FreeRTOS / ESP-IDF portability layer for the FSM framework.
 *
 * Maps platform queue, task, and critical-section primitives to the generic
 * macros used throughout the FSM implementation.  To port the framework to a
 * different RTOS or bare-metal environment, only this file (and the matching
 * fsm_port.c) needs to be replaced.
 *
 * Macro groups:
 *  - @c FSM_QUEUE_*         – event queue create / send / receive
 *  - @c FSM_TASK_*          – task handle type and task creation
 *  - @c FSM_DISABLE/ENABLE_INTERRUPT(_ISR) – critical-section guards
 *  - @c FSM_ASSERT           – assertion mapped to FreeRTOS configASSERT
 */

#ifndef __FSM_PORT_H__
#define __FSM_PORT_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** @brief Maximum number of simultaneously armed FSM software timers. */
#define MAX_FSM_TIMERS 4


/* ── Queue ────────────────────────────────────────────────────────────────── */

/** @brief Opaque queue handle type (wraps FreeRTOS QueueHandle_t). */
#define FSM_QUEUE_HANDLE QueueHandle_t

/**
 * @brief Create a new event queue.
 * @param queue_depth Maximum number of events that can be buffered.
 * @param event_size  Size of a single event item in bytes.
 */
#define FSM_CREATE_QUEUE(queue_depth, event_size) xQueueCreate(queue_depth, event_size)

/**
 * @brief Post an event to a queue from task context (non-blocking).
 * @param FSM_QUEUE_HANDLE  Handle returned by ::FSM_CREATE_QUEUE.
 * @param EVENT_PTR         Pointer to the event to copy into the queue.
 * @return pdTRUE on success, pdFALSE if the queue is full.
 */
#define FSM_QUEUE_SEND(FSM_QUEUE_HANDLE, EVENT_PTR)    xQueueSend(FSM_QUEUE_HANDLE, EVENT_PTR, 0)

/**
 * @brief Post an event to a queue from an ISR context.
 *
 * Issues a context-switch request via portYIELD_FROM_ISR if a higher-priority
 * task was unblocked.
 *
 * @param handle   Queue handle.
 * @param buf      Pointer to the event data to copy.
 */
#define FSM_QUEUE_SEND_FROM_ISR(handle, buf)                        \
    do {                                                            \
        BaseType_t _woken = pdFALSE;                               \
        xQueueSendFromISR((handle), (buf), &_woken);               \
        portYIELD_FROM_ISR(_woken);                                \
    } while(0)

/**
 * @brief Block until an event is available and copy it into @p BUFFER.
 * @param FSM_QUEUE_HANDLE  Queue handle.
 * @param BUFFER            Destination buffer; must be at least event_size bytes.
 */
#define FSM_QUEUE_RECIEVE(FSM_QUEUE_HANDLE, BUFFER)    xQueueReceive(FSM_QUEUE_HANDLE, BUFFER, portMAX_DELAY)


/* ── Task ─────────────────────────────────────────────────────────────────── */

/** @brief Opaque task handle type (wraps FreeRTOS TaskHandle_t). */
#define FSM_TASK_HANDLE TaskHandle_t

/**
 * @brief Create a FreeRTOS task for the FSM dispatch loop.
 * @param TASK_PTR          Function pointer to ::fsm_dispatch.
 * @param NAME              Human-readable task name string.
 * @param STACK_SIZE        Stack depth in words.
 * @param PARAMETER_PTR     Passed as the @c pv argument to @p TASK_PTR.
 * @param TASK_HANDLE_PTR   Output task handle.
 * @return pdPASS on success.
 */
#define FSM_TASK_CREATE(TASK_PTR, NAME, STACK_SIZE, PARAMETER_PTR, TASK_HANDLE_PTR) xTaskCreate(TASK_PTR, NAME, STACK_SIZE, PARAMETER_PTR, 4, TASK_HANDLE_PTR)


/* ── Critical sections ────────────────────────────────────────────────────── */

/** @brief Shared spinlock protecting the armed-timer linked list. */
extern portMUX_TYPE fsm_evt_mux;

/** @brief Enter a critical section from task context (disables interrupts). */
#define FSM_DISABLE_INTERRUPT   portENTER_CRITICAL(&fsm_evt_mux)

/** @brief Exit a critical section from task context (re-enables interrupts). */
#define FSM_ENABLE_INTERRUPT    portEXIT_CRITICAL(&fsm_evt_mux)

/** @brief Enter a critical section from an ISR (nested-safe). */
#define FSM_DISABLE_INTERRUPT_ISR   portENTER_CRITICAL_ISR(&fsm_evt_mux)

/** @brief Exit a critical section from an ISR (nested-safe). */
#define FSM_ENABLE_INTERRUPT_ISR    portEXIT_CRITICAL_ISR(&fsm_evt_mux)


/* ── Assertion ────────────────────────────────────────────────────────────── */

/** @brief Assert a condition; halts execution on failure (maps to configASSERT). */
#define FSM_ASSERT configASSERT

#endif
