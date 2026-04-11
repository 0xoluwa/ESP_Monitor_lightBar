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

    uint8_t event[me->event_structure_size_]; //adjusted for event size
    ((fsm_event *) event)->signal = SIG_INIT;

    fsm_post(me, (fsm_event *) event);
}

BaseType_t fsm_post(fsm *me, fsm_event const * event){
    return xQueueSend(me->queue_, event, portMAX_DELAY);
}

BaseType_t fsm_post_from_isr(fsm *me, fsm_event const * event){
    return xQueueSendFromISR(me->queue_, event, 0);
}

void fsm_dispatch(void *pv){
    fsm *me = (fsm *) pv;
    uint8_t event_buffer[me->event_structure_size_];
    fsm_event init_event = {SIG_INIT};
    for (;;) {
        xQueueReceive(me->queue_, &event_buffer, portMAX_DELAY);
        fsm_state result = (*(me->state))(me, (fsm_event *) &event_buffer);
        if (result == STATE_TRANSITION) {
            (*(me->state))(me, &init_event);
        }
    }
}