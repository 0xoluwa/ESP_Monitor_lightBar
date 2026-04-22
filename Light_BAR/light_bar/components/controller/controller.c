#include "controller.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "led_strip.h"
#include "connection.h"


const static uint8_t color_temp_preset_index[3] = {10, 35, 60};

/*FSM states*/

static fsm_state lightbar_entry_state      (lightbar_controller *me, fsm_event *e);
static fsm_state lightbar_off_state        (lightbar_controller *me, fsm_event *e);
static fsm_state lightbar_on_state         (lightbar_controller *me, fsm_event *e);

//led init
static void led_strip_setup(lightbar_controller *me);
static void led_strip_set_brightness(lightbar_controller *me, uint8_t brightness_index);
static void led_strip_set_color_temp(lightbar_controller *me, uint8_t kelvin_index);


//nvs init and helper
static void controller_nvs_init(lightbar_controller *me);


void lightbar_ctor(lightbar_controller *me, gpio_num_t led_pin){
    configASSERT(led_pin >= 0 && led_pin < GPIO_NUM_MAX);
    me->led_pin = led_pin;
    me->brt_curr_frame = 0;
    me->cct_cur_frame = 0;
    fsm_ctor((fsm *)me, QUEUE_DEPTH, sizeof(lightbar_event));
    fsm_time_event_ctor(&me->anim_timer, (fsm *)me, SIG_ANIM_TICK);
}

void lightbar_init(lightbar_controller *me, const char *task_name){
    led_strip_setup(me);
    controller_nvs_init(me);
    espnow_init(me);
    fsm_tick_init(1000);
    fsm_init((fsm *)me, task_name, (state_handler)lightbar_entry_state);
}

fsm_state lightbar_entry_state      (lightbar_controller *me, fsm_event *e){
    return TRAN(lightbar_on_state);
}

fsm_state lightbar_on_state(lightbar_controller *me, fsm_event *e){
    switch (e->signal){
        case SIG_ENTRY: {
            uint8_t brt_idx = 79;  
            uint8_t cct_idx = 50;  

            nvs_get_u8(me->nvs, "brt_idx", &brt_idx);
            nvs_get_u8(me->nvs, "cct_idx", &cct_idx);

            me->brt_target_frame = brt_idx;
            me->cct_target_frame = cct_idx;

            if (me->cct_target_frame > 64) me->cct_target_frame = 64;
            if (me->brt_target_frame > 100) me->brt_target_frame = 100;

            lightbar_event anim_event = {
                .super.signal = SIG_ANIM_TICK
            };

            fsm_post((fsm *) me, (fsm_event *) &anim_event);

            break;
        }
        
        case SIG_EXIT:  {
            nvs_set_u8(me->nvs, "brt_idx", (uint8_t)me->brt_target_frame);
            nvs_set_u8(me->nvs, "cct_idx", (uint8_t)me->cct_target_frame);
            nvs_commit(me->nvs);
            led_strip_set_brightness(me, 0);
            ESP_ERROR_CHECK(led_strip_refresh(me->strip_handle));
            fsm_time_event_disarm(&me->anim_timer);
            break;
        
        }
        
        case SIG_COLOR_TEMP:    {
            me->cct_target_frame += ((lightbar_event *) e)->delta;
            if (me->cct_target_frame > 64) me->cct_target_frame = 64;
            else if (me->cct_target_frame < 0) me->cct_target_frame = 0;

            lightbar_event anim_event = {
                .super.signal = SIG_ANIM_TICK
            };

            fsm_post((fsm *) me, (fsm_event *) &anim_event);
            break;
        }

        case SIG_COLOR_TEMP_PRESET: {
            int prev = me->cct_target_frame;
            me->cct_target_frame = color_temp_preset_index[0]; // default: wrap
            for (int i = 0; i < 3; i++) {
                if (prev < color_temp_preset_index[i]) {
                    me->cct_target_frame = color_temp_preset_index[i];
                    break;
                }
            }

            lightbar_event anim_event = {
                .super.signal = SIG_ANIM_TICK
            };

            fsm_post((fsm *) me, (fsm_event *) &anim_event);
            break;
        }

        case SIG_BRIGHTNESS:{
            me->brt_target_frame += ((lightbar_event *) e)->delta;
            if (me->brt_target_frame > 100) me->brt_target_frame = 100;
            else if (me->brt_target_frame < 0) me->brt_target_frame = 0;
            
            lightbar_event anim_event = {
                .super.signal = SIG_ANIM_TICK
            };

            fsm_post((fsm *) me, (fsm_event *) &anim_event);
            break;      
        }

        case SIG_POWER:{
            return TRAN(lightbar_off_state);
            break;
        }
        
        case SIG_ANIM_TICK: {
            bool refresh = false;
            if (me->cct_cur_frame != me->cct_target_frame){
                if (me->cct_cur_frame > me->cct_target_frame) me->cct_cur_frame--;
                else me->cct_cur_frame++;

                led_strip_set_color_temp(me, me->cct_cur_frame);
                fsm_time_event_rearm(&me->anim_timer, ANIM_TICK_PERIOD_MS);

                refresh = true;
            }

            if (me->brt_curr_frame != me->brt_target_frame){

                if (me->brt_curr_frame > me->brt_target_frame) me->brt_curr_frame--;
                else me->brt_curr_frame++;

                led_strip_set_brightness(me, me->brt_curr_frame);
                fsm_time_event_rearm(&me->anim_timer, ANIM_TICK_PERIOD_MS);

                refresh = true;
            }

            if (refresh) ESP_ERROR_CHECK(led_strip_refresh(me->strip_handle));

            break;
        }

        default:    {
            return STATE_IGNORED;
            break;
        }
    }

    return STATE_HANDLED;
}

