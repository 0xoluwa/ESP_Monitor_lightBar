#ifndef __CONFIG_H__
#define __CONFIG_H__

#define KNOB_DT_PIN     4
#define KNOB_CLK_PIN    2
#define KNOB_BTN_PIN    32


#define KNOB_POLL_PERIOD_US    (3 * 1000)
#define KNOB_DELTA_FLUSH_US     (300 * 1000)

#define SLEEP_PERIOD_US     (5 * 1000 * 1000)

#define KNOB_BTN_DEBOUNCE_US  5000
#define SHORT_BTN_MAX_PERIOD (1 * 1000 * 1000)
#define KNOB_BTN_LONG_PRESS_PERIOD  (2 * 1000 * 1000)

#define ESPNOW_CHANNEL 1

static const uint8_t LIGHTBAR_MAC[ESP_NOW_ETH_ALEN] = {0xB4, 0xBF, 0xE9, 0x15, 0xA6, 0xD4};


#endif