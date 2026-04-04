#include "controller.h"
#include "nvs.h"
#include "esp_now.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "led_strip.h"

#define NUM_LED 4

#define NVS_KEY_LEN 6

static led_strip_handle_t strip_handle_;
static nvs_handle_t  storage_handle;

static char const nvs_brightness_multiplier[] = "brightness";
static const char nvs_current_color[] = "cur_color";
static const char nvs_custom_color[] = "cus_color";
static const char nvs_presaved_color_1[] = "presavedColor1";
static const char nvs_presaved_color_2[] = "presavedColor2";
static const char nvs_presaved_color_3[] = "presavedColor3";

char const * nvs_key_array[NVS_KEY_LEN] = {nvs_brightness_multiplier, nvs_current_color, nvs_custom_color, nvs_presaved_color_1, nvs_presaved_color_2, nvs_presaved_color_3};

static inline void led_strip_init(controller * me){
    led_strip_spi_config_t strip_comm_config;
    led_strip_config_t  strip_config;

    strip_comm_config.clk_src = SPI_CLK_SRC_DEFAULT;
    strip_comm_config.spi_bus = SPI2_HOST;
    strip_comm_config.flags.with_dma = 1;

    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.max_leds = NUM_LED;
    strip_config.strip_gpio_num = me->pin.strip_data_pin;

    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &strip_comm_config, &strip_handle_));
}

static inline void storage_init(controller * me){
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("device", NVS_READWRITE, &storage_handle));

    int32_t out_value = 0;

    for (int i = 0; i < NVS_KEY_LEN; i++){
        if (nvs_get_i32(storage_handle, nvs_key_array[i], &out_value) != ESP_ERR_NVS_NOT_FOUND){
            nvs_set_i32(storage_handle, nvs_key_array[i], 0);
        }
    }
}

void controller_ctor(controller * me){
    fsm *fsm_pointer = (fsm *) me;
    fsm_ctor(fsm_pointer, QUEUE_DEPTH, sizeof(controller_event));
}

void controller_init(controller * me, const char* controller_name){
    led_strip_init(me);             //initialize led_strip 
    fsm *fsm_pointer = (fsm *) me;
    fsm_init(fsm_pointer, controller_name, entry_handler);
}

fsm_state entry_handler (fsm *me, fsm_event *event){
    return TRAN(brightness_state);
}

fsm_state top_main_state(fsm *me, fsm_event *event){
    switch(event->signal){
        case SIG_BRIGHT_BTN_PRESS:
            return TRAN(brightness_state);

        case SIG_PRESET_BTN_PRESS:
            return TRAN(preset_state);

        case SIG_CUSTOM_BTN_PRESS:
            return TRAN(custom_state);

        default:
            return STATE_IGNORED;
    }

}

fsm_state brightness_state(fsm *me, fsm_event *event){
    fsm_state state;
    int knob_count;

    switch(event->signal){
        case SIG_ENTRY:
            state = STATE_HANDLED;
            break;
            
        case SIG_KNOB:
            knob_count = ((controller_event *) event)->signal_data.knob_count;
            state = STATE_HANDLED;
            break;

        case SIG_EXIT:
            state = STATE_HANDLED;
            break;

        default:
            state = top_main_state(me, event);
            break;
    }

    return state;
}

fsm_state preset_state(fsm *me, fsm_event *event){
    fsm_state state;
    int knob_count;

    switch(event->signal){
        case SIG_ENTRY:
            state = STATE_HANDLED;
            break;

        case SIG_KNOB:
            knob_count = ((controller_event *) event)->signal_data.knob_count;
            state = STATE_HANDLED;
            break;

        case SIG_EXIT:
            state = STATE_HANDLED;
            break;

        default:
            state = top_main_state(me, event);
            break;
    }

    return state;
}

fsm_state custom_state(fsm *me, fsm_event *event){
    fsm_state state;
    int knob_count;

    switch(event->signal){
        case SIG_ENTRY:
            state = STATE_HANDLED;
            break;

        case SIG_KNOB:
            knob_count = ((controller_event *) event)->signal_data.knob_count;
            state = STATE_HANDLED;
            break;

        default:
            state = top_main_state(me, event);
            break;
    }

    return state;
}

fsm_state custom_red(fsm *me, fsm_event *event){
    fsm_state state;

    switch(event->signal){
        case SIG_ENTRY:
            state = STATE_HANDLED;
            break;

        case SIG_KNOB_BTN_PRESS:
            state = TRAN(custom_green);
            break;

        case SIG_EXIT:
            state = STATE_HANDLED;
            break;

        default:
            state = custom_state(me, event);
            break;
    }

    return state;
}

fsm_state custom_green(fsm *me, fsm_event *event){
    fsm_state state;

    switch(event->signal){
        case SIG_ENTRY:
            state = STATE_HANDLED;
            break;

        case SIG_KNOB_BTN_PRESS:
            state = TRAN(custom_blue);
            break;

        case SIG_EXIT:
            state = STATE_HANDLED;
            break;

        default:
            state = custom_state(me, event);
            break;
    }

    return state;
}

fsm_state custom_blue(fsm *me, fsm_event *event){
    fsm_state state;

    switch(event->signal){
        case SIG_ENTRY:
            state = STATE_HANDLED;
            break;

        case SIG_KNOB_BTN_PRESS:
            state = TRAN(custom_red);
            break;

        case SIG_EXIT:
            state = STATE_HANDLED;
            break;

        default:
            state = custom_state(me, event);
            break;
    }

    return state;
}



void post_knob_count(controller * me, int knob_count){
    fsm *me_fsm = (fsm *) me;

    controller_event event = {
        .super = {SIG_KNOB},
        .signal_data.knob_count = knob_count
    };

    fsm_post(me_fsm, (fsm_event *) &event);
}

void post_bright_btn(controller * me){
    fsm *me_fsm = (fsm *) me;

    fsm_event event = {SIG_BRIGHT_BTN_PRESS};

    fsm_post(me_fsm, &event);
}

void post_preset_btn(controller * me){
    fsm *me_fsm = (fsm *) me;

    fsm_event event = {SIG_PRESET_BTN_PRESS};

    fsm_post_from_isr(me_fsm, &event);
}

void post_custom_btn(controller * me){
    fsm *me_fsm = (fsm *) me;

    fsm_event event = {SIG_CUSTOM_BTN_PRESS};

    fsm_post_from_isr(me_fsm, &event);
}


