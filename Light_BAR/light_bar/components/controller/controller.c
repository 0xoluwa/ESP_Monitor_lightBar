/**
 * @file controller.c
 * @brief Light-bar controller FSM implementation.
 *
 * Implements three states:
 *  - **entry**  — pseudo-state; transitions immediately to ON.
 *  - **on**     — LED strip is running; handles brightness, CCT, animation,
 *                 and NVS persistence.
 *  - **off**    — LED strip is dark; only the power signal is handled.
 */
#include "controller.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "led_strip.h"
#include "connection.h"

static const char *LIGHTBAR_DEBUG = "light debug";

/** @brief CCT frame indices for the three preset positions (warm → neutral → cool). */
static const uint8_t color_temp_preset_index[3] = { 10, 35, 60 };

/* ── State handler forward declarations ───────────────────────────────────── */

static fsm_state lightbar_entry_state(lightbar_controller *me, fsm_event *e);
static fsm_state lightbar_off_state  (lightbar_controller *me, fsm_event *e);
static fsm_state lightbar_on_state   (lightbar_controller *me, fsm_event *e);

/* ── Private helper forward declarations ──────────────────────────────────── */

static void led_strip_setup         (lightbar_controller *me);
static void led_strip_set_brightness(lightbar_controller *me, uint8_t brightness_index);
static void led_strip_set_color_temp(lightbar_controller *me, uint8_t kelvin_index);
static void controller_nvs_init     (lightbar_controller *me);

/* ── Public API ───────────────────────────────────────────────────────────── */

void lightbar_ctor(lightbar_controller *me, gpio_num_t led_pin)
{
    configASSERT(led_pin >= 0 && led_pin < GPIO_NUM_MAX);
    me->led_pin        = led_pin;
    me->brt_curr_frame = 0;
    me->cct_cur_frame  = 0;
    fsm_ctor((fsm *)me, QUEUE_DEPTH, sizeof(lightbar_event));
    fsm_time_event_ctor(&me->anim_timer, (fsm *)me, SIG_ANIM_TICK);
}

void lightbar_init(lightbar_controller *me, const char *task_name)
{
    led_strip_setup(me);
    controller_nvs_init(me);
    espnow_init(me);
    fsm_tick_init(1000);
    fsm_init((fsm *)me, task_name, (state_handler)lightbar_entry_state);
}

/* ── FSM states ───────────────────────────────────────────────────────────── */

/**
 * @brief Entry pseudo-state — unconditionally transitions to ON.
 */
static fsm_state lightbar_entry_state(lightbar_controller *me, fsm_event *e)
{
    return TRAN(lightbar_on_state);
}

/**
 * @brief ON state — LED strip is active.
 *
 * Signal handling:
 *  - **SIG_ENTRY**             — resets current frames to 0 (forces animation
 *                                from dark), loads NVS targets, seeds an
 *                                animation tick.
 *  - **SIG_EXIT**              — persists targets to NVS, turns off LEDs,
 *                                disarms the animation timer.
 *  - **SIG_COLOR_TEMP**        — clamps and updates the CCT target, seeds
 *                                an animation tick.
 *  - **SIG_COLOR_TEMP_PRESET** — cycles through three preset CCT indices
 *                                (wraps back to the lowest on overflow).
 *  - **SIG_BRIGHTNESS**        — clamps and updates the brightness target,
 *                                seeds an animation tick.
 *  - **SIG_POWER**             — transitions to the OFF state.
 *  - **SIG_ANIM_TICK**         — advances each current frame one step toward
 *                                its target, refreshes the strip, and rearms
 *                                the timer while frames are still moving.
 */
