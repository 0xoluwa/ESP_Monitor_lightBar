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

// Preset CCT frame indices (0–99).  Derived from Kelvin values:
//   warm  2700 K → frame 24,  neutral 4000 K → frame 50,  cool 6500 K → frame 99
// Formula: round((K - CCT_MIN) * (CCT_FRAMES-1) / (CCT_MAX - CCT_MIN))
static const int16_t k_preset_frames[3] = { 24, 50, 99 };

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
    // Convert frame units → physical values
    float brt   = me->brt_anim / (float)(BRT_FRAMES - 1);
    float cct_k = CCT_MIN + me->cct_anim * ((CCT_MAX - CCT_MIN) / (float)(CCT_FRAMES - 1));

    uint8_t r, g, b;
    cct_to_rgb(cct_k, &r, &g, &b);

    // Apply brightness in linear space first, then gamma-correct
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

    // Brightness — interpolate in frame units (0.0–99.0)
    float db = (float)me->brt_target - me->brt_anim;
    if (fabsf(db) < ANIM_EPSILON) {
        if (me->brt_anim != (float)me->brt_target) {
            me->brt_anim = (float)me->brt_target;
            changed = true;
        }
    } else {
        me->brt_anim += db * ANIM_ALPHA;
        changed = true;
    }

    // CCT — interpolate in frame units (0.0–99.0)
    float dc = (float)me->cct_target - me->cct_anim;
    if (fabsf(dc) < ANIM_EPSILON) {
        if (me->cct_anim != (float)me->cct_target) {
            me->cct_anim = (float)me->cct_target;
            changed = true;
        }
    } else {
        me->cct_anim += dc * ANIM_ALPHA;
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
    // Find the nearest preset frame
    int closest = 0;
    int min_dist = (int)fabsf((float)(me->cct_target - k_preset_frames[0]));
    for (int i = 1; i < 3; i++) {
        int d = (int)fabsf((float)(me->cct_target - k_preset_frames[i]));
        if (d < min_dist) { min_dist = d; closest = i; }
    }
    // If already at that preset → advance; otherwise snap to it first
    if (me->cct_target == k_preset_frames[closest]) {
        closest = (closest + 1) % 3;
    }
    me->cct_target = k_preset_frames[closest];
    ESP_LOGI(TAG, "preset → frame %d", me->cct_target);
}

// ─────────────────────────────────────────────────────────────────────────────
// NVS helpers — brightness and CCT stored as uint8_t frame indices (0–99)
// ─────────────────────────────────────────────────────────────────────────────

static void nvs_load(lightbar_controller *me) {
    uint8_t brt_idx = 79;   // default ~80% brightness (frame 79/99)
    uint8_t cct_idx = 50;   // default neutral CCT (frame 50 ≈ 4000 K)

    nvs_get_u8(me->nvs, "brt_idx", &brt_idx);
    nvs_get_u8(me->nvs, "cct_idx", &cct_idx);

    me->brt_target    = brt_idx < BRT_FRAMES ? brt_idx : 79;
    me->cct_target    = cct_idx < CCT_FRAMES ? cct_idx : 50;
    me->saved_brt_idx = me->brt_target;

    me->brt_anim = 0.0f;                   // fade in from dark on power-on
    me->cct_anim = (float)me->cct_target;  // CCT snaps to saved value instantly

    ESP_LOGI(TAG, "NVS loaded: brt_frame=%d  cct_frame=%d",
             me->brt_target, me->cct_target);
}

// Save the provided frame indices before zeroing them on power-off.
static void nvs_save(lightbar_controller *me, int16_t brt_idx, int16_t cct_idx) {
    nvs_set_u8(me->nvs, "brt_idx", (uint8_t)brt_idx);
    nvs_set_u8(me->nvs, "cct_idx", (uint8_t)cct_idx);
    nvs_commit(me->nvs);
}

// ─────────────────────────────────────────────────────────────────────────────
// ESP-NOW receive callback — runs in its own task
// ─────────────────────────────────────────────────────────────────────────────

static lightbar_controller *s_lb = NULL;

static void espnow_rx_cb(const esp_now_recv_info_t *info,
                         const uint8_t *data, int len)
{
    if (!s_lb) {
        ESP_LOGW(TAG, "rx: controller not ready, dropped");
        return;
    }
    if (len < (int)sizeof(app_pkt_t)) {
        ESP_LOGW(TAG, "rx: short packet (%d bytes), dropped", len);
        return;
    }
    const uint8_t *m = info->src_addr;
    ESP_LOGI(TAG, "rx: pkt from %02x:%02x:%02x:%02x:%02x:%02x  len=%d",
             m[0], m[1], m[2], m[3], m[4], m[5], len);
    lightbar_post_espnow_pkt(s_lb, (const app_pkt_t *)data);
}

#define ESPNOW_CHANNEL 1    /* must match table controller */

