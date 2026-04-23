#ifndef __FSM_PORT_H__
#define __FSM_PORT_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX_FSM_TIMERS 4



#define FSM_QUEUE_HANDLE QueueHandle_t
#define FSM_CREATE_QUEUE(queue_depth, event_size) xQueueCreate(queue_depth, event_size)
#define FSM_QUEUE_SEND(FSM_QUEUE_HANDLE, EVENT_PTR)    xQueueSend(FSM_QUEUE_HANDLE, EVENT_PTR, 0)
#define FSM_QUEUE_SEND_FROM_ISR(handle, buf)                        \
    do {                                                            \
        BaseType_t _woken = pdFALSE;                               \
        xQueueSendFromISR((handle), (buf), &_woken);               \
        portYIELD_FROM_ISR(_woken);                                \
    } while(0)

#define FSM_QUEUE_RECIEVE(FSM_QUEUE_HANDLE, BUFFER)    xQueueReceive(FSM_QUEUE_HANDLE, BUFFER, portMAX_DELAY)



#define FSM_TASK_HANDLE TaskHandle_t
#define FSM_TASK_CREATE(TASK_PTR, NAME, STACK_SIZE, PARAMETER_PTR, TASK_HANDLE_PTR) xTaskCreate(TASK_PTR, NAME, STACK_SIZE, PARAMETER_PTR, 4, TASK_HANDLE_PTR)

extern portMUX_TYPE fsm_evt_mux;
#define FSM_DISABLE_INTERRUPT   portENTER_CRITICAL(&fsm_evt_mux)
#define FSM_ENABLE_INTERRUPT    portEXIT_CRITICAL(&fsm_evt_mux)

#define FSM_DISABLE_INTERRUPT_ISR   portENTER_CRITICAL_ISR(&fsm_evt_mux)
#define FSM_ENABLE_INTERRUPT_ISR    portEXIT_CRITICAL_ISR(&fsm_evt_mux)



#define FSM_ASSERT configASSERT

#endif