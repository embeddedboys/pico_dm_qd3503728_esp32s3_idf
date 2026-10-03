/*
 * Touch platform layer: FT6236 (I2C 0x38) driver + the single coordinate
 * transform (same model as pico-display-lib's indev.c: raw controller values
 * from the driver, axis order/inversion/clamp here, following the display
 * rotation).
 *
 * Phase 1 state: the controller driver is stubbed out
 * (INDEV_DRV_NOT_USED=1 in config.h, so the capability report says "no
 * touch" and a host will not register an input device).  The rotation
 * mapping is real -- the runtime-parameter path needs it before EP4 exists.
 * Phase 4 fills in the actual FT6236 reads and flips the flag.
 */

#include "indev.h"
#include "config.h"

indev_direction_t indev_dir_for_rotation(uint8_t rotation)
{
	switch (rotation) {
	case TFT_ROTATE_90:
		/* panel x comes from the controller's y, panel y from its
		 * inverted x (320x480 native, driven at 480x320) */
		return INDEV_DIR_SWITCH_XY | INDEV_DIR_INVERT_Y;
	case TFT_ROTATE_180:
		return INDEV_DIR_INVERT_X | INDEV_DIR_INVERT_Y;
	case TFT_ROTATE_270:
		return INDEV_DIR_SWITCH_XY | INDEV_DIR_INVERT_X;
	case TFT_ROTATE_0:
	default:
		return INDEV_DIR_NOP;
	}
}

void indev_set_dir(indev_direction_t dir)
{
	(void)dir;
}

int indev_driver_init(void)
{
	return 0;
}

bool indev_is_pressed(void)
{
	return false;
}

uint16_t indev_read_x(void)
{
	return 0;
}

uint16_t indev_read_y(void)
{
	return 0;
}
