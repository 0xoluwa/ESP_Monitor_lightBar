/**
 * @file config.h
 * @brief Board-level pin and PWM configuration for the table controller.
 *
 * Targets the uPesy ESP32 Wroom DevKit.  Pin assignments deliberately avoid:
 *  - Strapping pins  : GPIO 0, 2, 5, 12, 15
 *  - UART0 TX/RX     : GPIO 1, 3
 *  - PSRAM data lines: GPIO 16, 17
 *
 * GPIO 32 is an RTC-domain GPIO and is therefore required for EXT0
 * deep-sleep wakeup on button press.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/* ── Rotary encoder (knob) ────────────────────────────────────────────────── */

/** @brief Encoder CLK (channel A) – any interrupt-capable digital input. */
#define KNOB_CLK_PIN    18

/** @brief Encoder DATA (channel B) – any digital input. */
#define KNOB_DATA_PIN   19

/**
 * @brief Encoder push-button – must be an RTC GPIO for EXT0 deep-sleep wakeup.
 *
 * GPIO 32 is in the RTC domain; other non-RTC GPIOs cannot wake the chip
 * from deep sleep via the EXT0 source.
 */
#define KNOB_BUTTON_PIN 32


/* ── RGB status LED ───────────────────────────────────────────────────────── */

/** @brief Red channel – driven by LEDC channel 0. */
#define LED_RED_PIN    23

/** @brief Blue channel – driven by LEDC channel 2. */
#define LED_BLUE_PIN   21

/** @brief Green channel – driven by LEDC channel 1. */
#define LED_GREEN_PIN  25

/** @brief PWM fade duration in milliseconds for LED transitions. */
#define LED_FADE_TIME_MS 500

/** @brief Target PWM duty cycle (0–255) when an LED channel is fully on. */
#define LED_PWM_DUTY    125


#endif
