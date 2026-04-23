/**
 * @file timer_evt.c
 * @brief Software timer event implementation for the FSM framework.
 */
#include "timer_evt.h"
#include "esp_timer.h"

/** @brief Head of the intrusive linked list of all currently armed timers. */
fsm_time_event *time_event_list_head = NULL;

/** @brief Handle for the underlying esp_timer periodic tick driver. */
static esp_timer_handle_t tick_timer_ = NULL;

/**
 * @brief esp_timer ISR callback — forwards every tick to fsm_tick().
 * @param arg Unused.
 */
static void tick_isr_cb(void *arg)
{
    (void)arg;
    fsm_tick();
}

/* ── Tick driver ──────────────────────────────────────────────────────────── */

void fsm_tick_init(uint64_t period_us)
{
    FSM_ASSERT(tick_timer_ == NULL); /* guard against double-init */

    const esp_timer_create_args_t args = {
        .callback              = tick_isr_cb,
        .arg                   = NULL,
        .dispatch_method       = ESP_TIMER_ISR,
        .name                  = "fsm_tick",
        .skip_unhandled_events = true,
    };

    ESP_ERROR_CHECK(esp_timer_create(&args, &tick_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer_, period_us));
}

void fsm_tick_deinit(void)
{
    if (tick_timer_ != NULL) {
        ESP_ERROR_CHECK(esp_timer_stop(tick_timer_));
        ESP_ERROR_CHECK(esp_timer_delete(tick_timer_));
        tick_timer_ = NULL;
    }
}

/* ── Constructor ──────────────────────────────────────────────────────────── */

void fsm_time_event_ctor(fsm_time_event *me, fsm *owner, uint8_t signal)
{
    FSM_ASSERT(me);
    FSM_ASSERT(owner);
    me->super.signal  = signal;
    me->state_machine = owner;
    me->next          = NULL;
    me->down_counter  = 0;
    me->interval      = 0;
}

/* ── Arm ──────────────────────────────────────────────────────────────────── */

void fsm_time_event_arm(fsm_time_event *me, uint64_t nTicks, uint64_t interval)
{
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

/* ── Rearm ────────────────────────────────────────────────────────────────── */

bool fsm_time_event_rearm(fsm_time_event *me, uint64_t nTicks)
{
    FSM_ASSERT(nTicks > 0U);

    FSM_DISABLE_INTERRUPT;

    bool was_armed = false;
    for (fsm_time_event *curr = time_event_list_head; curr != NULL; curr = curr->next) {
        if (curr == me) { was_armed = true; break; }
    }

    me->down_counter = nTicks;

    if (!was_armed) {
        me->next             = time_event_list_head;
        time_event_list_head = me;
    }

    FSM_ENABLE_INTERRUPT;
    return was_armed;
}

/* ── Disarm ───────────────────────────────────────────────────────────────── */

void fsm_time_event_disarm(fsm_time_event *me)
{
    FSM_DISABLE_INTERRUPT;

    fsm_time_event **curr = &time_event_list_head;
    while (*curr != NULL) {
        if (*curr == me) {
            *curr    = me->next;
            me->next = NULL;
            break;
        }
        curr = &(*curr)->next;
    }

    FSM_ENABLE_INTERRUPT;
}

/* ── Tick ─────────────────────────────────────────────────────────────────── */

void fsm_tick(void)
{
    fsm_time_event *expired[MAX_FSM_TIMERS];
    int n_expired = 0;

    /* Phase 1: under ISR lock — collect expired timers. */
    FSM_DISABLE_INTERRUPT_ISR;

    fsm_time_event *curr = time_event_list_head;
    fsm_time_event *prev = NULL;

    while (curr != NULL) {
        fsm_time_event *next = curr->next;
        if (--curr->down_counter == 0U) {
            if (curr->interval != 0U) {
                curr->down_counter = curr->interval; /* periodic: reload and stay in list */
                prev = curr;
            } else {
                /* one-shot: unlink */
                if (prev == NULL) time_event_list_head = next;
                else              prev->next = next;
                curr->next = NULL;
            }
            FSM_ASSERT(n_expired < MAX_FSM_TIMERS);
            expired[n_expired++] = curr;
        } else {
            prev = curr;
        }
        curr = next;
    }

    FSM_ENABLE_INTERRUPT_ISR;

    /* Phase 2: post events with no lock held. */
    for (int i = 0; i < n_expired; i++) {
        fsm_post_from_isr((fsm *)expired[i]->state_machine, (fsm_event *)expired[i]);
    }
}
