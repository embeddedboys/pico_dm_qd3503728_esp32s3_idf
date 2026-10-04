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
 * Frame pipeline: USB ISR -> frame slots -> decoder task -> panel, with the
 * EP1 flow control hanging off the slot count.  ESP32-S3 port of
 * Pico-USB-Display/src/decoders/decoder.c, QOI-only build (the RP2350
 * original dispatches over seven DECODER_TYPEs; this one compiles the QOI
 * branch only, which is also what DECODER_TYPE reports to the host).
 */

#include <stdlib.h>
#include <string.h>

#include "tft.h"
#include "backlight.h"
#include "decoder.h"
#include "decoder_internal.h"
#include "usb.h"
#include "pud.h" /* PUD_EP1_HEADER_SIZE, the EP1 framing */

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "freertos/semphr.h"

static const char *TAG = "decoder";

void decoder_drawimg(u16 xs, u16 ys, u16 xe, u16 ye, u8 *data, u32 size,
                     int slot, u32 serial)
{
	(void)slot;   /* only DECODER_TYPE 6 (not built here) reads these */
	(void)serial;

	qoi_drawimg(xs, ys, xe, ye, data, size);
}

struct decoder_frame {
	u16 xs, ys, xe, ye;
	u32 size;
	u32 serial;
	u8 busy;
	u8 data[DECODER_FRAME_MAX];
};

EXT_RAM_BSS_ATTR static struct decoder_frame s_frames[DECODER_FRAME_SLOTS];
/* Slots are handed out lowest-free-first and drained in index order (the
 * RP2350 build tried a round-robin cursor pair and stalled the pipeline
 * under a full-screen load -- same model kept here). */

static SemaphoreHandle_t s_decoder_sem;

/* Diagnostics, readable from a debugger: frames seen / dropped / drawn.
 * A non-zero drop count means the host is outrunning the decoder and the
 * flow control is not doing its job (symptom: stale regions on screen). */
volatile u32 g_decoder_stat_submitted;
volatile u32 g_decoder_stat_dropped;
volatile u32 g_decoder_stat_drawn;
volatile u32 g_decoder_stat_oversize;

/* True when at least one frame slot is idle, i.e. the USB stack may arm
 * EP1 for the next frame.  Used for EP1 flow control (see usb.h). */
bool decoder_slot_free(void)
{
	int i;

	for (i = 0; i < DECODER_FRAME_SLOTS; i++) {
		if (!s_frames[i].busy)
			return true;
	}

	return false;
}

void decoder_submit_frame(u16 xs, u16 ys, u16 xe, u16 ye, const u8 *data,
                          u32 size)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	int i;

	/* Drop rather than truncate; the control stage refuses oversized
	 * transfers first, so this is a last line of defence. */
	if (size > DECODER_FRAME_MAX) {
		g_decoder_stat_oversize++;
		return;
	}

	g_decoder_stat_submitted++;

	for (i = 0; i < DECODER_FRAME_SLOTS; i++) {
		if (!s_frames[i].busy) {
			s_frames[i].xs = xs;
			s_frames[i].ys = ys;
			s_frames[i].xe = xe;
			s_frames[i].ye = ye;
			s_frames[i].size = size;
			s_frames[i].serial = g_decoder_stat_submitted - 1;
			memcpy(s_frames[i].data, data, size);
			s_frames[i].busy = 1;
			xSemaphoreGiveFromISR(s_decoder_sem,
			                      &xHigherPriorityTaskWoken);
			break;
		}
	}

	if (i == DECODER_FRAME_SLOTS)
		g_decoder_stat_dropped++;

	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void decoder_task(void *param)
{
	int i;

	(void)param;

	/* Whatever the host set over USB that has to reach the panel is
	 * applied here, before anything is drawn. */
	pud_params_flush_display();

	/* No boot logo in this build yet (the RP2350 original draws a
	 * QOI-compressed one baked into the firmware); the first host frame
	 * paints the screen.  TODO: generate one with pudcodec. */
	backlight_set_level(100);
	ESP_LOGI(TAG, "decoder task up, backlight 100%%");

	for (;;) {
		int slot = -1;

		/* Take stock before sleeping.  The submit side signals a binary
		 * semaphore, so two frames arriving close together can collapse
		 * into a single give; scanning the slots first draws the second
		 * frame now instead of after the wait below. */
		for (i = 0; i < DECODER_FRAME_SLOTS; i++) {
			if (s_frames[i].busy) {
				slot = i;
				break;
			}
		}

		if (slot < 0) {
			/* Nothing queued.  The timed wait doubles as the EP1
			 * caretaker: it drops a transfer whose host went away and
			 * re-arms a read that was lost. */
			if (xSemaphoreTake(s_decoder_sem,
			                   pdMS_TO_TICKS(EP1_POLL_PERIOD_MS)) != pdTRUE)
				usbd_vendor_ep1_poll();
			/* a rotation/brightness asked for while idle still has to
			 * land */
			pud_params_flush_display();
			continue;
		}

		/* Before this frame, not after: a rotation the host asked for
		 * arrives together with the mode change that follows it, and
		 * the frame it sends next is already in the new orientation. */
		pud_params_flush_display();

		decoder_drawimg(s_frames[slot].xs, s_frames[slot].ys,
		                s_frames[slot].xe, s_frames[slot].ye,
		                s_frames[slot].data, s_frames[slot].size, slot,
		                s_frames[slot].serial);
		s_frames[slot].busy = 0;
		g_decoder_stat_drawn++;
		/* A slot is free again: re-arm EP1 if the host's request was
		 * deferred. */
		usbd_vendor_ep1_tick();
	}
}

/*
 * ESP-IDF stack sizes are in bytes (the RP2350 original sizes in words).
 * The whole decode path peaked at 496 B on the RP2350 build (0xa5 fill
 * scan); 4 KB keeps the same generous margin.  QOI-only build, so the
 * JPEGDEC figure that motivated the original comment does not apply.
 */
#define DECODER_TASK_STACK_BYTES 4096

void decoder_init(void)
{
	BaseType_t created;

	s_decoder_sem = xSemaphoreCreateBinary();

	created = xTaskCreatePinnedToCore(decoder_task, "decoder_task",
	                                 DECODER_TASK_STACK_BYTES, NULL,
	                                 tskIDLE_PRIORITY + 1, NULL, 1);
	if (created != pdPASS) {
		/* Without this the failure is silent from both ends: bands are
		 * accepted until the slots fill, and then nothing drains one,
		 * so EP1 flow control holds the host forever and the panel
		 * simply stops updating. */
		ESP_LOGE(TAG, "FATAL: xTaskCreate(decoder_task) failed");
		abort();
	}

	ESP_LOGI(TAG, "Decoder type: QOI");
}

/* This build has no DECODER_TYPE 6 dictionary windows; answer with magic
 * plus zero slots so a host can tell "no windows" from "command not
 * understood". */
void qoid_read_state(struct pud_qoid_state *st)
{
	memset(st, 0, sizeof(*st));
	st->magic = PUD_QOID_MAGIC;
}
