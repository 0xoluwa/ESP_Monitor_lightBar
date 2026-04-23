/**
 * @file fsm_port.h
 * @brief FreeRTOS port layer for the FSM framework.
 *
 * Abstracts queue, task, and critical-section primitives so that the core FSM
 * logic in fsm.h / fsm.c remains RTOS-agnostic.  Replace this file to port
 * the framework to a different RTOS.
 */
#ifndef __FSM_PORT_H__
#define __FSM_PORT_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** @brief Maximum number of simultaneously armed FSM time events. */
#define MAX_FSM_TIMERS 4

/* ── Queue ────────────────────────────────────────────────────────────────── */

/** @brief Opaque queue handle type alias. */
#define FSM_QUEUE_HANDLE QueueHandle_t

/** @brief Create a FreeRTOS queue for FSM events. */
#define FSM_CREATE_QUEUE(queue_depth, event_size) \
    xQueueCreate(queue_depth, event_size)

/** @brief Enqueue an event from task context (non-blocking, zero timeout). */
#define FSM_QUEUE_SEND(FSM_QUEUE_HANDLE, EVENT_PTR) \
    xQueueSend(FSM_QUEUE_HANDLE, EVENT_PTR, 0)

/**
 * @brief Enqueue an event from ISR context with automatic task yield.
 *
 * Calls xQueueSendFromISR and triggers a context switch if a higher-priority
 * task was unblocked by the enqueue.
 */
#define FSM_QUEUE_SEND_FROM_ISR(handle, buf)                        \
    do {                                                            \
        BaseType_t _woken = pdFALSE;                               \
        xQueueSendFromISR((handle), (buf), &_woken);               \
        portYIELD_FROM_ISR(_woken);                                \
    } while (0)

/** @brief Block indefinitely until an event is available on the queue. */
#define FSM_QUEUE_RECIEVE(FSM_QUEUE_HANDLE, BUFFER) \
    xQueueReceive(FSM_QUEUE_HANDLE, BUFFER, portMAX_DELAY)

/* ── Task ─────────────────────────────────────────────────────────────────── */

/** @brief Opaque task handle type alias. */
#define FSM_TASK_HANDLE TaskHandle_t

/** @brief Create a FreeRTOS task for the FSM dispatch loop at priority 4. */
#define FSM_TASK_CREATE(TASK_PTR, NAME, STACK_SIZE, PARAMETER_PTR, TASK_HANDLE_PTR) \
    xTaskCreate(TASK_PTR, NAME, STACK_SIZE, PARAMETER_PTR, 4, TASK_HANDLE_PTR)

/* ── Critical sections ────────────────────────────────────────────────────── */

/** @brief Spinlock protecting the timer linked list from concurrent task and ISR access. */
extern portMUX_TYPE fsm_evt_mux;

/** @brief Enter a critical section from task context. */
#define FSM_DISABLE_INTERRUPT     portENTER_CRITICAL(&fsm_evt_mux)
/** @brief Exit a critical section from task context. */
#define FSM_ENABLE_INTERRUPT      portEXIT_CRITICAL(&fsm_evt_mux)

/** @brief Enter a critical section from ISR context. */
#define FSM_DISABLE_INTERRUPT_ISR portENTER_CRITICAL_ISR(&fsm_evt_mux)
/** @brief Exit a critical section from ISR context. */
#define FSM_ENABLE_INTERRUPT_ISR  portEXIT_CRITICAL_ISR(&fsm_evt_mux)

/* ── Assert ───────────────────────────────────────────────────────────────── */

/** @brief Runtime assertion mapped to FreeRTOS configASSERT. */
#define FSM_ASSERT configASSERT

#endif /* __FSM_PORT_H__ */
