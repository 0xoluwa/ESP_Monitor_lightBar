#include "fsm.h"
#include "esp_log.h"

fsm_event RESERVED_EVENT[SIG_USER_CODE] = {
    {.signal = SIG_ENTRY},
    {.signal = SIG_EXIT},
    {.signal = SIG_INIT}
};


void fsm_ctor(fsm *me, uint8_t queue_depth, uint16_t event_size){
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

BaseType_t fsm_post_nonblock(fsm *me, fsm_event const * event){
    return xQueueSend(me->queue_, event, 0);
}

BaseType_t fsm_post_from_isr(fsm *me, fsm_event const * event){
    return xQueueSendFromISR(me->queue_, event, 0);
}

void fsm_dispatch(void *pv){
    fsm *me = (fsm *) pv;
    uint8_t *event_buffer = pvPortMalloc(me->event_structure_size_);
    configASSERT(event_buffer);
    state_handler  current_state_;
    state_handler  previous_state_;

    for (;;) {
        xQueueReceive(me->queue_, event_buffer, portMAX_DELAY);
        current_state_ = me->state;

        fsm_state result = (*(me->state))(me, (fsm_event *) event_buffer);

        while (result == STATE_TRANSITION) {
            previous_state_ = current_state_;
            current_state_ = me->state;
            
            STATE_EXIT(previous_state_);
            STATE_ENTRY(current_state_);
            
            /*call the state handler init*/
            if (current_state_ != me->state) ESP_ERROR_CHECK(-1);               
            else{                                                               
                result = (*current_state_) ((fsm *) me, &RESERVED_EVENT[SIG_INIT]);                
            }
        }
    }
}