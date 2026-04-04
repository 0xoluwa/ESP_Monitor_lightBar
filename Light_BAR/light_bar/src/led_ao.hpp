#include "ao.hpp"

#include "esp_log.h"
#include "led_strip.h"
#include "ESP32Encoder/src/InterruptEncoder.h"
#include "esp_log.h"

static constexpr uint8_t  NUM_LEDS   = 30;
static constexpr uint8_t  NUM_COLORS = 3;
static constexpr gpio_num_t LED_PIN  = GPIO_NUM_2;

enum Color : uint8_t { RED = 0, GREEN = 1, BLUE = 2 };



/*
 * LedAO — the Active Object.
 *
 * State machine uses pointer-to-member-function (PTMF) for dispatch.
 * This is the C++ equivalent of a function pointer state machine:
 *   - no virtual dispatch overhead per event
 *   - states are private methods — no external coupling
 *   - tran() fires EXIT/ENTRY symmetrically, same as QP's Q_TRAN
 *
 * HSM hierarchy is implemented by explicit delegation:
 *   default: stateTop(e);   ← sub-states call parent on unhandled events
 */
class LedAO : public AO {
public:
    LedAO()
        : AO(16, sizeof(Event))
    {
        led_strip_init();
    }
 
    void postButtonPress() { Event e{SIG_BTN_PRESS}; post(e); }
    void postKnobCW()      { Event e{SIG_KNOB_CW};   post(e); }
    void postKnobCCW()     { Event e{SIG_KNOB_CCW};  post(e); }
 
protected:
    void run() override {
        const Event entry{SIG_ENTRY};
        (this->*state_)(entry);
 
        Event e;
        for (;;) {
            xQueueReceive(queue(), &e, portMAX_DELAY); // ← protected getter
            (this->*state_)(e);
        }
    }
 
private:
    using StateFn = void (LedAO::*)(const Event &);
 
    // -------------------------------------------------------------------------
    // tran() — transition with symmetric EXIT / ENTRY
    // -------------------------------------------------------------------------
    void tran(StateFn next) {
        const Event exit{SIG_EXIT}, entry{SIG_ENTRY};
        (this->*state_)(exit);
        state_ = next;
        (this->*state_)(entry);
    }

    inline void led_strip_init(){
        led_strip_spi_config_t strip_comm_config;
        led_strip_config_t  strip_config;

        strip_comm_config.clk_src = SPI_CLK_SRC_DEFAULT;
        strip_comm_config.spi_bus = SPI2_HOST;
        strip_comm_config.flags.with_dma = 1;

        strip_config = {
            .strip_gpio_num = LED_PIN,
            .max_leds = NUM_LEDS,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags = {.invert_out = 0},
        };

        ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &strip_comm_config, &strip_handle_));
    }
 
    // -------------------------------------------------------------------------
    // stateTop — PARENT state
    // Handles knob events. Sub-states delegate here on default.
    // -------------------------------------------------------------------------
    void stateTop(const Event &e) {
        switch (e.sig) {
 
        case SIG_KNOB_CW:
            pos_[selected_] = (pos_[selected_] + 1) % NUM_LEDS;
            refreshStrip();
            ESP_LOGI(TAG, "CW → %s pos=%u",
                     colorName(selected_), pos_[selected_]);
            break;
 
        case SIG_KNOB_CCW:
            // Add NUM_LEDS before subtracting to avoid uint8 underflow
            pos_[selected_] = (pos_[selected_] + NUM_LEDS - 1) % NUM_LEDS;
            refreshStrip();
            ESP_LOGI(TAG, "CCW → %s pos=%u",
                     colorName(selected_), pos_[selected_]);
            break;
 
        default:
            break; // TOP silently drops anything it doesn't handle
        }
    }
 
    // -------------------------------------------------------------------------
    // stateRed
    // -------------------------------------------------------------------------
    void stateRed(const Event &e) {
        switch (e.sig) {
 
        case SIG_ENTRY:
            selected_ = RED;
            ESP_LOGI(TAG, "[RED selected]  pos=%u", pos_[RED]);
            break;
 
        case SIG_EXIT:
            ESP_LOGI(TAG, "[RED] exit");
            break;
 
        case SIG_BTN_PRESS:
            tran(&LedAO::stateGreen);
            break;
 
        default:
            stateTop(e);    // ← HSM delegation to parent
            break;
        }
    }
 
    // -------------------------------------------------------------------------
    // stateGreen
    // -------------------------------------------------------------------------
    void stateGreen(const Event &e) {
        switch (e.sig) {
 
        case SIG_ENTRY:
            selected_ = GREEN;
            ESP_LOGI(TAG, "[GREEN selected]  pos=%u", pos_[GREEN]);
            break;
 
        case SIG_EXIT:
            ESP_LOGI(TAG, "[GREEN] exit");
            break;
 
        case SIG_BTN_PRESS:
            tran(&LedAO::stateBlue);
            break;
 
        default:
            stateTop(e);
            break;
        }
    }
 
    // -------------------------------------------------------------------------
    // stateBlue
    // -------------------------------------------------------------------------
    void stateBlue(const Event &e) {
        switch (e.sig) {
 
        case SIG_ENTRY:
            selected_ = BLUE;
            ESP_LOGI(TAG, "[BLUE selected]  pos=%u", pos_[BLUE]);
            break;
 
        case SIG_EXIT:
            ESP_LOGI(TAG, "[BLUE] exit");
            break;
 
        case SIG_BTN_PRESS:
            tran(&LedAO::stateRed);   // wrap around
            break;
 
        default:
            stateTop(e);
            break;
        }
    }
 
    // -------------------------------------------------------------------------
    // refreshStrip()
    // Clears pixel buffer, sets one pixel per color, writes to strip.
    // Called only from within this task — no race condition possible.
    // -------------------------------------------------------------------------
    void refreshStrip() {
        led_strip_clear(strip_handle_);

        // Accumulate R, G, B contributions per strip index
        uint8_t r[NUM_LEDS] = {0};
        uint8_t g[NUM_LEDS] = {0};
        uint8_t b[NUM_LEDS] = {0};

        r[pos_[RED]]   = led_brightness_;
        g[pos_[GREEN]] = led_brightness_;
        b[pos_[BLUE]]  = led_brightness_;

        for (uint8_t i = 0; i < NUM_LEDS; i++) {
            led_strip_set_pixel(strip_handle_, i, r[i], g[i], b[i]);
        }

        led_strip_refresh(strip_handle_);
    }
 
    static const char *colorName(Color c) {
        switch (c) {
        case RED:   return "RED";
        case GREEN: return "GREEN";
        case BLUE:  return "BLUE";
        default:    return "?";
        }
    }
 
    // State data
    StateFn  state_    = &LedAO::stateRed;
    uint8_t  pos_[NUM_COLORS] = {0, 0, 0};
    Color    selected_ = RED;
    uint8_t led_brightness_ = 255;
 
    // WS2812 pixel buffer
    uint8_t  pixels_[NUM_LEDS * 3] = {};
 
    // led strip handles (populated in constructor)
    led_strip_handle_t strip_handle_;
 
    static const char *TAG;
};
 
const char *LedAO::TAG = "LedAO";