fsm_state lightbar_off_state(lightbar_controller *me, fsm_event *e){
    switch(e->signal){
        case SIG_POWER: {
            return TRAN(lightbar_on_state);
        }

        case SIG_ENTRY: {
            break;

        }

        case SIG_EXIT: {
            break;

        }

        case SIG_INIT: {
            break;

        }
    }

    return STATE_HANDLED;
}

void led_strip_setup(lightbar_controller *me) {
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

void led_strip_set_brightness(lightbar_controller *me, uint8_t brightness_index)
{
    const uint16_t *current_temp_hsv_index = color_temp_lookup[me->cct_cur_frame];
    for (int pixel_count = 0; pixel_count < LIGHTBAR_NUM_LEDS; pixel_count++){
        ESP_ERROR_CHECK(led_strip_set_pixel_hsv_16(me->strip_handle, pixel_count, current_temp_hsv_index[0], current_temp_hsv_index[1], gamma_lut[brightness_index]));
    }
}

void led_strip_set_color_temp(lightbar_controller *me, uint8_t kelvin_index)
{
    for (int pixel_count = 0; pixel_count < LIGHTBAR_NUM_LEDS; pixel_count++){
        ESP_ERROR_CHECK(led_strip_set_pixel_hsv_16(me->strip_handle, pixel_count, color_temp_lookup[kelvin_index][0], color_temp_lookup[kelvin_index][1], gamma_lut[me->brt_curr_frame]));
    }
}

void post_power_button_isr(lightbar_controller *me){
    lightbar_event evt = {
        .super.signal = SIG_POWER
    };

    fsm_post_from_isr((fsm *) me, (fsm_event *) &evt);
}

void post_power_button(lightbar_controller *me){
    lightbar_event evt = {
        .super.signal = SIG_POWER
    };

    fsm_post((fsm *) me, (fsm_event *) &evt);
}

void post_color_temp_button(lightbar_controller *me){
    lightbar_event evt = {
        .super.signal = SIG_COLOR_TEMP_PRESET
    };

    fsm_post_from_isr((fsm *) me, (fsm_event *) &evt);
}

void post_color_temp_delta(lightbar_controller *me, int delta){
    lightbar_event event = {
        .super.signal = SIG_COLOR_TEMP,
        .delta = delta
    };

    fsm_post((fsm *) me, (fsm_event *) &event);
}

void post_brightness_delta(lightbar_controller *me, int delta){
    lightbar_event event = {
        .super.signal = SIG_BRIGHTNESS,
        .delta = delta
    };

    fsm_post((fsm *) me, (fsm_event *) &event);
}

void controller_nvs_init(lightbar_controller *me){
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(nvs_open("lightbar", NVS_READWRITE, &me->nvs));
}