static fsm_state lightbar_on_state(lightbar_controller *me, fsm_event *e)
{
    switch (e->signal) {
        case SIG_ENTRY: {
            me->brt_curr_frame = 0; /* force animation to run from dark */
            me->cct_cur_frame  = 0;

            uint8_t brt_idx = BRIGHTNESS_FRAME_DEFAULT;
            uint8_t cct_idx = COLOR_TEMP_FRAME_DEFAULT;

            nvs_get_u8(me->nvs, "brt_idx", &brt_idx);
            nvs_get_u8(me->nvs, "cct_idx", &cct_idx);

            me->brt_target_frame = brt_idx;
            me->cct_target_frame = cct_idx;

            if (me->cct_target_frame > MAX_COLOR_TEMP_FRAME) me->cct_target_frame = MAX_COLOR_TEMP_FRAME;
            if (me->brt_target_frame > MAX_BRIGHTNESS_FRAME) me->brt_target_frame = MAX_BRIGHTNESS_FRAME;

            lightbar_event anim_event = { .super.signal = SIG_ANIM_TICK };
            fsm_post((fsm *)me, (fsm_event *)&anim_event);
            break;
        }

        case SIG_EXIT: {
            nvs_set_u8(me->nvs, "brt_idx", (uint8_t)me->brt_target_frame);
            nvs_set_u8(me->nvs, "cct_idx", (uint8_t)me->cct_target_frame);
            nvs_commit(me->nvs);
            led_strip_set_brightness(me, 0);
            ESP_ERROR_CHECK(led_strip_refresh(me->strip_handle));
            fsm_time_event_disarm(&me->anim_timer);
            break;
        }

        case SIG_COLOR_TEMP: {
            me->cct_target_frame += ((lightbar_event *)e)->delta;
            if      (me->cct_target_frame > MAX_COLOR_TEMP_FRAME) me->cct_target_frame = MAX_COLOR_TEMP_FRAME;
            else if (me->cct_target_frame < MIN_COLOR_TEMP_FRAME) me->cct_target_frame = MIN_COLOR_TEMP_FRAME;

            lightbar_event anim_event = { .super.signal = SIG_ANIM_TICK };
            ESP_LOGI(LIGHTBAR_DEBUG, "cct target frame: %i", me->cct_target_frame);
            fsm_post((fsm *)me, (fsm_event *)&anim_event);
            break;
        }

        case SIG_COLOR_TEMP_PRESET: {
            int prev = me->cct_target_frame;
            me->cct_target_frame = color_temp_preset_index[0]; /* default: wrap to lowest */
            for (int i = 0; i < 3; i++) {
                if (prev < color_temp_preset_index[i]) {
                    me->cct_target_frame = color_temp_preset_index[i];
                    break;
                }
            }

            lightbar_event anim_event = { .super.signal = SIG_ANIM_TICK };
            ESP_LOGI(LIGHTBAR_DEBUG, "cct target frame: %i", me->cct_target_frame);
            fsm_post((fsm *)me, (fsm_event *)&anim_event);
            break;
        }

        case SIG_BRIGHTNESS: {
            me->brt_target_frame += ((lightbar_event *)e)->delta;
            if      (me->brt_target_frame > MAX_BRIGHTNESS_FRAME) me->brt_target_frame = MAX_BRIGHTNESS_FRAME;
            else if (me->brt_target_frame < MIN_BRIGHTNESS_FRAME) me->brt_target_frame = MIN_BRIGHTNESS_FRAME;

            lightbar_event anim_event = { .super.signal = SIG_ANIM_TICK };
            ESP_LOGI(LIGHTBAR_DEBUG, "brt target frame: %i", me->brt_target_frame);
            fsm_post((fsm *)me, (fsm_event *)&anim_event);
            break;
        }

        case SIG_POWER:
            return TRAN(lightbar_off_state);

        case SIG_ANIM_TICK: {
            bool refresh = false;

            if (me->cct_cur_frame != me->cct_target_frame) {
                if (me->cct_cur_frame > me->cct_target_frame) me->cct_cur_frame--;
                else                                           me->cct_cur_frame++;

                led_strip_set_color_temp(me, me->cct_cur_frame);
                fsm_time_event_rearm(&me->anim_timer, ANIM_TICK_PERIOD_MS);
                refresh = true;
            }

            if (me->brt_curr_frame != me->brt_target_frame) {
                if (me->brt_curr_frame > me->brt_target_frame) me->brt_curr_frame--;
                else                                            me->brt_curr_frame++;

                led_strip_set_brightness(me, me->brt_curr_frame);
                fsm_time_event_rearm(&me->anim_timer, ANIM_TICK_PERIOD_MS);
                refresh = true;
            }

            if (refresh) ESP_ERROR_CHECK(led_strip_refresh(me->strip_handle));
            break;
        }

        default:
            return STATE_IGNORED;
    }

    return STATE_HANDLED;
}

