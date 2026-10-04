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
 * PUD protocol definitions, device side.  Mirrored from
 * Pico-USB-Display/include/pud.h: the wire structures are protocol fields
 * and must stay identical to the host driver's pud.h and to
 * PUD-kernel-drivers/notes/usb-protocol.md.  ESP32-S3 port.
 */

#ifndef __PUD_H
#define __PUD_H

#include "usb.h"
#include "tft.h"
#include "indev.h"
#include "config.h"
#include "backlight.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef signed char s8;
typedef signed short s16;
typedef signed int s32;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* Panel active area in mm (config.h).  0 means "unknown", which the host
 * reads as "leave the input resolution alone". */
#ifndef PUD_PANEL_WIDTH_MM
#define PUD_PANEL_WIDTH_MM 0
#endif
#ifndef PUD_PANEL_HEIGHT_MM
#define PUD_PANEL_HEIGHT_MM 0
#endif

#define PUD_CMD_GET_SN 0x01
#define PUD_CMD_GET_CAPS 0x02
#define PUD_CMD_SET_PARAM 0x03 /* control OUT (REQ_SET_PARAM), struct pud_params */
#define PUD_CMD_GET_PARAM 0x04 /* EP2 IN, struct pud_param_state */
/* DECODER_TYPE 6 dictionary-window state (struct pud_qoid_state).  This
 * build only ships QOI, so it answers magic plus zero slots -- a host can
 * tell "no windows" from "command unknown". */
#define PUD_CMD_GET_QOID 0x05

/* Device capabilities, answered by PUD_CMD_GET_CAPS on the EP2 IN path.
 * Keep this struct in sync with the driver's pud.h and with
 * scripts/pud_usb.py; fields are appended only, never reordered. */
#define PUD_CAPS_MAGIC 0x43445550 /* "PUDC" */
#define PUD_PROTO_VER 2

struct pud_caps {
	u32 magic;
	u32 proto_ver;
	u32 frame_max; /* max bytes per EP1 transfer, header included */
	u32 decoder_type; /* 0 tjpgd, 1 JPEGDEC, 2 LZ4, 3 QOI, 4 RLE,
	                     5 QOI+deflate, 6 QOI+deflate+dict */

	u16 xres; /* panel size in the frame it is driven in */
	u16 yres;
	u16 pixelclock_khz; /* bus clock the panel is driven with */
	u8 rotation; /* TFT_ROTATION the firmware applied */
	u8 bpp;
	u8 intf_type;
	u8 tp_polling_period; /* touch poll period, ms (0 when there is no touch) */
	u16 width_mm; /* active area, for the host's input resolution */
	u16 height_mm;
	u16 flags; /* PUD_CAPS_*: what this build actually has */
};

#define PUD_CAPS_SIZE 32
_Static_assert(sizeof(struct pud_caps) == PUD_CAPS_SIZE,
               "pud_caps wire size (append only, never reorder)");

/* Capability flags.  Touch is optional; the host must not register an input
 * device when PUD_CAPS_TOUCH is clear. */
#define PUD_CAPS_TOUCH 0x0001 /* an indev driver is compiled in and polled */

/* Runtime parameters (protocol v2, appended without a version bump). */
#define PUD_PARAM_BRIGHTNESS 0x00000001 /* u8, 0..100 percent */
#define PUD_PARAM_ROTATION 0x00000002 /* 0..3, TFT_ROTATION numbering */
/* 0x00000004 is retired: it was `fps`, and the device does not pace frames at
 * all -- EP1 flow control makes the host wait, which cannot drop a frame. */
#define PUD_PARAM_DECODER 0x00000008 /* 0..6, DECODER_TYPE numbering */

struct pud_params {
	u32 mask; /* PUD_PARAM_*: which of the values below this write sets */
	u8 brightness; /* 0..100 percent */
	u8 rotation; /* TFT_ROTATION numbering */
	u8 reserved; /* retired `fps`: keeps the struct at 8 bytes, no padding */
	u8 decoder; /* DECODER_TYPE numbering */
};

