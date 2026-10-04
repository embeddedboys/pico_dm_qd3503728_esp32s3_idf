/*
 * PUD device firmware entry (ESP32-S3 + Pico_DM_QD3503728).
 *
 * Brings up the display path (LovyanGFX glue), the decoder pipeline and the
 * CherryUSB device stack, then lets the tasks run: USB interrupts move EP1
 * transfers into frame slots, the decoder task decodes and pushes to the
 * panel.  CONFIG_PUD_LVGL_DEMO runs the original vendor demo instead (kept
 * as a known-good reference for the hardware).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"
#include "pud.h"
#include "usb.h"

static const char *TAG = "pud";

extern void lvgl_demo_run(void);

void app_main(void)
{
#if CONFIG_PUD_LVGL_DEMO
	lvgl_demo_run();
	/* not reached */
#endif

	ESP_LOGI(TAG, "PUD device init (USB OTG device on GPIO19/20; "
	              "console moves here, UART0 TX=GPIO42)");

	pud_init();
	pud_touch_task_start();
	usb_device_init();

	ESP_LOGI(TAG, "PUD device up: %ux%u, decoder QOI, frame_max %u",
	         g_pud_data.disp.xres, g_pud_data.disp.yres,
	         (unsigned)PUD_MAX_TRANSFER);

	/* app_main's task is no longer needed: USB runs on its interrupt,
	 * the decoder task does the drawing. */
}
