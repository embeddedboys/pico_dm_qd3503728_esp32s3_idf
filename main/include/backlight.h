/* Backlight platform API (same contract as pico-display-lib's backlight.h). */

#ifndef __PUD_BOARD_BACKLIGHT_H
#define __PUD_BOARD_BACKLIGHT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void backlight_driver_init(void);
void backlight_set_level(uint8_t level); /* 0..100 percent */
uint8_t backlight_get_level(void);

#ifdef __cplusplus
}
#endif

#endif /* __PUD_BOARD_BACKLIGHT_H */
