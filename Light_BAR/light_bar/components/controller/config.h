#ifndef __CONFIG_H__
#define __CONFIG_H__

#define LIGHTBAR_NUM_LEDS    76

#define CCT_TABLE_SIZE       65        // entries in color_temp_lookup
#define BRT_TABLE_SIZE       101       // entries in gamma_lut (0–100 inclusive)

#define ANIM_TICK_PERIOD_MS         20        // 50 Hz animation rate

#define QUEUE_DEPTH 20


#define MAX_COLOR_TEMP_FRAME 64
#define MIN_COLOR_TEMP_FRAME 10

#define MAX_BRIGHTNESS_FRAME 100
#define MIN_BRIGHTNESS_FRAME 10


#define BRIGHTNESS_FRAME_DEFAULT 80
#define COLOR_TEMP_FRAME_DEFAULT 40


#endif
