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
 * QOI (Quite OK Image, RGB565 variant) decoding, ESP32-S3 port of
 * Pico-USB-Display/src/decoders/decoder_qoi.c (without the DECODER_STATS
 * counters; add them back when there is something to measure).
 *
 * Default path decodes a whole band into one of two ping-pong buffers and
 * flushes it asynchronously, so the panel write of one band overlaps the
 * decode of the next (QOI_NONCALLBACK=2).  The callback fallback streams
 * small batches for rectangles too big for the band buffer.
 */

#include <string.h>

#include "tft.h"
#include "decoder.h"
#include "decoder_internal.h"
#include "pud.h"

#include "esp_log.h"

#include "rgb565_qoi.h"

#ifndef QOI_BUF_ROWS
#define QOI_BUF_ROWS 8
#endif
/* 0 = callback API only, 1 = non-callback without overlap (measurement
 * only), 2 = non-callback with band ping-pong (default; see qoi_drawimg). */
#ifndef QOI_NONCALLBACK
#define QOI_NONCALLBACK 2
#endif

/* Whole panel rows per accumulation buffer, two of them ping-ponged. */
#define QOI_MAX_LOGICAL_WIDTH \
	((TFT_HOR_RES > TFT_VER_RES) ? TFT_HOR_RES : TFT_VER_RES)
#define QOI_BATCH_PIXELS (QOI_MAX_LOGICAL_WIDTH * QOI_BUF_ROWS)
static uint16_t qoi_buf_a[QOI_BATCH_PIXELS];
#if PUD_DECODER_PINGPONG
static uint16_t qoi_buf_b[QOI_BATCH_PIXELS];
#endif

#if QOI_NONCALLBACK >= 2
/* Two whole-band buffers: one is being written to the panel while the next
 * band is decoded into the other (see qoi_drawimg).  These feed the
 * LovyanGFX DMA path, so they must stay valid until the transfer lands --
 * the ping-pong plus the flush-side wait is what guarantees it. */
static uint16_t qoi_band[2 * PUD_BAND_MAX_PIXELS] __attribute__((aligned(4)));
static unsigned qoi_band_next;
#elif QOI_NONCALLBACK
static uint16_t qoi_band[PUD_BAND_MAX_PIXELS] __attribute__((aligned(4)));
#endif

struct qoi_draw_ctx {
	uint16_t ox;
	uint16_t oy;
};

static void qoi_flush(const uint16_t *pixels, size_t count, uint16_t xs,
                      uint16_t ys, uint16_t xe, uint16_t ye, void *user_data)
{
	struct qoi_draw_ctx *ctx = (struct qoi_draw_ctx *)user_data;

	/*
	 * Asynchronous flush: this returns while the panel is still receiving
	 * the batch, so the decoder can start on the other ping-pong buffer.
	 * The transfer is completed by the next flush (or by the wait at the
	 * end of qoi_drawimg), which is what keeps the buffer reuse safe.
	 */
#if PUD_DECODER_PINGPONG
	tft_async_video_flush(ctx->ox + xs, ctx->oy + ys, ctx->ox + xe,
	                      ctx->oy + ye, (void *)pixels, count * 2);
#else
	/* one buffer: it has to be free before the decoder fills it again */
	tft_video_flush(ctx->ox + xs, ctx->oy + ys, ctx->ox + xe, ctx->oy + ye,
	                (void *)pixels, count * 2);
#endif
}

void qoi_drawimg(u16 xs, u16 ys, u16 xe, u16 ye, u8 *qoi_data, u32 qoi_size)
{
	struct qoi_draw_ctx ctx;
	uint16_t width = xe - xs + 1;
	size_t buf_cap;

	if (qoi_data == NULL || qoi_size == 0 || width == 0 ||
	    width > g_pud_data.disp.xres) {
		ESP_LOGW("qoi", "reject: width %u > logical %u", width,
		         (unsigned)g_pud_data.disp.xres);
		return;
	}

	ctx.ox = xs;
	ctx.oy = ys;

#if QOI_NONCALLBACK
	/*
	 * Non-callback path: decode the whole transfer as one image into one
	 * band-sized buffer and flush it in a single window.  This is
	 * possible because the host bands every transfer by band_pixels, so
	 * one transfer is one rectangle that fits this buffer.  It is a lot
	 * faster than the streaming callback API, and with two buffers the
	 * panel write of one band overlaps the decode of the next.
	 */
	{
		size_t rect_px = (size_t)width * (size_t)(ye - ys + 1);

		if (rect_px <= PUD_BAND_MAX_PIXELS) {
#if QOI_NONCALLBACK >= 2
			uint16_t *buf =
			        qoi_band + qoi_band_next * PUD_BAND_MAX_PIXELS;
#else
			uint16_t *buf = qoi_band;
#endif

			if (rgb565_qoi_decompress(qoi_data, qoi_size, buf,
			                          rect_px) == rect_px) {
				tft_async_video_flush(xs, ys, xe, ye, buf,
				                      rect_px * 2);
#if QOI_NONCALLBACK >= 2
				/* The next flush's window command waits for
				 * this transfer, by which time the next band
				 * has been decoded into the other buffer. */
				qoi_band_next ^= 1u;
#else
				tft_async_video_wait();
#endif
			} else {
				ESP_LOGW("qoi", "decompress short/mismatch");
			}
			return;
		}
	}
#endif

	/* Keep batches aligned to whole rows: a multiple of the frame width,
	 * capped at the static buffer size. */
	buf_cap = (size_t)width * QOI_BUF_ROWS;
	if (buf_cap > QOI_BATCH_PIXELS)
		buf_cap = QOI_BATCH_PIXELS;

	rgb565_qoi_decompress_callback(qoi_data, qoi_size, width, qoi_buf_a,
#if PUD_DECODER_PINGPONG
	                               qoi_buf_b,
#else
	                               NULL,
#endif
	                               buf_cap, qoi_flush, &ctx);
	/* the last batch is still in flight; the frame is not done until it
	 * lands */
	tft_async_video_wait();
}
