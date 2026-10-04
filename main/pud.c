// Copyright (c) 2025 embeddedboys developers
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

/*
 * PUD device abstraction: identity/caps data and the touch poll.
 * ESP32-S3 port of Pico-USB-Display/src/pud.c (serial number comes from the
 * efuse base MAC instead of the RP2350 flash unique id).
 */

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pud.h"
#include "decoder.h"

static const char *TAG = "pud";

struct pud_data g_pud_data = { 0 };

/*
 * The length is clamped to the field: these are fed from the host's EP2
 * request (`struct req_ep2_in.size`), and copying it verbatim into a fixed
 * field once wrote past the target on the RP2350 build.
 */
#define define_pud_ro_attr(field, field_type)             \
	void pud_get_ro_##field(field_type *ptr, int len) \
	{                                                 \
		struct pud_data *data = &g_pud_data;      \
		if (len > (int)sizeof(data->field))       \
			len = (int)sizeof(data->field);   \
		memcpy((void *)ptr, &data->field, len);   \
	}

#define define_pud_ro_sub_attr(parent, field, field_type)            \
	void pud_get_ro_##parent##_##field(field_type *ptr, int len) \
	{                                                            \
		struct pud_data *data = &g_pud_data;                 \
		if (len > (int)sizeof(data->parent.field))           \
			len = (int)sizeof(data->parent.field);       \
		memcpy((void *)ptr, &data->parent.field, len);       \
	}

define_pud_ro_attr(sn, u8);

define_pud_ro_sub_attr(disp, xres, u16);
define_pud_ro_sub_attr(disp, yres, u16);
define_pud_ro_sub_attr(disp, pixelclock_khz, u16);

static void pud_config_init(void)
{
	struct pud_data *data = &g_pud_data;
	u8 mac[6];

	/* Serial number: efuse base MAC (6 B, factory-unique) folded into the
	 * protocol's 8-byte field. */
	esp_read_mac(mac, ESP_MAC_BASE);
	memcpy(data->sn, mac, 6);
	data->sn[6] = mac[0] ^ mac[2] ^ mac[4];
	data->sn[7] = mac[1] ^ mac[3] ^ mac[5];

	if (TFT_ROTATION & 1) {
		data->disp.xres = TFT_VER_RES;
		data->disp.yres = TFT_HOR_RES;
	} else {
		data->disp.xres = TFT_HOR_RES;
		data->disp.yres = TFT_VER_RES;
	}
	data->disp.rotation = TFT_ROTATION;
	data->disp.intf_type = 0;
	data->disp.pixelclock_khz = TFT_BUS_CLK_KHZ;
	/* the panel and the EP1 stream are 16 bpp */
	data->disp.bpp = 16;

	data->disp.width_mm = PUD_PANEL_WIDTH_MM;
	data->disp.height_mm = PUD_PANEL_HEIGHT_MM;

	/* 0 when this build has no touch driver (PUD_CAPS_TOUCH is clear
	 * then) */
	data->tp.polling_period = INDEV_DRV_NOT_USED ? 0 :
	                                               INDEV_POLLING_PERIOD_MS;

	ESP_LOGI(TAG, "SN: %02x%02x%02x%02x%02x%02x%02x%02x",
	         data->sn[0], data->sn[1], data->sn[2], data->sn[3],
	         data->sn[4], data->sn[5], data->sn[6], data->sn[7]);
	ESP_LOGI(TAG, "display: %ux%u @ %u kHz, rotation %u",
	         data->disp.xres, data->disp.yres,
	         (unsigned)data->disp.pixelclock_khz, data->disp.rotation);
}

/*
 * One touch poll.
 *
 * Keeps g_pud_data.tp current and pushes a report when there is something
 * to say: while the panel is held (so moves arrive) and once on release.
 * Nothing is sent while it is idle -- the host's interrupt URB simply stays
 * pending, which is how an input endpoint normally works.
 */
void pud_touch_poll(void)
{
	static bool was_pressed;
	static u8 seq;
	struct pud_touch_report report;
	bool pressed = indev_is_pressed();
	u16 x = 0, y = 0;

	if (pressed) {
		/* indev_read_*() returns panel coordinates in the display
		 * frame (the indev layer applies the rotation transform).
		 * Clamp anyway -- the report is a wire contract. */
		x = indev_read_x();
		y = indev_read_y();
		if (x >= g_pud_data.disp.xres)
			x = g_pud_data.disp.xres - 1;
		if (y >= g_pud_data.disp.yres)
			y = g_pud_data.disp.yres - 1;
	}

	if (!pressed && !was_pressed)
		return; /* idle, and it already knows */

	was_pressed = pressed;

	report.flags = pressed ? PUD_TOUCH_PRESSED : 0;
	report.x_hi = (u8)(x >> 8);
	report.x_lo = (u8)(x & 0xff);
	report.y_hi = (u8)(y >> 8);
	report.y_lo = (u8)(y & 0xff);
	report.seq = ++seq;
	report.version = PUD_TOUCH_VERSION;
	report.reserved = 0;

	usbd_vendor_ep4_submit(&report);
}

void pud_init(void)
{
	ESP_LOGI(TAG, "init: display begin");
	tft_driver_init();
	ESP_LOGI(TAG, "init: display ready");
	backlight_driver_init();
	ESP_LOGI(TAG, "init: backlight ready");
	if (!INDEV_DRV_NOT_USED && indev_driver_init() != 0)
		ESP_LOGW(TAG, "touch driver initialization failed");
	ESP_LOGI(TAG, "init: touch ready");
	decoder_init();
	ESP_LOGI(TAG, "init: decoder ready");

	pud_config_init();
}

static void touch_task(void *arg)
{
	(void)arg;
	for (;;) {
		pud_touch_poll();
		vTaskDelay(pdMS_TO_TICKS(INDEV_POLLING_PERIOD_MS));
	}
}

void pud_touch_task_start(void)
{
	if (!INDEV_DRV_NOT_USED)
		xTaskCreate(touch_task, "touch", 3072, NULL, 5, NULL);
}
