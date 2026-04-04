#include "stdint.h"

enum Signal : uint8_t {
    SIG_ENTRY = 0,      // synthesised by dispatcher on state enter
    SIG_EXIT,           // synthesised by dispatcher on state exit
    SIG_POWER_BTN_PRESS,
    SIG_PRESET_BTN_PRESS,        // encoder rotated clockwise
    SIG_BRIGHTNESS_PLUS,
    SIG_BRIGHTNESS_MINUS,
    SIG_EXT_CUSTOM_,
    SIG_MAX
};