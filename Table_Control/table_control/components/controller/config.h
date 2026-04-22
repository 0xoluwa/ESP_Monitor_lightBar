#ifndef __CONFIG_H__
#define __CONFIG_H__

/* ── uPesy ESP32 Wroom DevKit pin assignments ────────────────────────────────
 * Avoids strapping pins (0, 2, 5, 12, 15), UART0 (1, 3), and PSRAM (16, 17).
 *
 * GPIO18  Encoder CLK  (any digital input, interrupt-capable)
 * GPIO19  Encoder DATA (any digital input)
 * GPIO32  Encoder BTN  (RTC GPIO — required for EXT0 deep-sleep wakeup)
 * ─────────────────────────────────────────────────────────────────────────── */

#define KNOB_CLK_PIN    18
#define KNOB_DATA_PIN   19
#define KNOB_BUTTON_PIN 32   /* must be an RTC GPIO for deep-sleep EXT0 wakeup */

#define LED_RED_PIN  23
#define LED_BLUE_PIN  21
#define LED_GREEN_PIN  25

#define LED_FADE_TIME_MS 500
#define LED_PWM_DUTY    125


#endif