static void espnow_receiver_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // NOTE: do NOT call esp_netif_create_default_wifi_sta() — we are pure ESP-NOW,
    // no TCP/IP stack needed. Creating the netif triggers DHCP activity that can
    // interfere with manual channel locking.

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));   /* disable power-save — ESP-NOW needs radio always-on */

    // Verify the channel actually stuck
    uint8_t primary; wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    ESP_LOGI(TAG, "WiFi channel confirmed: %d (wanted %d)", primary, ESPNOW_CHANNEL);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_rx_cb));
    ESP_LOGI(TAG, "ESP-NOW receiver ready on channel %d", primary);
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
    ESP_LOGI(TAG, "lightbar constructed — brt=%d cct=%d",
             me->brt_target, me->cct_target);
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
            ESP_LOGI(TAG, "pkt BRIGHTNESS  delta=%d  seq=%u", pkt->knob_delta, pkt->seq);
            e.super.signal = SIG_BRIGHTNESS;
            e.delta = pkt->knob_delta;
            break;
        case PKT_PRESET_EVENT:
            ESP_LOGI(TAG, "pkt CCT         delta=%d  seq=%u", pkt->knob_delta, pkt->seq);
            e.super.signal = SIG_PRESET;
            e.delta = pkt->knob_delta;
            break;
        case PKT_KNOB_BUTTON:
            ESP_LOGI(TAG, "pkt POWER  seq=%u", pkt->seq);
            e.super.signal = SIG_POWER;
            e.delta = 0;
            break;
        case PKT_HEARTBEAT:
        default:
            ESP_LOGD(TAG, "pkt type=0x%02x ignored", pkt->type);
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
            // Restore saved brightness target; brt_anim is already ~0 from fade-out
            ESP_LOGI(TAG, "[off] power on → brt_target=%d", controller->saved_brt_idx);
            controller->brt_target = controller->saved_brt_idx;
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
            // Persist current targets before fade-out zeroes brt_target
            ESP_LOGI(TAG, "[on] power off → saving brt=%d cct=%d",
                     controller->brt_target, controller->cct_target);
            nvs_save(controller, controller->brt_target, controller->cct_target);
            controller->saved_brt_idx = controller->brt_target;
            controller->brt_target    = 0;
            controller->turning_off   = true;
            return STATE_HANDLED;   // stay in current sub-state; fade drives TRAN

        case SIG_ANIM_TICK:
            step_animation(controller);
            if (controller->turning_off && controller->brt_anim < ANIM_EPSILON) {
                controller->brt_anim  = 0.0f;
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
            ESP_LOGI(TAG, "[brightness]  brt=%d  cct=%d",
                     controller->brt_target, controller->cct_target);
            return STATE_HANDLED;

        case SIG_EXIT:
            // Timer keeps running when transitioning to preset_state.
            // It is disarmed in off_state SIG_ENTRY.
            return STATE_HANDLED;

        case SIG_BRIGHTNESS: {
            lightbar_event *le = (lightbar_event *)e;
            int16_t v = controller->brt_target + le->delta;
            if (v < 0) v = 0;
            if (v >= BRT_FRAMES) v = BRT_FRAMES - 1;
            controller->brt_target = v;
            ESP_LOGI(TAG, "[brightness] brt_target=%d", controller->brt_target);
            return STATE_HANDLED;
        }

        case SIG_PRESET: {
            lightbar_event *le = (lightbar_event *)e;
            if (le->delta != 0) {
                // Remote CCT knob → 1 click = 1 frame; switch to preset_state
                int16_t v = controller->cct_target + le->delta;
                if (v < 0) v = 0;
                if (v >= CCT_FRAMES) v = CCT_FRAMES - 1;
                controller->cct_target = v;
                ESP_LOGI(TAG, "[brightness] cct_target=%d → preset mode", controller->cct_target);
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
            ESP_LOGI(TAG, "[preset]  cct=%d", controller->cct_target);
            return STATE_HANDLED;

        case SIG_EXIT:
            return STATE_HANDLED;

        case SIG_PRESET: {
            lightbar_event *le = (lightbar_event *)e;
            if (le->delta != 0) {
                // Remote CCT knob — keep adjusting in frame units
                int16_t v = controller->cct_target + le->delta;
                if (v < 0) v = 0;
                if (v >= CCT_FRAMES) v = CCT_FRAMES - 1;
                controller->cct_target = v;
                ESP_LOGI(TAG, "[preset] cct_target=%d", controller->cct_target);
                return STATE_HANDLED;
            }
            // Local button → delegate to parent (cycle_preset)
            return lightbar_on_state(me, e);
        }

        case SIG_BRIGHTNESS: {
            // Remote returned to brightness mode — apply delta and switch back
            lightbar_event *le = (lightbar_event *)e;
            int16_t v = controller->brt_target + le->delta;
            if (v < 0) v = 0;
            if (v >= BRT_FRAMES) v = BRT_FRAMES - 1;
            controller->brt_target = v;
            ESP_LOGI(TAG, "[preset] brt_target=%d → brightness mode", controller->brt_target);
            return TRAN(lightbar_brightness_state);
        }

        default:
            return lightbar_on_state(me, e);
    }
}
