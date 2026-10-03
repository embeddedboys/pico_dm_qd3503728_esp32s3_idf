/*
 * Display platform API, the surface the PUD decoder layer expects (same
 * contract as pico-display-lib's tft.h, implemented over LovyanGFX here).
 */

#ifndef __PUD_BOARD_TFT_H
#define __PUD_BOARD_TFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int tft_driver_init(void);
int tft_set_rotation(uint8_t rotation);

/* Synchronous: returns when the whole window has been pushed to the panel.
 * `len` is in bytes (RGB565). */
void tft_video_flush(int xs, int ys, int xe, int ye, void *vmem, uint32_t len);

/* Asynchronous: returns while the transfer may still be in flight; `vmem`
 * must not be reused until tft_async_video_wait() returned (or the next
 * async flush, which completes the previous one first).  One transfer in
 * flight at a time. */
void tft_async_video_flush(int xs, int ys, int xe, int ye, void *vmem,
                           uint32_t len);
void tft_async_video_wait(void);

#ifdef __cplusplus
}
#endif

#endif /* __PUD_BOARD_TFT_H */
