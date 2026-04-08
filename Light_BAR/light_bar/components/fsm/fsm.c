#include "fsm.h"
#include "timer_channel.h"
#include "esp_log.h"
#include <string.h>


void fsm_ctor(fsm *me, uint8_t queue_depth, uint32_t event_size){
    me->event_size_ = event_size;
    me->queue_ = xQueueCreate(queue_depth, event_size);
    configASSERT(me->queue_);

    me->Timers = heap_caps_malloc(sizeof(timer_t) * TIMER_CHANNEL_MAX,
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    configASSERT(me->Timers);

    for (int i = 0; i < TIMER_CHANNEL_MAX; i++) {
        me->Timers[i] = (timer_t){0};
    }
}

void fsm_init(fsm *me, const char * task_name, state_handler entry_function)
{
    assert(entry_function);
    me->state = entry_function;

    assert(xTaskCreate(&fsm_dispatch, task_name, 4096, me, 1, &me->task_));

    uint8_t kick[me->event_size_];      // buffer of correct size for the queue item
    ((fsm_event *)kick)->signal = SIG_INIT;
    fsm_post(me, (fsm_event *) kick);
}

BaseType_t fsm_post(fsm *me, fsm_event const * event){
    return xQueueSend(me->queue_, event, 0);
}

BaseType_t fsm_post_from_isr(fsm *me, fsm_event const * event){
    return xQueueSendFromISR(me->queue_, event, NULL);
}

void fsm_dispatch(void *pv){
    fsm *me = (fsm *) pv;
    uint8_t e_buf[me->event_size_];      // receive buffer large enough for any event
    fsm_event entry_e = {SIG_ENTRY};
    fsm_event exit_e  = {SIG_EXIT};

    for (;;) {
        xQueueReceive(me->queue_, e_buf, portMAX_DELAY);

        state_handler old_state = me->state; 
        fsm_state result = (*(me->state))(me, (fsm_event *)e_buf);

        // Synthesise SIG_EXIT on the source state and SIG_ENTRY on the target state.
        // States that don't use entry/exit simply return STATE_IGNORED for them.
        if (result == STATE_TRANSITION) {
            old_state(me, &exit_e);           // SIG_EXIT → source state
            (*(me->state))(me, &entry_e);     // SIG_ENTRY → target state
        }
    }
}

void fsm_timer_arm(fsm *me, fsm_event const *event, timer_channel_t channel, uint64_t frequency, uint64_t tick)
{
    timer_t *t = &me->Timers[channel];
    t->owner   = me;
    t->channel = channel;
    t->tick    = tick;
    // Copy the full event payload (up to event_size bytes) into the timer slot.
    memcpy(t->event_data, event, me->event_size_);

    if (t->timer_handle == NULL) {
        t->timer_handle = xTimerCreate("fsm_timer", (TickType_t)frequency, pdTRUE, t, fsm_timer_callback);
        configASSERT(t->timer_handle);
    } else {
        vTimerSetTimerID(t->timer_handle, t);
        xTimerChangePeriod(t->timer_handle, (TickType_t)frequency, 0);
    }

    xTimerStart(t->timer_handle, 0);
}

void fsm_timer_callback(TimerHandle_t xTimer)
{
    timer_t *t = (timer_t *)pvTimerGetTimerID(xTimer);
    fsm *me = (fsm *)t->owner;
    // Post the full event_data buffer — correct number of bytes for the queue item size.
    xQueueSend(me->queue_, t->event_data, 0);

    if (t->tick > 0) {
        t->tick--;
        if (t->tick == 0) {
            xTimerStop(t->timer_handle, 0);
        }
    }
}

void fsm_timer_disarm(fsm *me, timer_channel_t channel)
{
    timer_t *t = &me->Timers[channel];
    if (t->timer_handle == NULL) {
        return;
    }
    xTimerStop(t->timer_handle, portMAX_DELAY);
}

void fsm_timer_update_frequency(fsm *me, timer_channel_t channel, uint64_t frequency)
{
    timer_t *t = &me->Timers[channel];
    if (t->timer_handle == NULL) {
        return;
    }
    t->frequency = frequency;
    xTimerChangePeriod(t->timer_handle, (TickType_t)frequency, portMAX_DELAY);
}

void fsm_timer_update_tick(fsm *me, timer_channel_t channel, uint64_t tick)
{
    me->Timers[channel].tick = tick;
}
