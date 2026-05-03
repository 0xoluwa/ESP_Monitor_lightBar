#ifndef __CONFIG_H__
#define __CONFIG_H__

#define KNOB_DT_PIN     2
#define KNOB_CLK_PIN    5
#define KNOB_BTN_PIN    34


#define KNOB_POLL_PERIOD_US    (2 * 1000)
#define KNOB_DELTA_FLUSH_US     (100 * 1000)

#define SLEEP_PERIOD_US     (5 * 1000 * 1000)

#define KNOB_BTN_DEBOUNCE_US  5000
#define SHORT_BTN_MAX_PERIOD (1 * 1000 * 1000)
#define KNOB_BTN_LONG_PRESS_PERIOD  (2 * 1000 * 1000)

#define ESPNOW_CHANNEL 1


#endif