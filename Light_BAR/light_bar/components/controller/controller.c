#include "controller.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>
#include "esp_wifi.h"

static const char *TAG = "lightbar";

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static const float k_presets[3] = { CCT_WARM, CCT_NEUTRAL, CCT_COOL };

// ─────────────────────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────────────────────

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gamma-2.2 LUT — computed once at construction time
// ─────────────────────────────────────────────────────────────────────────────

static void init_gamma_lut(uint8_t *lut) {
    for (int i = 0; i < 256; i++) {
        lut[i] = (uint8_t)(255.0f * powf(i / 255.0f, 2.2f) + 0.5f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tanner Helland CCT → linear RGB
// Maps a correlated colour temperature in Kelvin to an sRGB triplet (0–255).
// Source: https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html
// ─────────────────────────────────────────────────────────────────────────────

static void cct_to_rgb(float cct_k, uint8_t *r, uint8_t *g, uint8_t *b) {
    float temp = cct_k / 100.0f;
    float red, green, blue;

    // Red channel
    if (temp <= 66.0f) {
        red = 255.0f;
    } else {
        red = 329.698727446f * powf(temp - 60.0f, -0.1332047592f);
        red = clampf(red, 0.0f, 255.0f);
    }

    // Green channel
    if (temp <= 66.0f) {
        green = 99.4708025861f * logf(temp) - 161.1195681661f;
    } else {
        green = 288.1221695283f * powf(temp - 60.0f, -0.0755148492f);
    }
    green = clampf(green, 0.0f, 255.0f);

    // Blue channel
    if (temp >= 66.0f) {
        blue = 255.0f;
    } else if (temp <= 19.0f) {
        blue = 0.0f;
    } else {
        blue = 138.5177312231f * logf(temp - 10.0f) - 305.0447927307f;
        blue = clampf(blue, 0.0f, 255.0f);
    }

    *r = (uint8_t)red;
    *g = (uint8_t)green;
    *b = (uint8_t)blue;
}

// ─────────────────────────────────────────────────────────────────────────────
// Strip refresh
// Converts current CCT + brightness → gamma-corrected RGB → writes all LEDs.
// ─────────────────────────────────────────────────────────────────────────────

static void refresh_strip(lightbar_controller *me) {
    uint8_t r, g, b;
    cct_to_rgb(me->current_cct, &r, &g, &b);

    // Apply brightness in linear space first, then gamma-correct
    float brt = me->current_brightness;
    r = me->gamma_lut[(uint8_t)(r * brt)];
    g = me->gamma_lut[(uint8_t)(g * brt)];
    b = me->gamma_lut[(uint8_t)(b * brt)];

    for (int i = 0; i < LIGHTBAR_NUM_LEDS; i++) {
        led_strip_set_pixel(me->strip, i, r, g, b);
    }
    led_strip_refresh(me->strip);
}

// ─────────────────────────────────────────────────────────────────────────────
// Animation step — called every ANIM_TICK_MS from the FSM timer tick.
// Exponential smoothing toward targets; snaps when within epsilon.
// Returns true if the strip was updated (caller may use this to detect idle).
// ─────────────────────────────────────────────────────────────────────────────

static bool step_animation(lightbar_controller *me) {
    bool changed = false;

    // Brightness
    float db = me->target_brightness - me->current_brightness;
    if (fabsf(db) < ANIM_BRT_EPSILON) {
        if (me->current_brightness != me->target_brightness) {
            me->current_brightness = me->target_brightness;
            changed = true;
        }
    } else {
        me->current_brightness += db * ANIM_ALPHA;
        changed = true;
    }

    // CCT
    float dc = me->target_cct - me->current_cct;
    if (fabsf(dc) < ANIM_CCT_EPSILON) {
        if (me->current_cct != me->target_cct) {
            me->current_cct = me->target_cct;
            changed = true;
        }
    } else {
        me->current_cct += dc * ANIM_ALPHA;
        changed = true;
    }

    if (changed) {
        refresh_strip(me);
    }
    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Preset cycling — proximity-based: advance to the preset after the closest one
// ─────────────────────────────────────────────────────────────────────────────

static void cycle_preset(lightbar_controller *me) {
    int closest = 0;
    float min_dist = fabsf(me->target_cct - k_presets[0]);
    for (int i = 1; i < 3; i++) {
        float d = fabsf(me->target_cct - k_presets[i]);
        if (d < min_dist) { min_dist = d; closest = i; }
    }
    me->target_cct = k_presets[(closest + 1) % 3];
    ESP_LOGI(TAG, "preset → %.0f K", (double)me->target_cct);
}

// ─────────────────────────────────────────────────────────────────────────────
// NVS helpers
// Brightness and CCT are stored as raw uint32 bit-patterns of float values.
// ─────────────────────────────────────────────────────────────────────────────

static void nvs_load(lightbar_controller *me) {
    uint32_t brt_bits = 0, cct_bits = 0;
    float brt = 0.8f, cct = CCT_NEUTRAL;   // defaults on first boot

    if (nvs_get_u32(me->nvs, "brightness", &brt_bits) == ESP_OK)
        memcpy(&brt, &brt_bits, sizeof(float));
    if (nvs_get_u32(me->nvs, "cct", &cct_bits) == ESP_OK)
        memcpy(&cct, &cct_bits, sizeof(float));

    // target loads saved value; current starts at 0 so power-on fades in
    me->target_brightness = clampf(brt, 0.05f, 1.0f);
    me->current_brightness = 0.0f;
    me->target_cct  = clampf(cct, CCT_MIN, CCT_MAX);
    me->current_cct = me->target_cct;   // CCT is restored instantly

    ESP_LOGI(TAG, "NVS loaded: brightness=%.2f  cct=%.0fK",
             (double)brt, (double)cct);
}

// Save the provided brightness and CCT (call with target values before zeroing them).
static void nvs_save(lightbar_controller *me, float brt, float cct) {
    uint32_t brt_bits, cct_bits;
    memcpy(&brt_bits, &brt, sizeof(float));
    memcpy(&cct_bits, &cct, sizeof(float));
    nvs_set_u32(me->nvs, "brightness", brt_bits);
    nvs_set_u32(me->nvs, "cct",        cct_bits);
    nvs_commit(me->nvs);
}

// ─────────────────────────────────────────────────────────────────────────────
// ESP-NOW receive callback — runs in its own task
// ─────────────────────────────────────────────────────────────────────────────

static lightbar_controller *s_lb = NULL;

static void espnow_rx_cb(const esp_now_recv_info_t *info,
                         const uint8_t *data, int len)
{
    if (!s_lb || len < (int)sizeof(app_pkt_t)) return;
    lightbar_post_espnow_pkt(s_lb, (const app_pkt_t *)data);
}

static void espnow_receiver_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_rx_cb));
    ESP_LOGI(TAG, "ESP-NOW receiver ready");
}

// ─────────────────────────────────────────────────────────────────────────────
// LED strip initialisation (WS2812 via SPI)
// ─────────────────────────────────────────────────────────────────────────────

static void led_strip_setup(lightbar_controller *me) {
    led_strip_config_t strip_cfg = {
        .strip_gpio_num         = me->pin.led_data_pin,
        .max_leds               = LIGHTBAR_NUM_LEDS,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags                  = { .invert_out = 0 },
    };
    led_strip_spi_config_t spi_cfg = {
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = SPI2_HOST,
        .flags   = { .with_dma = 1 },
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_cfg, &spi_cfg, &me->strip));
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void lightbar_ctor(lightbar_controller *me,
                   gpio_num_t power_pin,
                   gpio_num_t preset_pin,
                   gpio_num_t led_pin)
{
    // NVS flash — call before nvs_open
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(nvs_open("lightbar", NVS_READWRITE, &me->nvs));

    // Pin config
    me->pin.power_btn_pin  = power_pin;
    me->pin.preset_btn_pin = preset_pin;
    me->pin.led_data_pin   = led_pin;

    me->turning_off = false;

    // LED strip
    led_strip_setup(me);

    // Gamma LUT (one-time floating-point pass at boot)
    init_gamma_lut(me->gamma_lut);

    // Restore brightness + CCT from NVS
    nvs_load(me);

    // FSM queue sized for the extended event (signal + int16 delta)
    fsm_ctor((fsm *)me, LIGHTBAR_QUEUE_DEPTH, sizeof(lightbar_event));

    // Store global for ESP-NOW callback
    s_lb = me;
}

void lightbar_init(lightbar_controller *me, const char *task_name) {
    espnow_receiver_init();
    fsm_init((fsm *)me, task_name, lightbar_entry_state);
}

// ─────────────────────────────────────────────────────────────────────────────
// ISR-facing post helpers — ISR timestamp debounce (20 ms)
// ─────────────────────────────────────────────────────────────────────────────

void IRAM_ATTR lightbar_post_power_isr(lightbar_controller *me) {
    static int64_t last_us = 0;
    int64_t now = esp_timer_get_time();
    if ((now - last_us) < 20000) return;    // 20 ms debounce window
    last_us = now;

    lightbar_event e = {};
    e.super.signal = SIG_POWER;
    e.delta = 0;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(((fsm *)me)->queue_, &e, &woken);
    portYIELD_FROM_ISR(woken);
}

void IRAM_ATTR lightbar_post_preset_isr(lightbar_controller *me) {
    static int64_t last_us = 0;
    int64_t now = esp_timer_get_time();
    if ((now - last_us) < 20000) return;
    last_us = now;

    lightbar_event e = {};
    e.super.signal = SIG_PRESET;
    e.delta = 0;    // delta==0 flags this as a local button press
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(((fsm *)me)->queue_, &e, &woken);
    portYIELD_FROM_ISR(woken);
}

void lightbar_post_espnow_pkt(lightbar_controller *me, const app_pkt_t *pkt) {
    lightbar_event e = {};

    switch (pkt->type) {
        case PKT_BRIGHTNESS_EVENT:
            e.super.signal = SIG_BRIGHTNESS;
            e.delta = pkt->knob_delta;
            break;
        case PKT_PRESET_EVENT:
            e.super.signal = SIG_PRESET;
            e.delta = pkt->knob_delta;   // non-zero → remote CCT control
            break;
        case PKT_KNOB_BUTTON:
            e.super.signal = SIG_POWER;
            e.delta = 0;
            break;
        case PKT_HEARTBEAT:
        default:
            return;
    }

    fsm_post((fsm *)me, (fsm_event *)&e);
}

// ─────────────────────────────────────────────────────────────────────────────
// FSM States
//
// Hierarchy (flat-HSM, manual parent delegation):
//
//   lightbar_entry_state  ─── initial pseudo-state
//   lightbar_off_state    ─── LED dark
//   lightbar_on_state     ─── PARENT (not set as me->state; called via delegation)
//     lightbar_brightness_state  ─── default on-sub-state
//     lightbar_preset_state      ─── remote CCT control active
//
// Sub-states call lightbar_on_state(me, e) in their default branch to
// implement parent-state delegation (same pattern as table_controller).
// ─────────────────────────────────────────────────────────────────────────────

// 50 Hz animation tick event — static so its address is stable for fsm_timer_arm
static const lightbar_event k_tick_evt = { .super = {SIG_ANIM_TICK}, .delta = 0 };

// ─── entry_state ─────────────────────────────────────────────────────────────

fsm_state lightbar_entry_state(fsm *me, fsm_event *e) {
    // Only TRAN on SIG_INIT (or SIG_ENTRY); return handled for SIG_EXIT so
    // the dispatcher's exit synthesis does not re-trigger the transition.
    if (e->signal != SIG_EXIT) {
        return TRAN(lightbar_off_state);
    }
    return STATE_HANDLED;
}

// ─── off_state ───────────────────────────────────────────────────────────────

fsm_state lightbar_off_state(fsm *me, fsm_event *e) {
    lightbar_controller *controller = (lightbar_controller *)me;

    switch (e->signal) {
        case SIG_ENTRY:
            fsm_timer_disarm(me, TIMER_CH_ANIM);
            led_strip_clear(controller->strip);
            led_strip_refresh(controller->strip);
            ESP_LOGI(TAG, "[off]");
            return STATE_HANDLED;

        case SIG_EXIT:
            return STATE_HANDLED;

        case SIG_POWER:
            // Restore brightness from zero (fade-in) — target was saved before power-off
            controller->current_brightness = 0.0f;
            return TRAN(lightbar_brightness_state);

        default:
            return STATE_IGNORED;
    }
}

// ─── on_state (PARENT — never assigned to me->state directly) ────────────────
// Sub-states delegate to this function in their default: branch.

fsm_state lightbar_on_state(fsm *me, fsm_event *e) {
    lightbar_controller *controller = (lightbar_controller *)me;

    switch (e->signal) {

        case SIG_POWER:
            // Save current targets before starting the fade-out.
            nvs_save(controller, controller->target_brightness, controller->target_cct);
            controller->target_brightness = 0.0f;
            controller->turning_off = true;
            return STATE_HANDLED;   // stay in current sub-state; fade drives TRAN

        case SIG_ANIM_TICK:
            step_animation(controller);
            if (controller->turning_off && controller->current_brightness <= ANIM_BRT_EPSILON) {
                controller->current_brightness = controller->target_brightness = 0.0f;
                controller->turning_off = false;
                return TRAN(lightbar_off_state);   // animation complete → power off
            }
            return STATE_HANDLED;

        case SIG_PRESET: {
            lightbar_event *le = (lightbar_event *)e;
            if (le->delta == 0) {
                // Local preset button — cycle through presets
                cycle_preset(controller);
                return STATE_HANDLED;
            }
            return STATE_IGNORED;   // remote delta handled in sub-states
        }

        default:
            return STATE_IGNORED;
    }
}

// ─── brightness_state ────────────────────────────────────────────────────────

fsm_state lightbar_brightness_state(fsm *me, fsm_event *e) {
    lightbar_controller *controller = (lightbar_controller *)me;

    switch (e->signal) {
        case SIG_ENTRY:
            fsm_timer_arm(me, (fsm_event *)&k_tick_evt,
                          TIMER_CH_ANIM, pdMS_TO_TICKS(ANIM_TICK_MS), 0);
            refresh_strip(controller);
            ESP_LOGI(TAG, "[brightness]  brt=%.2f  cct=%.0fK",
                     (double)controller->target_brightness, (double)controller->target_cct);
            return STATE_HANDLED;

        case SIG_EXIT:
            // Timer keeps running when transitioning to preset_state.
            // It is disarmed in off_state SIG_ENTRY.
            return STATE_HANDLED;

        case SIG_BRIGHTNESS: {
            lightbar_event *le = (lightbar_event *)e;
            controller->target_brightness = clampf(
                controller->target_brightness + le->delta * BRIGHTNESS_DELTA_PER_UNIT,
                0.0f, 1.0f);
            return STATE_HANDLED;
        }

        case SIG_PRESET: {
            lightbar_event *le = (lightbar_event *)e;
            if (le->delta != 0) {
                // Remote CCT knob → accumulate and switch to preset_state
                controller->target_cct = clampf(
                    controller->target_cct + le->delta * CCT_DELTA_PER_UNIT,
                    CCT_MIN, CCT_MAX);
                return TRAN(lightbar_preset_state);
            }
            // delta==0 (local button) → delegate to parent
            return lightbar_on_state(me, e);
        }

        default:
            return lightbar_on_state(me, e);
    }
}

// ─── preset_state ────────────────────────────────────────────────────────────

fsm_state lightbar_preset_state(fsm *me, fsm_event *e) {
    lightbar_controller *controller = (lightbar_controller *)me;

    switch (e->signal) {
        case SIG_ENTRY:
            // Re-arm in case timer was not yet running (e.g. first transition)
            fsm_timer_arm(me, (fsm_event *)&k_tick_evt,
                          TIMER_CH_ANIM, pdMS_TO_TICKS(ANIM_TICK_MS), 0);
            ESP_LOGI(TAG, "[preset]  cct=%.0fK", (double)controller->target_cct);
            return STATE_HANDLED;

        case SIG_EXIT:
            return STATE_HANDLED;

        case SIG_PRESET: {
            lightbar_event *le = (lightbar_event *)e;
            if (le->delta != 0) {
                // Remote knob — keep adjusting CCT
                controller->target_cct = clampf(
                    controller->target_cct + le->delta * CCT_DELTA_PER_UNIT,
                    CCT_MIN, CCT_MAX);
                return STATE_HANDLED;
            }
            // Local button → delegate to parent (cycle_preset)
            return lightbar_on_state(me, e);
        }

        case SIG_BRIGHTNESS: {
            // Remote returned to brightness mode — apply delta and switch back
            lightbar_event *le = (lightbar_event *)e;
            controller->target_brightness = clampf(
                controller->target_brightness + le->delta * BRIGHTNESS_DELTA_PER_UNIT,
                0.0f, 1.0f);
            return TRAN(lightbar_brightness_state);
        }

        default:
            return lightbar_on_state(me, e);
    }
}
