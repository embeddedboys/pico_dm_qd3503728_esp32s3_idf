/*
 * Board configuration for the PUD device on Pico_DM_QD3503728 +
 * Nologo ESP32-S3 Pico.  Values mirror the RP2350 build's
 * configs/pico_dm_qd3503728.cmake unless noted; anything protocol-visible
 * (resolution, rotation, bus clock, transfer sizes) is part of the reply to
 * PUD_CMD_GET_CAPS and must stay in sync with the facts the host is told.
 */

#ifndef __PUD_BOARD_CONFIG_H
#define __PUD_BOARD_CONFIG_H

/* Panel geometry: ILI9488 native 320x480, driven rotated so the host sees
 * 480x320 (TFT_ROTATION 1), same as the RP2350 build. */
#define TFT_HOR_RES 320
#define TFT_VER_RES 480
#define TFT_ROTATION 1

/* LovyanGFX parallel bus write clock (cfg.freq_write in the board hpp). */
#define TFT_BUS_CLK_KHZ 50000

enum tft_rotation {
	TFT_ROTATE_0 = 0x00,
	TFT_ROTATE_90 = 0x01,
	TFT_ROTATE_180 = 0x02,
	TFT_ROTATE_270 = 0x03,
};

/* Touch controller selection is automatic: FT6236 at 0x38 is preferred, then
 * TSC2007 at 0x48. Both use the original FT6236 wiring. */
#define INDEV_DRV_NOT_USED 0
#define TSC2007_I2C_ADDR 0x48
#define TSC2007_PIN_SDA 7
#define TSC2007_PIN_SCL 8
#define TSC2007_PIN_IRQ 5
#define INDEV_POLLING_PERIOD_MS 10

/* Active area in mm, 0 = unknown (the RP2350 build ships 0 for this panel
 * too; the host then leaves the input axis resolution alone). */
#define PUD_PANEL_WIDTH_MM 0
#define PUD_PANEL_HEIGHT_MM 0

/* Decoder selection: DECODER_USE_QOI (3).  The value is protocol-visible. */
#define DECODER_TYPE 3

/* The ESP32-S3-PICO SiP provides internal Octal PSRAM.  The panel remains
 * on its existing 16-bit hardware bus; decoder frame slots live in PSRAM. */

#endif /* __PUD_BOARD_CONFIG_H */
