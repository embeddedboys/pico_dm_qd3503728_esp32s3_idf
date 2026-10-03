/*
 * Display + backlight platform glue over LovyanGFX (Bus_Parallel16 +
 * Panel_ILI9488 + Light_PWM), implementing the API surface the PUD decoder
 * layer expects (tft.h / backlight.h).
 *
 * The LGFX instance here is built with PUD_BOARD_NO_TOUCH: touch is the PUD
 * indev layer's own FT6236 driver (board_touch.c), so LovyanGFX must not
 * claim the I2C bus.  The LVGL demo (lvgl_demo.cpp) keeps the original
 * touch-enabled class.
 */

#include <stdint.h>

#include "tft.h"
#include "backlight.h"
#include "config.h"

#define PUD_BOARD_NO_TOUCH 1
#include "LGFX_MakerFabs_Parallel_S3.hpp"

static LGFX lcd;

extern "C" {

int tft_driver_init(void)
{
	lcd.init();
	lcd.setRotation(TFT_ROTATION);
	return 0;
}

int tft_set_rotation(uint8_t rotation)
{
	if (rotation > 3)
		return -1;
	lcd.setRotation(rotation);
	return 0;
}

void tft_video_flush(int xs, int ys, int xe, int ye, void *vmem, uint32_t len)
{
	lcd.startWrite();
	lcd.setAddrWindow(xs, ys, xe - xs + 1, ye - ys + 1);
	lcd.writePixels((const uint16_t *)vmem, len / 2, true);
	lcd.endWrite();
}

void tft_async_video_flush(int xs, int ys, int xe, int ye, void *vmem,
                           uint32_t len)
{
	lcd.startWrite();
	lcd.setAddrWindow(xs, ys, xe - xs + 1, ye - ys + 1);
	lcd.writePixelsDMA((const uint16_t *)vmem, len / 2, true);
	/* no endWrite: the transfer may still be in flight (see tft.h).  The
	 * next flush's window command waits for it (the bus waits for
	 * LCD_CAM_LCD_START to clear before issuing a command), which is the
	 * contract the decoder relies on for its buffer ping-pong. */
}

void tft_async_video_wait(void)
{
	lcd.waitDMA();
	lcd.endWrite();
}

static uint8_t s_bl_level;

void backlight_driver_init(void)
{
	backlight_set_level(100);
}

void backlight_set_level(uint8_t level)
{
	if (level > 100)
		level = 100;
	s_bl_level = level;
	lcd.setBrightness((uint16_t)level * 255 / 100);
}

uint8_t backlight_get_level(void)
{
	return s_bl_level;
}

}
