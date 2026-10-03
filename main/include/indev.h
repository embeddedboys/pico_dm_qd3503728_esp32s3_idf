/*
 * Touch platform API.  Same split as pico-display-lib: the controller driver
 * returns raw values, the single transform (axis order / inversion / clamp,
 * following the display rotation) lives in the indev layer.
 */

#ifndef __PUD_BOARD_INDEV_H
#define __PUD_BOARD_INDEV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	INDEV_DIR_NOP = 0x00,
	INDEV_DIR_SWITCH_XY = 0x01,
	INDEV_DIR_INVERT_X = 0x02,
	INDEV_DIR_INVERT_Y = 0x04,
} indev_direction_t;

int indev_driver_init(void);
bool indev_is_pressed(void);
uint16_t indev_read_x(void);
uint16_t indev_read_y(void);

indev_direction_t indev_dir_for_rotation(uint8_t rotation);
void indev_set_dir(indev_direction_t dir);

#ifdef __cplusplus
}
#endif

#endif /* __PUD_BOARD_INDEV_H */
