#include "indev.h"
#include "config.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <string.h>

static const char *TAG = "touch";
static indev_direction_t s_dir;
static uint16_t s_raw_x;
static uint16_t s_raw_y;
static bool s_pressed;
static enum { TOUCH_NONE, TOUCH_FT6236, TOUCH_TSC2007 } s_controller;

#define FT6236_ADDR 0x38
#define FT_REG_TD_STATUS 0x02
#define FT_REG_TOUCH1_XH 0x03
#define FT_REG_TOUCH1_XL 0x04
#define FT_REG_TOUCH1_YH 0x05
#define FT_REG_TOUCH1_YL 0x06
#define FT_TOUCH_COORD_MASK 0x0f
#define TSC2007_CMD_READ_X 0xc0
#define TSC2007_CMD_READ_Y 0xd0

static esp_err_t i2c_read_bytes(uint8_t address, uint8_t reg,
		uint8_t *data, size_t length)
{
	return i2c_master_write_read_device(I2C_NUM_0, address, &reg, 1,
			data, length, pdMS_TO_TICKS(20));
}

static esp_err_t tsc2007_read_axis(uint8_t command, uint16_t *value)
{
	uint8_t raw[2];
	esp_err_t err = i2c_read_bytes(TSC2007_I2C_ADDR, command, raw,
			sizeof(raw));
	if (err != ESP_OK)
		return err;

	*value = (uint16_t)(((uint16_t)raw[0] << 4) | (raw[1] >> 4));
	return ESP_OK;
}

static esp_err_t ft6236_read_axis(uint8_t reg, uint16_t *value)
{
	uint8_t raw[2];
	esp_err_t err = i2c_read_bytes(FT6236_ADDR, reg, raw, sizeof(raw));
	if (err != ESP_OK)
		return err;
	*value = (uint16_t)(((raw[0] & FT_TOUCH_COORD_MASK) << 8) | raw[1]);
	return ESP_OK;
}

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
	s_dir = dir;
}

int indev_driver_init(void)
{
	i2c_config_t config = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = TSC2007_PIN_SDA,
		.scl_io_num = TSC2007_PIN_SCL,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 400000,
	};
	esp_err_t err = i2c_param_config(I2C_NUM_0, &config);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
		return err;
	err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
		return err;

	gpio_config_t gpio = {
		.pin_bit_mask = 1ULL << TSC2007_PIN_IRQ,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	err = gpio_config(&gpio);
	if (err != ESP_OK)
		return err;

	uint8_t probe;
	err = i2c_read_bytes(FT6236_ADDR, FT_REG_TD_STATUS, &probe, 1);
	if (err == ESP_OK) {
		s_controller = TOUCH_FT6236;
		ESP_LOGI(TAG, "FT6236 ready at 0x%02x, SDA=%d SCL=%d IRQ=%d",
				FT6236_ADDR, TSC2007_PIN_SDA, TSC2007_PIN_SCL,
				TSC2007_PIN_IRQ);
	} else {
		err = i2c_read_bytes(TSC2007_I2C_ADDR, TSC2007_CMD_READ_X,
				&probe, 1);
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "no FT6236 at 0x%02x or TSC2007 at 0x%02x",
					 FT6236_ADDR, TSC2007_I2C_ADDR);
			return err;
		}
		s_controller = TOUCH_TSC2007;
		ESP_LOGI(TAG, "TSC2007 ready at 0x%02x, SDA=%d SCL=%d IRQ=%d",
				TSC2007_I2C_ADDR, TSC2007_PIN_SDA, TSC2007_PIN_SCL,
				TSC2007_PIN_IRQ);
	}

	s_dir = indev_dir_for_rotation(TFT_ROTATION);
	ESP_LOGI(TAG, "TSC2007 ready at 0x%02x, SDA=%d SCL=%d IRQ=%d",
			TSC2007_I2C_ADDR, TSC2007_PIN_SDA, TSC2007_PIN_SCL,
			TSC2007_PIN_IRQ);
	return 0;
}

bool indev_is_pressed(void)
{
	uint16_t x, y;
	if (s_controller == TOUCH_FT6236) {
		uint8_t count;
		if (i2c_read_bytes(FT6236_ADDR, FT_REG_TD_STATUS, &count, 1) != ESP_OK)
			return false;
		s_pressed = (count & 0x0f) != 0;
		if (!s_pressed || ft6236_read_axis(FT_REG_TOUCH1_XH, &x) != ESP_OK ||
		    ft6236_read_axis(FT_REG_TOUCH1_YH, &y) != ESP_OK)
			return false;
		s_raw_x = x;
		s_raw_y = y;
	} else if (s_controller == TOUCH_TSC2007) {
		s_pressed = gpio_get_level(TSC2007_PIN_IRQ) == 0;
		if (!s_pressed || tsc2007_read_axis(TSC2007_CMD_READ_X, &x) != ESP_OK ||
		    tsc2007_read_axis(TSC2007_CMD_READ_Y, &y) != ESP_OK)
			return false;
		s_raw_x = (uint16_t)(((uint32_t)x * TFT_HOR_RES) / 4096);
		s_raw_y = (uint16_t)(((uint32_t)y * TFT_VER_RES) / 4096);
	} else {
		return false;
	}

	if (!s_pressed) {
		s_pressed = false;
		return false;
	}
	return true;
}

static void transformed_coords(uint16_t *x, uint16_t *y)
{
	uint16_t raw_x = *x;
	uint16_t raw_y = *y;
	uint16_t x_limit = TFT_HOR_RES;
	uint16_t y_limit = TFT_VER_RES;
	if (s_dir & INDEV_DIR_SWITCH_XY) {
		uint16_t swapped = raw_x;
		raw_x = raw_y;
		raw_y = swapped;
		x_limit = TFT_VER_RES;
		y_limit = TFT_HOR_RES;
	}
	if (s_dir & INDEV_DIR_INVERT_X)
		raw_x = x_limit - 1 - raw_x;
	if (s_dir & INDEV_DIR_INVERT_Y)
		raw_y = y_limit - 1 - raw_y;
	*x = raw_x;
	*y = raw_y;
}

uint16_t indev_read_x(void)
{
	uint16_t x = s_raw_x;
	uint16_t y = s_raw_y;
	transformed_coords(&x, &y);
	return x;
}

uint16_t indev_read_y(void)
{
	uint16_t x = s_raw_x;
	uint16_t y = s_raw_y;
	transformed_coords(&x, &y);
	return y;
}
