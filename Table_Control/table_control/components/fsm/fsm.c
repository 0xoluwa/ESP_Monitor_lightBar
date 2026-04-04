#include "fsm.h"
#include "esp_log.h"


void fsm_ctor(fsm *me, uint8_t queue_depth, uint32_t event_size){
    me->queue_ = xQueueCreate(queue_depth, event_size);
    configASSERT(me->queue_);
}

void fsm_init(fsm *me, const char * task_name, state_handler entry_function){
    assert(entry_function);
    me->state = entry_function;
    assert(xTaskCreate(&fsm_dispatch, task_name, 4096, me, 1, &me->task_));
}

BaseType_t fsm_post(fsm *me, fsm_event const * event){
    return xQueueSend(me->queue_, event, portMAX_DELAY);
}

BaseType_t fsm_post_from_isr(fsm *me, fsm_event const * event){
    return xQueueSendFromISR(me->queue_, event, NULL);
}

void fsm_dispatch(void *pv){
    fsm *me = (fsm *) pv;
    fsm_event e;
    for (;;) {
        xQueueReceive(me->queue_, &e, portMAX_DELAY); // ← protected getter
        (*(me->state))(me, &e);
    }
}