#ifndef __LIGHTBAR_CONTROLLER_H__
#define __LIGHTBAR_CONTROLLER_H__

#include "fsm.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "nvs.h"
#include <stdint.h>
#include <stdbool.h>

// ─── ESP-NOW packet types (shared with table_controller) ─────────────────────
typedef enum __attribute__((packed)) {
    PKT_BRIGHTNESS_EVENT = 0x01,
    PKT_PRESET_EVENT     = 0x02,
    PKT_KNOB_BUTTON      = 0x03,
    PKT_HEARTBEAT        = 0x04
} pkt_type_t;

typedef struct __attribute__((packed)) {
    pkt_type_t type;
    uint8_t    seq;
    union {
        int16_t knob_delta;
        uint8_t knob_button_state;
    };
} app_pkt_t;

// ─── LED configuration ────────────────────────────────────────────────────────
#define LIGHTBAR_NUM_LEDS   76

// ─── Color temperature range and presets (Kelvin) ────────────────────────────
#define CCT_MIN      1500.0f
#define CCT_MAX      7500.0f
#define CCT_WARM     2700.0f    // preset 0
#define CCT_NEUTRAL  4000.0f    // preset 1
#define CCT_COOL     6500.0f    // preset 2

// ─── Animation ───────────────────────────────────────────────────────────────
// NOTE: FreeRTOS default tick rate is 100 Hz → pdMS_TO_TICKS(20) = 2 ticks = 20 ms.
// For tighter 60 Hz, set CONFIG_FREERTOS_HZ=1000 in sdkconfig.
#define ANIM_TICK_MS        20          // ~50 Hz, safe with default 100 Hz FreeRTOS
#define ANIM_ALPHA          0.06f       // exponential smoothing; ~1.5 s settle at 50 Hz
#define ANIM_BRT_EPSILON    0.002f      // brightness snap threshold (< 0.2% of full scale)
#define ANIM_CCT_EPSILON    1.0f        // CCT snap threshold (1 K)

// ─── Remote scaling ───────────────────────────────────────────────────────────
#define BRIGHTNESS_DELTA_PER_UNIT   0.01f   // 1 % of full scale per encoder click
#define CCT_DELTA_PER_UNIT          50.0f   // 50 K per encoder click

// ─── FSM queue depth ─────────────────────────────────────────────────────────
#define LIGHTBAR_QUEUE_DEPTH  50

// ─── Timer channels ──────────────────────────────────────────────────────────
#define TIMER_CH_ANIM   TIMER_CHANNEL_0   // 50 Hz animation tick

// ─── Signals ─────────────────────────────────────────────────────────────────
enum lightbar_signal : uint8_t {
    // Power toggle: local power button OR remote PKT_KNOB_BUTTON
    SIG_POWER      = SIG_USER_CODE,
    // CCT change: delta==0 → local preset button (cycle), delta!=0 → remote knob
    SIG_PRESET,
    // Brightness delta from remote (PKT_BRIGHTNESS_EVENT)
    SIG_BRIGHTNESS,
    // 50 Hz tick from FreeRTOS timer → drives smooth interpolation
    SIG_ANIM_TICK,
    SIG_MAX
};

// ─── Extended event ───────────────────────────────────────────────────────────
// Payload for SIG_BRIGHTNESS and SIG_PRESET (remote): signed encoder delta.
// Must fit within FSM_MAX_EVENT_SIZE (currently 8 bytes).
typedef struct __attribute__((packed)) {
    fsm_event super;    // MUST be first member; contains uint8_t signal
    int16_t   delta;    // encoder delta; 0 for button-press signals
} lightbar_event;

// ─── Forward declaration ─────────────────────────────────────────────────────
typedef struct LIGHTBAR_CONTROLLER lightbar_controller;

// ─── Controller struct ────────────────────────────────────────────────────────
struct LIGHTBAR_CONTROLLER {
    fsm super;           // MUST be first — cast-compatible with fsm *

    struct {
        gpio_num_t power_btn_pin;
        gpio_num_t preset_btn_pin;
        gpio_num_t led_data_pin;
    } pin;

    led_strip_handle_t strip;

    // Smooth animation: current values track target via exponential smoothing
    float current_brightness;   // 0.0 – 1.0  (what is shown right now)
    float target_brightness;    // 0.0 – 1.0  (where we are heading)
    float current_cct;          // CCT_MIN – CCT_MAX K
    float target_cct;

    // Set true when powering off so animation drives to off_state on completion
    bool turning_off;

    // NVS handle (kept open for the lifetime of the controller)
    nvs_handle_t nvs;

    // Precomputed gamma-2.2 lookup table (index = linear, value = gamma-corrected)
    uint8_t gamma_lut[256];
};

// ─── Public API ──────────────────────────────────────────────────────────────

// Call before lightbar_init. Initialises NVS, LED strip, loads persisted state.
void lightbar_ctor(lightbar_controller *me,
                   gpio_num_t power_pin,
                   gpio_num_t preset_pin,
                   gpio_num_t led_pin);

// Call after GPIO ISRs are installed. Starts ESP-NOW and the FSM task.
void lightbar_init(lightbar_controller *me, const char *task_name);

// Call from GPIO ISRs — debounce handled internally (20 ms ISR timestamp).
void IRAM_ATTR lightbar_post_power_isr(lightbar_controller *me);
void IRAM_ATTR lightbar_post_preset_isr(lightbar_controller *me);

// Call from the ESP-NOW receive callback (task context, not ISR).
void lightbar_post_espnow_pkt(lightbar_controller *me, const app_pkt_t *pkt);

// ─── FSM state functions ──────────────────────────────────────────────────────
fsm_state lightbar_entry_state      (fsm *me, fsm_event *e);
fsm_state lightbar_off_state        (fsm *me, fsm_event *e);
fsm_state lightbar_on_state         (fsm *me, fsm_event *e);   // parent — never set as me->state
fsm_state lightbar_brightness_state (fsm *me, fsm_event *e);
fsm_state lightbar_preset_state     (fsm *me, fsm_event *e);

#endif // __LIGHTBAR_CONTROLLER_H__