/**
 * @brief OFF state — LED strip is dark.
 *
 * Only ::SIG_POWER is handled; all other signals are ignored.
 * The LED strip is driven to zero brightness and the animation timer is
 * disarmed in the ON state's SIG_EXIT handler before this state is entered.
 */
static fsm_state lightbar_off_state(lightbar_controller *me, fsm_event *e)
{
    switch (e->signal) {
        case SIG_POWER:
            return TRAN(lightbar_on_state);

        default:
            break;
    }

    return STATE_HANDLED;
}

/* ── Private helpers ──────────────────────────────────────────────────────── */

/**
 * @brief Initialise the WS2812 LED strip over SPI with DMA.
 * @param me Pointer to the controller instance.
 */
static void led_strip_setup(lightbar_controller *me)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num         = me->led_pin,
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

    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_cfg, &spi_cfg, &me->strip_handle));
}

/**
 * @brief Write the current CCT hue/saturation at the given brightness to every pixel.
 *
 * Reads the hue and saturation from the CCT look-up table at the controller's
 * current CCT frame and applies @p brightness_index as the HSV value component.
 *
 * @param me               Pointer to the controller instance.
 * @param brightness_index Raw brightness value (0 – 255).
 */
static void led_strip_set_brightness(lightbar_controller *me, uint8_t brightness_index)
{
    const uint16_t *hsv = color_temp_lookup[me->cct_cur_frame];
    for (int i = 0; i < LIGHTBAR_NUM_LEDS; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel_hsv_16(me->strip_handle, i, hsv[0], hsv[1], brightness_index));
    }
}

/**
 * @brief Write the hue/saturation for @p kelvin_index at the current brightness to every pixel.
 *
 * @param me           Pointer to the controller instance.
 * @param kelvin_index CCT frame index into ::color_temp_lookup.
 */
static void led_strip_set_color_temp(lightbar_controller *me, uint8_t kelvin_index)
{
    for (int i = 0; i < LIGHTBAR_NUM_LEDS; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel_hsv_16(
            me->strip_handle, i,
            color_temp_lookup[kelvin_index][0],
            color_temp_lookup[kelvin_index][1],
            me->brt_curr_frame));
    }
}

/**
 * @brief Initialise NVS flash and open the "lightbar" namespace.
 *
 * Erases NVS if the partition has no free pages or contains an incompatible
 * format version, then re-initialises before opening the namespace.
 *
 * @param me Pointer to the controller instance.
 */
static void controller_nvs_init(lightbar_controller *me)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(nvs_open("lightbar", NVS_READWRITE, &me->nvs));
}

/* ── Event posting helpers ────────────────────────────────────────────────── */

IRAM_ATTR void post_power_button_isr(lightbar_controller *me)
{
    lightbar_event evt = { .super.signal = SIG_POWER };
    fsm_post_from_isr((fsm *)me, (fsm_event *)&evt);
}

void post_power_button(lightbar_controller *me)
{
    lightbar_event evt = { .super.signal = SIG_POWER };
    fsm_post((fsm *)me, (fsm_event *)&evt);
}

IRAM_ATTR void post_color_temp_button(lightbar_controller *me)
{
    lightbar_event evt = { .super.signal = SIG_COLOR_TEMP_PRESET };
    fsm_post_from_isr((fsm *)me, (fsm_event *)&evt);
}

void post_color_temp_delta(lightbar_controller *me, int delta)
{
    lightbar_event event = { .super.signal = SIG_COLOR_TEMP, .delta = delta };
    fsm_post((fsm *)me, (fsm_event *)&event);
}

void post_brightness_delta(lightbar_controller *me, int delta)
{
    lightbar_event event = { .super.signal = SIG_BRIGHTNESS, .delta = delta };
    fsm_post((fsm *)me, (fsm_event *)&event);
}
