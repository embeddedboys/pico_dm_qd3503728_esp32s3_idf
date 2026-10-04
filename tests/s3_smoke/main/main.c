#include <inttypes.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "s3_smoke";
static volatile uint32_t s_core_ticks[2];

static void print_chip_info(void)
{
	esp_chip_info_t chip;
	uint32_t flash_size = 0;
	esp_chip_info(&chip);
	esp_flash_get_size(NULL, &flash_size);

	ESP_LOGI(TAG, "chip model=%d revision=%d cores=%d features=0x%" PRIx32,
	         chip.model, chip.revision, chip.cores, chip.features);
	ESP_LOGI(TAG, "cpu=%dMHz flash=%" PRIu32 "KB psram=%" PRIu32 "KB",
	         CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, flash_size / 1024,
	         (uint32_t)(esp_psram_get_size() / 1024));
	ESP_LOGI(TAG, "reset_reason=%d", esp_reset_reason());
}

static bool psram_test(void)
{
	const size_t size = 512 * 1024;
	uint8_t *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (buffer == NULL) {
		ESP_LOGE(TAG, "PSRAM allocation failed (%u bytes)", (unsigned)size);
		return false;
	}

	for (size_t i = 0; i < size; ++i)
		buffer[i] = (uint8_t)(i * 37u + 11u);
	for (size_t i = 0; i < size; ++i) {
		if (buffer[i] != (uint8_t)(i * 37u + 11u)) {
			ESP_LOGE(TAG, "PSRAM verify failed at %u", (unsigned)i);
			heap_caps_free(buffer);
			return false;
		}
	}

	uint32_t start = esp_timer_get_time();
	for (int pass = 0; pass < 8; ++pass)
		memset(buffer, pass, size);
	uint32_t elapsed_us = (uint32_t)(esp_timer_get_time() - start);
	uint32_t mbps = elapsed_us ? (uint32_t)((size * 8ULL) / elapsed_us) : 0;
	ESP_LOGI(TAG, "PSRAM read/write PASS, size=%u KB memset=%" PRIu32 " MB/s",
	         (unsigned)(size / 1024), mbps);
	heap_caps_free(buffer);
	return true;
}

static void core_task(void *arg)
{
	const int core = (int)(intptr_t)arg;
	for (;;) {
		s_core_ticks[core]++;
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void app_main(void)
{
	print_chip_info();
	ESP_LOGI(TAG, "heap internal=%u KB 8bit=%u KB",
	         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
	         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
	bool psram_ok = psram_test();

	xTaskCreatePinnedToCore(core_task, "core0", 2048, (void *)(intptr_t)0,
	                        1, NULL, 0);
	xTaskCreatePinnedToCore(core_task, "core1", 2048, (void *)(intptr_t)1,
	                        1, NULL, 1);
	ESP_LOGI(TAG, "dual-core tasks started, psram=%s", psram_ok ? "PASS" : "FAIL");

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
		ESP_LOGI(TAG, "heartbeat core0=%" PRIu32 " core1=%" PRIu32
		         " internal_free=%u KB psram_free=%u KB",
		         s_core_ticks[0], s_core_ticks[1],
		         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
		         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
	}
}
