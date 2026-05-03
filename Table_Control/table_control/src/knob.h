/**
 * @file knob.h
 * @brief Quadrature rotary encoder driver using a Gray-code lookup table.
 *
 * Decodes a standard two-channel (A/B) incremental encoder by sampling GPIO
 * levels and indexing a 4×4 state-transition table.  The design counts only
 * on arrival at the detent position (S3: A=1, B=1) so that exactly one unit
 * is accumulated per physical click, regardless of polling rate.
 *
 * Illegal transitions (caused by contact bounce or missed edges) are
 * represented by the sentinel value 99 in ::knob_lookup and are silently
 * discarded without corrupting the accumulated delta.
 *
 * All functions are @c static @c inline and defined here in the header;
 * the matching knob.c is intentionally empty.
 */

#ifndef __KNOB_H__
#define __KNOB_H__

#include "stdint.h"
#include "driver/gpio.h"


/* ── Encoder state machine ────────────────────────────────────────────────── */

/**
 * @brief Gray-code quadrature states derived from the two encoder pins.
 *
 * The state is encoded as @c (A << 1) | B where A is the CLK pin and B is
 * the DATA pin.
 */
typedef enum {
    ENC_S0 = 0, /**< A=0 B=0 */
    ENC_S1 = 1, /**< A=0 B=1 */
    ENC_S2 = 2, /**< A=1 B=0 */
    ENC_S3 = 3, /**< A=1 B=1 – detent position; counts are registered here. */
} enc_state_t;

/**
 * @brief State-transition lookup table [previous_state][current_state] → delta.
 *
 * Values:
 *  -  0  : no movement or intermediate transition – do not count.
 *  - +1  : one CW detent reached.
 *  - -1  : one CCW detent reached.
 *  - 99  : illegal / skipped transition (bounce) – discard.
 *
 * Only transitions that arrive at S3 (the detent) produce a non-zero count,
 * ensuring exactly 1 count per click.
 */
static const int8_t knob_lookup[4][4] = {
//  curr:  S0   S1   S2   S3
/* S0 */  { 0,   0,   0,  99 },
/* S1 */  { 0,   0,  99,  -1 },
/* S2 */  { 0,  99,   0,  +1 },
/* S3 */  {99,   0,   0,   0 },
};


/* ── Handle struct ────────────────────────────────────────────────────────── */

/**
 * @brief Runtime state for a single encoder instance.
 */
typedef struct {
    enc_state_t state;       /**< Last decoded Gray-code state.                  */
    int32_t     delta;       /**< Accumulated signed delta since last read.      */
    uint32_t    error_count; /**< Number of illegal transitions detected.        */
    gpio_num_t  pinA;        /**< GPIO for encoder channel A (CLK).              */
    gpio_num_t  pinB;        /**< GPIO for encoder channel B (DATA).             */
} encoder_handle_t;


/* ── Inline API ───────────────────────────────────────────────────────────── */

/**
 * @brief Bind an encoder handle to its GPIO pins.
 *
 * Does not configure the GPIO hardware; the caller must set both pins as
 * inputs (with pull-ups if required) before calling ::encoder_fsm_init.
 *
 * @param me      Encoder handle to initialise.
 * @param clk_pin GPIO number for the CLK (channel A) pin.
 * @param dat_pin GPIO number for the DATA (channel B) pin.
 */
static inline void encoder_fsm_ctor(encoder_handle_t *me, gpio_num_t clk_pin, gpio_num_t dat_pin){
    me->pinA = clk_pin;
    me->pinB = dat_pin;
}

/**
 * @brief Read the current GPIO levels and set the initial encoder state.
 *
 * Must be called after the GPIO pins are configured and before the first
 * ::encoder_handle_tick call.  Zeros the accumulated delta and error count.
 *
 * @param me Encoder handle (constructed with ::encoder_fsm_ctor).
 */
static inline void encoder_fsm_init(encoder_handle_t *me) {
    uint8_t a = gpio_get_level(me->pinA);
    uint8_t b = gpio_get_level(me->pinB);
    me->state       = (enc_state_t)((a << 1) | b);
    me->delta       = 0;
    me->error_count = 0;
}

/**
 * @brief Sample the encoder pins and update the accumulated delta.
 *
 * Call periodically from a FreeRTOS timer callback.  Each call reads the
 * current A/B levels, looks up the transition in ::knob_lookup, and adds
 * the result to @c me->delta.  Illegal transitions (99) are discarded.
 *
 * @param me Encoder handle.
 */
static inline void encoder_handle_tick(encoder_handle_t *me) {
    uint8_t a = gpio_get_level(me->pinA);
    uint8_t b = gpio_get_level(me->pinB);
    enc_state_t next = (enc_state_t)((a << 1) | b);

    int8_t step = knob_lookup[me->state][next];

    if (step == 99) {
        return;  /* illegal transition – discard */
    }

    me->delta += step;
    me->state  = next;
}

/**
 * @brief Return and atomically clear the accumulated encoder delta.
 *
 * Call from the flush timer callback to retrieve the net displacement since
 * the last read.  Returns 0 if there has been no movement.
 *
 * @param me Encoder handle.
 * @return Signed delta since last call (positive = CW, negative = CCW).
 */
static inline int32_t encoder_read_and_clear(encoder_handle_t *me) {
    int32_t d = me->delta;
    me->delta = 0;
    return d;
}

#endif
