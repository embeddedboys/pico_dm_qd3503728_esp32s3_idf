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
 * Decoder interface, ESP32-S3 port.  This build ships QOI only
 * (DECODER_TYPE 3); the numbering itself is a protocol field (reported in
 * PUD_CMD_GET_CAPS) and the values below are kept identical to the RP2350
 * firmware so the host's decoder_type means the same thing everywhere.
 */

#ifndef __DECODER_H
#define __DECODER_H

#include <stdbool.h>

#define DECODER_USE_TJPGD 0
#define DECODER_USE_JPEGDEC 1
#define DECODER_USE_LZ4 2
#define DECODER_USE_QOI 3
#define DECODER_USE_RLE 4
#define DECODER_USE_QOIZ 5
#define DECODER_USE_QOID 6

/* config.h always defines this (3 = QOI); the fallback only applies to a
 * build that bypassed it and has to agree with that default. */
#ifndef DECODER_TYPE
#define DECODER_TYPE DECODER_USE_QOI
#endif

#if DECODER_TYPE != DECODER_USE_QOI
#error "this build ships the QOI decoder only (DECODER_TYPE 3)"
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void qoi_drawimg(u16 xs, u16 ys, u16 xe, u16 ye, u8 *qoi_data,
                        u32 qoi_size);

/* Answers PUD_CMD_GET_QOID: which band each DECODER_TYPE 6 dictionary
 * window holds.  Defined for every build -- a device without windows still
 * has to answer, otherwise a host cannot tell "no windows" from "command
 * not understood". */
struct pud_qoid_state;
extern void qoid_read_state(struct pud_qoid_state *st);

extern void decoder_init(void);
extern void decoder_submit_frame(u16 xs, u16 ys, u16 xe, u16 ye,
                                 const u8 *data, u32 size);
extern bool decoder_slot_free(void);

/* Decode one band with the codec this firmware was built for (QOI). */
extern void decoder_drawimg(u16 xs, u16 ys, u16 xe, u16 ye, u8 *data,
                            u32 size, int slot, u32 serial);

#endif /* __DECODER_H */
