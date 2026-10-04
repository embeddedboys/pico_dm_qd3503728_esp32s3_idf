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
#include "boot_request.h"
#include "usb.h"

static const char *TAG = "pud";

extern void lvgl_demo_run(void);

void app_main(void)
{

	/* 最早处：主机要用这个把板子请进下载态，所以它必须早于任何可能崩的初始化
	 * （曾经死在显示初始化，那时这行还没跑到就已经重启了）。 */
	pud_boot_request_start();


#if CONFIG_PUD_LVGL_DEMO
	lvgl_demo_run();
	/* not reached */
#endif

	ESP_LOGI(TAG, "PUD device init (USB OTG device on GPIO19/20; "
	              "console moves here, UART0 TX=GPIO42)");

	pud_init();
	/* 分步日志：修 IWDT 复位时要能说出是哪一步把中断关了 300 ms（判据是
	 * 复位前最后打印的那一行）。 */
	ESP_LOGI(TAG, "step: touch task start");
	pud_touch_task_start();
	ESP_LOGI(TAG, "step: touch task started");
	ESP_LOGI(TAG, "step: usb init");
	usb_device_init();
	ESP_LOGI(TAG, "step: usb ready");

	ESP_LOGI(TAG, "PUD device up: %ux%u, decoder QOI, frame_max %u",
	         g_pud_data.disp.xres, g_pud_data.disp.yres,
	         (unsigned)PUD_MAX_TRANSFER);

	/* app_main's task is no longer needed: USB runs on its interrupt,
	 * the decoder task does the drawing. */
}
