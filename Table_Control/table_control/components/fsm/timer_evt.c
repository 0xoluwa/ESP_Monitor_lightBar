#include "timer_evt.h"
#include "esp_timer.h"

/* global head of the armed timer linked list */
fsm_time_event *time_event_list_head = NULL;

static esp_timer_handle_t tick_timer_ = NULL;

static void tick_isr_cb(void *arg){
    (void)arg;
    fsm_tick();
}


/* --- tick driver ---------------------------------------------------------- */
/* period_us: tick period in microseconds (e.g. 1000 = 1 ms tick).           *
 * Uses ESP_TIMER_TASK dispatch — callback runs in the esp_timer service task.*
 * fsm_tick uses fsm_post_nonblock (0 timeout) so it never blocks that task.  */
void fsm_tick_init(uint64_t period_us){
    FSM_ASSERT(tick_timer_ == NULL);  /* guard against double-init */

    const esp_timer_create_args_t args = {
        .callback        = tick_isr_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "fsm_tick",
        .skip_unhandled_events = true,
    };

    ESP_ERROR_CHECK(esp_timer_create(&args, &tick_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer_, period_us));
}

void fsm_tick_deinit(void){
    if (tick_timer_ != NULL) {
        ESP_ERROR_CHECK(esp_timer_stop(tick_timer_));
        ESP_ERROR_CHECK(esp_timer_delete(tick_timer_));
        tick_timer_ = NULL;
    }
}


/* --- ctor ----------------------------------------------------------------- */
/* Binds the time event to its owning FSM and sets the signal it will carry.  *
 * Call once at startup before arming.                                        */
void fsm_time_event_ctor(fsm_time_event *me, fsm *owner, uint8_t signal){
    FSM_ASSERT(me);
    FSM_ASSERT(owner);
    me->super.signal  = signal;
    me->state_machine = owner;
    me->next          = NULL;
    me->down_counter  = 0;
    me->interval      = 0;
}


/* --- arm ------------------------------------------------------------------ */
/* nTicks must be >= 1.  interval = 0 → one-shot, interval > 0 → periodic.   */
void fsm_time_event_arm(fsm_time_event *me, uint32_t nTicks, uint32_t interval){
    FSM_ASSERT(nTicks > 0U);          /* guard against wrap-around bug */

    FSM_DISABLE_INTERRUPT;
    me->down_counter  = nTicks;
    me->interval      = interval;
    /* prepend to the armed list */
    me->next          = time_event_list_head;
    time_event_list_head = me;
    FSM_ENABLE_INTERRUPT;
}


/* --- rearm ---------------------------------------------------------------- */
/* Resets the counter to nTicks.  If the timer is already armed it stays in   *
 * the list (no unlink/relink).  If it expired or was disarmed it is added    *
 * back.  interval is preserved from the original arm call.                   *
 * Returns true if the timer was already running, false if it was re-inserted.*/
bool fsm_time_event_rearm(fsm_time_event *me, uint32_t nTicks){
    FSM_ASSERT(nTicks > 0U);

    FSM_DISABLE_INTERRUPT;

    /* check if already in the list */
    bool was_armed = false;
    fsm_time_event *curr = time_event_list_head;
    while (curr != NULL) {
        if (curr == me) { was_armed = true; break; }
        curr = curr->next;
    }

    me->down_counter = nTicks;

    if (!was_armed) {
        /* re-insert at head */
        me->next             = time_event_list_head;
        time_event_list_head = me;
    }

    FSM_ENABLE_INTERRUPT;
    return was_armed;
}


/* --- disarm --------------------------------------------------------------- */
/* Removes the time event from the armed list.  Safe to call if not armed.   */
void fsm_time_event_disarm(fsm_time_event *me){
    FSM_DISABLE_INTERRUPT;

    fsm_time_event **curr = &time_event_list_head;
    while (*curr != NULL) {
        if (*curr == me) {
            *curr = me->next;   /* unlink */
            me->next = NULL;
            break;
        }
        curr = &(*curr)->next;
    }

    FSM_ENABLE_INTERRUPT;
}


/* --- tick ----------------------------------------------------------------- */
/* Call from a periodic ISR or tick-hook at the desired timer resolution.     *
 * Walks the armed list, decrements counters, posts expired events.           */
void fsm_tick(void){
    fsm_time_event *current_time_event_ = time_event_list_head;

    while (current_time_event_ != NULL) {
        if (--current_time_event_->down_counter == 0U) {
            if (current_time_event_->interval != 0U) {
                current_time_event_->down_counter = current_time_event_->interval; /* reload periodic */
            }
            fsm_post((fsm *)current_time_event_->state_machine,
                             (fsm_event *)current_time_event_);
        }
        current_time_event_ = current_time_event_->next;
    }
}