struct pud_param_state {
	u32 settable; /* PUD_PARAM_* this build can change at runtime */
	u32 rejected; /* PUD_PARAM_* the last PUD_CMD_SET_PARAM could not apply */
	u8 brightness; /* values in effect now */
	u8 rotation;
	u8 reserved;
	u8 decoder;
};

#define PUD_PARAMS_SIZE 8
#define PUD_PARAM_STATE_SIZE 12
_Static_assert(sizeof(struct pud_params) == PUD_PARAMS_SIZE,
               "pud_params wire size");
_Static_assert(sizeof(struct pud_param_state) == PUD_PARAM_STATE_SIZE,
               "pud_param_state wire size");

/*
 * EP1 OUT framing (protocol v2).
 *
 * Every transfer is a header followed by the payload it describes:
 *
 *     [ struct pud_ep1_header ][ payload ]
 *
 * The device reads one max-size packet first, which always holds the whole
 * header (PUD_EP1_HEADER_SIZE <= 64), and then exactly the remaining payload,
 * so the end of a transfer never depends on a short packet.  A header that
 * declares more than frame_max - PUD_EP1_HEADER_SIZE, or a rectangle off the
 * panel, is dropped and the endpoint re-armed (counted in g_ep1_stat),
 * deliberately not stalled.
 */
#define PUD_EP1_HEADER_SIZE 12

struct pud_ep1_header {
	u16 xs;
	u16 ys;
	u16 xe;
	u16 ye;
	u32 size; /* payload bytes that follow */
};

/* DECODER_TYPE 6 state query.  This build has no dictionary windows; it still
 * answers so a host can tell "no windows" from "command not understood". */
#define PUD_QOID_MAGIC 0x51445550u /* 'PUDQ' */
#define PUD_QOID_WINDOWS 4
struct pud_qoid_state {
	u32 magic;
	u32 slots;
	u32 serial[PUD_QOID_WINDOWS];
	u32 len[PUD_QOID_WINDOWS];
	u32 valid[PUD_QOID_WINDOWS];
	u32 window;
};

#define PUD_QOID_STATE_SIZE 60
_Static_assert(sizeof(struct pud_qoid_state) == PUD_QOID_STATE_SIZE,
               "pud_qoid_state wire size");

struct disp_data {
	u16 xres;
	u16 yres;
	u8 rotation;
	u32 pixelclock_khz;
	u8 bpp;
	u8 intf_type;
	u16 width_mm;
	u16 height_mm;
};

struct tp_data {
	u8 polling_period;
};

/*
 * EP4 touch report, one per interrupt IN transfer (see
 * PUD-kernel-drivers/notes/usb-protocol.md).  Byte-explicit layout, a
 * protocol field.
 *
 *   0  flags      bit0 = pressed
 *   1  x >> 8     panel coordinates in the frame the panel is driven in
 *   2  x & 0xff
 *   3  y >> 8
 *   4  y & 0xff
 *   5  sequence   wraps
 *   6  version    PUD_TOUCH_VERSION (0 = this build has no touch driver)
 *   7  reserved   0
 */
#define PUD_TOUCH_VERSION 1
#define PUD_TOUCH_PRESSED 0x01

struct pud_touch_report {
	u8 flags;
	u8 x_hi;
	u8 x_lo;
	u8 y_hi;
	u8 y_lo;
	u8 seq;
	u8 version;
	u8 reserved;
};

struct pud_data {
	u8 sn[8];

	struct disp_data disp;
	struct tp_data tp;
};

extern struct pud_data g_pud_data;

void pud_init(void);

/* Reads are clamped to the field, so an EP2 request can ask for less than
 * the whole field but never past it. */
void pud_get_ro_sn(u8 *ptr, int len);

void pud_get_ro_disp_xres(u16 *ptr, int len);
void pud_get_ro_disp_yres(u16 *ptr, int len);
void pud_get_ro_disp_pixelclock_khz(u16 *ptr, int len);

/* One touch poll: push an EP4 report if there is something to say.
 * Called from the indev task every polling_period ms. */
void pud_touch_poll(void);
void pud_touch_task_start(void);

#endif /* __PUD_H */
