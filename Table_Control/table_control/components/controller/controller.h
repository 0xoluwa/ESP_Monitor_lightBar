/**
 * @file controller.h
 * @brief Device controller FSM – public interface.
 *
 * The controller is a two-level hierarchical FSM that manages the full
 * lifecycle of the table-side remote:
 *
 *  - **Rotary encoder**: brightness and colour-temperature adjustments
 *    transmitted over ESP-NOW.
 *  - **Short press**: toggle knob mode (BRIGHTNESS ↔ COLOR_TEMP).
 *  - **Long press**: send a power-toggle packet to the light bar.
 *  - **RGB LED feedback**: blue = connected/active, red = TX failure.
 *  - **Idle timer**: 30-second inactivity window → deep sleep.
 *
 * State hierarchy:
 * @code
 *   entry_handler   (initial pseudo-state; selects tx_state on SIG_INIT)
 *   └── awake_state (super-state; handles SLEEP_SIG → sleeping_state)
 *       └── tx_state (active TX + blue LED + idle timer)
 *   sleeping_state  (deep sleep entry; entered from awake_state)
 * @endcode
 */

#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include "fsm.h"
#include "timer_evt.h"
#include "driver/gpio.h"


/* ── Configuration ────────────────────────────────────────────────────────── */

/** @brief Depth of the controller FSM event queue (number of events). */
#define QUEUE_DEPTH         20

/**
 * @brief Inactivity timeout in FSM ticks before entering deep sleep.
 *
 * At the default 1 ms tick resolution this equals 30 seconds.
 */
#define IDLE_TIMEOUT_TICKS  30000U


/* ── Enumerations ─────────────────────────────────────────────────────────── */

/**
 * @brief Classified duration of a knob button press.
 */
typedef enum {
    SHORT_PRESS, /**< Press between ::SHORT_PRESS_US and ::LONG_PRESS_US.  */
    LONG_PRESS   /**< Press >= ::LONG_PRESS_US; triggers a power packet.   */
} button_duration;

/**
 * @brief RGB LED channel selector used by power_led().
 */
typedef enum {
    RED_LED,   /**< Red channel   (LEDC channel 0, ::LED_RED_PIN).   */
    BLUE_LED,  /**< Blue channel  (LEDC channel 2, ::LED_BLUE_PIN).  */
    GREEN_LED  /**< Green channel (LEDC channel 1, ::LED_GREEN_PIN). */
} active_led;

/**
 * @brief LED power operation passed to power_led().
 */
typedef enum{
    POWER_DOWN, /**< Fade the LED to 0 % duty.             */
    POWER_ON    /**< Fade the LED to ::LED_PWM_DUTY duty.  */
} led_operation;

/**
 * @brief Knob transmission mode toggled by a short button press.
 */
typedef enum{
    BRIGHTNESS = 0, /**< Encoder delta → ::PKT_BRIGHTNESS_EVENT packet. */
    COLOR_TEMP = 1  /**< Encoder delta → ::PKT_PRESET_EVENT packet.     */
} knob_transmit_signal_t;


/* ── Event structure ──────────────────────────────────────────────────────── */

/**
 * @brief Concrete event type for the controller FSM.
 *
 * Inherits ::fsm_event (super must be first) and carries one of two
 * payloads depending on the signal:
 *  - @c knob_count            – used with ::SIG_KNOB
 *  - @c knob_button_duration  – used with ::SIG_KNOB_BTN_PRESS
 */
typedef struct controller_evt {
    fsm_event super;           /**< Base event; signal field selects the payload. */
    union {
        int             knob_count;            /**< Net encoder delta (positive = CW). */
        button_duration knob_button_duration;  /**< Classified press duration.         */
    };
} controller_event;

/**
 * @brief Application-level signals for the controller FSM.
 *
 * All values start at ::SIG_USER_CODE to avoid collision with the
 * framework-reserved signals (SIG_ENTRY, SIG_EXIT, SIG_INIT).
 */
enum controller_signal {
    SIG_KNOB_BTN_PRESS = SIG_USER_CODE, /**< Debounced encoder button press (from ISR).  */
    SIG_KNOB,                           /**< Non-zero encoder delta accumulated over the flush window. */
    SLEEP_SIG,                          /**< Idle timeout expired; transition to deep sleep.           */
    CONNECTED_SIG,                      /**< ESP-NOW send ACK received from the light bar.             */
    DISCONNECTED_SIG,                   /**< ESP-NOW send failure (no ACK from light bar).             */
    SIG_MAX                             /**< Sentinel – not a valid signal value.                      */
};


/* ── Controller struct ────────────────────────────────────────────────────── */

typedef struct CONTROLLER controller;

/**
 * @brief Controller FSM control block.
 *
 * Embeds ::fsm as its first member so it can be cast to @c fsm* and passed
 * to any FSM framework function.
 */
struct CONTROLLER{
    fsm             super;                  /**< Base FSM; must be first.                        */
    fsm_time_event  idle_timer;             /**< Inactivity timer; fires ::SLEEP_SIG after 30 s. */
    knob_transmit_signal_t knob_button_press_state_; /**< Current knob transmission mode.        */
};


/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Construct the controller FSM.
 *
 * Allocates the event queue, constructs the idle timer, and sets the
 * default knob mode to ::BRIGHTNESS.  Must be called before
 * ::controller_init.
 *
 * @param me Controller instance to construct.
 */
void controller_ctor(controller *me);

/**
 * @brief Initialise hardware and start the controller FSM task.
 *
 * Configures LEDC for the RGB LED, initialises the ESP-NOW link, and
 * launches the FSM dispatch task.
 *
 * @param me              Controller instance (already constructed).
 * @param controller_name FreeRTOS task name for the FSM dispatch task.
 */
void controller_init(controller *me, const char *controller_name);

/**
 * @brief Post a knob delta event to the controller from task context.
 *
 * @param me         Controller instance.
 * @param knob_count Signed encoder delta (positive = CW, negative = CCW).
 */
void post_knob_count(controller *me, int knob_count);

/**
 * @brief Post a button-press event to the controller from an ISR.
 *
 * ISR-safe; uses ::fsm_post_from_isr internally.
 *
 * @param me             Controller instance.
 * @param press_duration Classified press length (::SHORT_PRESS or ::LONG_PRESS).
 */
void IRAM_ATTR post_knob_button(controller *me, button_duration press_duration);

#endif
