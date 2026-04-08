#include "fsm.h"
#include "esp_log.h"


void fsm_ctor(fsm *me, uint8_t queue_depth, uint32_t event_size){
    me->queue_ = xQueueCreate(queue_depth, event_size);
    configASSERT(me->queue_);
    me->event_structure_size_ = event_size;
}

void fsm_init(fsm *me, const char * task_name, state_handler entry_function){
    assert(entry_function);
    me->state = entry_function;
    assert(xTaskCreate(&fsm_dispatch, task_name, 4096, me, 1, &me->task_));
    uint8_t kick[me->event_structure_size_];      // buffer of correct size for the queue item
    ((fsm_event *)kick)->signal = SIG_INIT;
    xQueueSend(me->queue_, kick, portMAX_DELAY);
}

BaseType_t fsm_post(fsm *me, fsm_event const * event){
    return xQueueSend(me->queue_, event, portMAX_DELAY);
}

BaseType_t fsm_post_from_isr(fsm *me, fsm_event const * event){
    return xQueueSendFromISR(me->queue_, event, 0);
}

void fsm_dispatch(void *pv){
    fsm *me = (fsm *) pv;
    uint8_t e_buf[me->event_structure_size_];      // receive buffer large enough for any event
    for (;;) {
        xQueueReceive(me->queue_, &e_buf, portMAX_DELAY); // ← protected getter
        (*(me->state))(me, (fsm_event * ) &e_buf);
    }
}