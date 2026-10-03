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
 * Decoder internals: frame slots and the band bound.  ESP32-S3 port of
 * Pico-USB-Display/src/decoders/decoder_internal.h, QOI-only build.
 */

#ifndef __DECODER_INTERNAL_H
#define __DECODER_INTERNAL_H

#include "decoder.h"
#include "pud.h"          /* PUD_EP1_HEADER_SIZE */
#include "usbd_vendor.h"  /* PUD_MAX_TRANSFER */

/* Batch ping-pong for the callback decoder (QOI): with a second buffer the
 * next batch can be decoded while the previous one is still being written
 * to the panel (the flush is asynchronous).  Correctness does not depend on
 * it: single buffer mode uses the synchronous flush. */
#ifndef PUD_DECODER_PINGPONG
#define PUD_DECODER_PINGPONG 1
#endif

/*
 * The largest band the host may send, in pixels -- one pixel more than the
 * device's own rounding, because the host rounds
 * ((min(65535, frame_max) - PUD_EP1_HEADER_SIZE - 16) / 3) while the device
 * rounds the same expression without the min().  The 16 bytes are the
 * codec's own framing (QOI's header plus end marker); the 12 are the EP1
 * header in front of every payload.
 */
#define PUD_BAND_MAX_PIXELS \
	(((PUD_MAX_TRANSFER - PUD_EP1_HEADER_SIZE - 16) / 3) + 1)

/*
 * Decode and the TFT flush must NOT run on the USB interrupt stack: the
 * decode path needs a real stack and holding the USB IRQ for tens of
 * milliseconds wedges the controller.  The USB ISR just copies the received
 * frame into a slot and wakes a dedicated decoder task.
 *
 * How many slots decides whether the decoder is visible at all.  EP1 is
 * armed only while a slot is free, so the pipeline is slots - 1 bands deep
 * (measured on the RP2350 build: 3 slots hide the decoder entirely behind
 * the full-speed link; 4 was out of RAM there).  One slot costs
 * PUD_MAX_TRANSFER: 32 KB here, 128 KB for the trio plus the read buffer.
 */
#define DECODER_FRAME_SLOTS 3
#define DECODER_FRAME_MAX PUD_MAX_TRANSFER

_Static_assert(DECODER_FRAME_MAX >= PUD_MAX_TRANSFER,
               "a frame slot must be able to hold a whole EP1 transfer");

#endif /* __DECODER_INTERNAL_H */
