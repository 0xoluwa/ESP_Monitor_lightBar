/**
 * @file timer_evt.c
 * @brief FSM software timer – ISR tick driver and linked-list management.
 *
 * The tick driver uses an @c ESP_TIMER_ISR periodic timer so that
 * ::fsm_tick runs directly inside the esp_timer service ISR.  All list
 * operations use the FSM critical-section macros to be safe against
 * concurrent task-context calls (arm/disarm) and ISR-context calls (tick).
 */

#include "timer_evt.h"
#include "esp_timer.h"

/** @brief Head of the singly-linked list of currently armed timers. */
fsm_time_event *time_event_list_head = NULL;

/** @brief Handle for the underlying ESP high-resolution periodic timer. */
static esp_timer_handle_t tick_timer_ = NULL;

/**
 * @brief ESP timer ISR callback – delegates to ::fsm_tick.
 * @param arg Unused.
 */
static void IRAM_ATTR tick_isr_cb(void *arg){
    (void)arg;
    fsm_tick();
}


/* ── Tick driver ─────────────────────────────────────────────────────────── */

/**
 * @brief Start the hardware timer that drives all FSM software timers.
 *
 * Uses @c ESP_TIMER_ISR dispatch so the callback runs in the esp_timer
 * service ISR with minimal latency.  ::fsm_tick uses ::fsm_post_from_isr
 * (zero-timeout queue send) so it never blocks inside that ISR.
 *
 * @param period_us Tick period in microseconds (e.g. 1000 = 1 ms tick).
 */
void fsm_tick_init(uint64_t period_us){
    FSM_ASSERT(tick_timer_ == NULL);  /* guard against double-init */

    const esp_timer_create_args_t args = {
        .callback        = tick_isr_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_ISR,
        .name            = "fsm_tick",
        .skip_unhandled_events = true,
    };

    ESP_ERROR_CHECK(esp_timer_create(&args, &tick_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer_, period_us));
}

/**
 * @brief Stop and delete the hardware tick timer.
 *
 * No-op if the timer was never started.
 */
void fsm_tick_deinit(void){
    if (tick_timer_ != NULL) {
        ESP_ERROR_CHECK(esp_timer_stop(tick_timer_));
        ESP_ERROR_CHECK(esp_timer_delete(tick_timer_));
        tick_timer_ = NULL;
    }
}


/* ── Constructor ─────────────────────────────────────────────────────────── */

/**
 * @brief Bind a time event to its owning FSM and set its signal.
 *
 * Must be called once before any arm/disarm call.  The timer starts in a
 * disarmed state.
 *
 * @param me     Time event to construct.
 * @param owner  FSM that receives the event when the timer fires.
 * @param signal Signal carried by the event on expiry.
 */
void fsm_time_event_ctor(fsm_time_event *me, fsm *owner, uint8_t signal){
    FSM_ASSERT(me);
    FSM_ASSERT(owner);
    me->super.signal  = signal;
    me->state_machine = owner;
    me->next          = NULL;
    me->down_counter  = 0;
    me->interval      = 0;
}


/* ── Arm ─────────────────────────────────────────────────────────────────── */

/**
 * @brief Insert a timer event into the armed list with the given countdown.
 *
 * Asserts that the timer is not already in the list to prevent double-arm
 * corruption of the linked list.
 *
 * @param me       Timer to arm.
 * @param nTicks   Initial countdown (>= 1 tick).
 * @param interval Periodic reload; 0 for a one-shot timer.
 */
void fsm_time_event_arm(fsm_time_event *me, uint64_t nTicks, uint64_t interval){
    FSM_ASSERT(nTicks > 0U);

    FSM_DISABLE_INTERRUPT;

    bool already_armed = false;
    for (fsm_time_event *curr = time_event_list_head; curr != NULL; curr = curr->next) {
        if (curr == me) { already_armed = true; break; }
    }

    FSM_ASSERT(!already_armed);

    me->down_counter     = nTicks;
    me->interval         = interval;
    me->next             = time_event_list_head;
    time_event_list_head = me;

    FSM_ENABLE_INTERRUPT;
}


/* ── Rearm ───────────────────────────────────────────────────────────────── */

/**
 * @brief Reset the countdown, re-inserting the timer if it had expired.
 *
 * If the timer is already in the armed list its countdown is simply updated
 * without any unlink/relink, making this cheap to call from a running state.
 * If the timer had expired or been disarmed it is re-inserted at the head.
 * The @c interval value set during ::fsm_time_event_arm is preserved.
 *
 * @param me     Timer to rearm.
 * @param nTicks New countdown value (>= 1 tick).
 * @return true  if the timer was still running (already in the list),
 *         false if it was expired or disarmed and has been re-inserted.
 */
bool fsm_time_event_rearm(fsm_time_event *me, uint64_t nTicks){
    FSM_ASSERT(nTicks > 0U);

    FSM_DISABLE_INTERRUPT;

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


/* ── Disarm ──────────────────────────────────────────────────────────────── */

/**
 * @brief Remove a timer from the armed list.
 *
 * Safe to call if the timer has already expired (one-shot) or was never
 * armed.  Walks the list and unlinks the node if found.
 *
 * @param me Timer to disarm.
 */
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


/* ── Tick ────────────────────────────────────────────────────────────────── */

/**
 * @brief Walk the armed list, decrement counters, and fire expired timers.
 *
 * Runs inside the @c ESP_TIMER_ISR callback at the tick period configured
 * by ::fsm_tick_init.  Two-phase design:
 *  - Phase 1 (with ISR lock): decrement all counters, collect expired nodes.
 *    Periodic timers are reloaded; one-shot timers are unlinked.
 *  - Phase 2 (no lock): post events to owning FSMs via ::fsm_post_from_isr.
 */
void IRAM_ATTR fsm_tick(void){
    fsm_time_event *expired[MAX_FSM_TIMERS];
    int n_expired = 0;

    FSM_DISABLE_INTERRUPT_ISR;
    fsm_time_event *curr  = time_event_list_head;
    fsm_time_event *prev  = NULL;
    while (curr != NULL) {
        fsm_time_event *next = curr->next;
        if (--curr->down_counter == 0U) {
            if (curr->interval != 0U) {
                curr->down_counter = curr->interval;   /* periodic: reload */
                prev = curr;   /* stays in list – advance prev */
            } else {
                /* one-shot: unlink */
                if (prev == NULL) time_event_list_head = next;
                else              prev->next = next;
                curr->next = NULL;
            }
            FSM_ASSERT(n_expired < MAX_FSM_TIMERS);  /* catch overflow */
            expired[n_expired++] = curr;
        } else {
            prev = curr;
        }
        curr = next;
    }
    FSM_ENABLE_INTERRUPT_ISR;

    /* Phase 2: post events with no lock held */
    for (int i = 0; i < n_expired; i++) {
        fsm_post_from_isr((fsm *)expired[i]->state_machine, (fsm_event *)expired[i]);
    }
}
