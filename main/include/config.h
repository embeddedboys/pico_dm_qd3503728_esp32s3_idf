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

/* Touch (FT6236 on the display board, I2C 0x38).  INDEV_DRV_NOT_USED flips to
 * 0 together with the EP4 implementation; while 1 the capability report says
 * "no touch" and the host must not register an input device. */
#define INDEV_DRV_NOT_USED 1
#define INDEV_POLLING_PERIOD_MS 10

/* Active area in mm, 0 = unknown (the RP2350 build ships 0 for this panel
 * too; the host then leaves the input axis resolution alone). */
#define PUD_PANEL_WIDTH_MM 0
#define PUD_PANEL_HEIGHT_MM 0

/* Decoder selection: DECODER_USE_QOI (3).  The value is protocol-visible. */
#define DECODER_TYPE 3

/* Largest single EP1 transfer, header included: 32 KB profile (the RP2040
 * tier).  Sizes ep1_read_buffer and each of the 3 decoder frame slots
 * (128 KB static RAM total); ESP32-S3 runs this board without PSRAM because
 * the panel's data bus sits on the octal-PSRAM pins.  The definition itself
 * lives in usb/usbd_vendor.h (PUD_MAX_TRANSFER), next to the other protocol
 * constants. */

#endif /* __PUD_BOARD_CONFIG_H */
