/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Xpost software product nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file xpost_dev_generic.h
 * @brief This file provides utilify functions for all devices.
 *
 * This header provides utility functions for all devices.
 * It opens and closes the file a device writes one page to.
 * And implements lower-level sorting and polygon filling
 * routines for speed.
 * @defgroup xpost_library Library functions
 *
 * @{
 */

#ifndef XPOST_DEV_GENERIC_H
#define XPOST_DEV_GENERIC_H

#include <stdio.h> /* FILE */

#include "xpost_private.h" /* XPOST_MUST_CHECK */

/** the standard output, as the file operator names it */
#define XPOST_DEV_STDOUT_NAME "%stdout"
#define XPOST_DEV_STDOUT_LEN (sizeof(XPOST_DEV_STDOUT_NAME) - 1)

/**
 * @brief open the file the page being written goes to
 *
 * The one opener a compiled device writes a page through. The name is
 * the one the page machinery settled on the device before running Emit,
 * so the page number a %d in the output name asks for is already in it
 * and every device numbers its pages alike. A device holds the stream
 * no longer than the page: it opens here, is written, and is closed
 * through xpost_device_page_close() before Emit returns.
 *
 * Returns NULL when the device carries no settled name or the name
 * cannot be opened.
 */
FILE *xpost_device_page_open(Xpost_Context *ctx, Xpost_Object devdic);

/**
 * @brief finish the file a page was written to
 *
 * Closes what xpost_device_page_open() opened. A standard stream is
 * flushed and left open, since it outlives the page.
 */
void xpost_device_page_close(FILE *f);

/**
 * @brief install operator .yxsort to improve performance of 'fill'
 *
 * also C fillpoly implementation that uses device DrawLine method.
 */
int xpost_oper_init_generic_device_ops(Xpost_Context *ctx,
                                       Xpost_Object sd);

/**
 * @brief append bytes to the pdfwrite device's content accumulator
 *
 * For the text operators, which emit glyph outlines as content-stream
 * path fragments. Answers 0 for no error, undefined when the device
 * carries no accumulator and VMerror when memory is exhausted.
 */
XPOST_MUST_CHECK int xpost_dev_pdf_append(Xpost_Context *ctx, Xpost_Object devdic,
                         const char *s, size_t n);

/**
 * @brief format a number in PDF content-stream syntax
 *
 * Writes an integer when integral, else two decimals (never
 * exponential). Returns the number of bytes written.
 */
int xpost_dev_pdf_fmt_num(char *o, double v);

/**
 * @brief retire the page device a restore to the given save level displaces
 *
 * PLRM 6.1 keeps the page device in the graphics state, so a restore
 * back past the setpagedevice that installed one reactivates the device
 * the saved state names and deactivates the replacement. The replacement
 * holds its raster or its content accumulator outside virtual memory,
 * which the collector neither reaches nor owns, so it is released here.
 * A restore that leaves the install standing retires nothing.
 */
void xpost_device_retire_restored(Xpost_Context *ctx, unsigned int level);

/**
 * @brief the page's ground, as channel values on the scale of @p scale
 *
 * The colour erasepage last cleared the page to, which the raster base
 * class records on the instance as it clears (data/image.ps, /Ground).
 * It is grey 1.0 through the transfer function in force (PLRM 8.2), so
 * what it comes to is the page's business rather than white by
 * assumption.
 *
 * A device whose page is a buffer of its own reads it for the pixels
 * that buffer does not hold: one outside the page, where a mark is
 * dropped and so a read answers rather than refusing, and every pixel of
 * an instance whose buffer has been released. A device that has not been
 * erased has no record, and the answer is the full scale its Create
 * filled the buffer with.
 *
 * The record is kept in the range a colour operand arrives in, which is
 * the only range every device shares, and each device folds it to the
 * channel it stores. @p scale is that device's integer channel scale,
 * the one it hands xpost_dev_num_to_scaled() -- 255 for an 8-bit
 * channel, 65535 for a 16-bit one. Folding once to a byte and stretching
 * the byte to a wider channel is not the same number as folding to the
 * wider channel, and the value a read answers is the value an erased
 * pixel of that device holds, which is whatever the device's own PutPix
 * would have written.
 */
void xpost_device_ground_scaled(Xpost_Context *ctx, Xpost_Object devdic,
                                double scale, int *r, int *g, int *b);

/**
 * @brief the page's ground, as 8-bit channel values
 *
 * xpost_device_ground_scaled() for a device whose channels are bytes,
 * which is every device here but the window one.
 */
void xpost_device_ground_channels(Xpost_Context *ctx, Xpost_Object devdic,
                                  int *r, int *g, int *b);

/**
 * @brief report what to allocate for a raster of @p w by @p h pixels of
 *        @p pixel bytes each, or refuse a buffer this platform cannot address
 *
 * Returns non-zero having written to @p bytes the whole size to ask the
 * allocator for -- @p reserve bytes for whatever the caller keeps in
 * front of the raster, such as its buffer header, plus the raster's own
 * -- or zero having written nothing, in which case the caller answers
 * limitcheck.
 *
 * The extent asked about is the buffer's: the block that is to be
 * resident and indexed, whose row width is what a pixel's position is
 * computed against. Every device here holds its whole page in one such
 * block, so it asks about the page's extent; the question is still the
 * buffer's, and a device holding less of the page than that would ask
 * about what it holds.
 *
 * A buffer of no extent is reported rather than refused: its raster
 * comes to no bytes and the reserve is all there is to allocate, and
 * whatever a device makes of an empty page it makes on its own terms. A
 * negative extent is not an extent and is refused with the unaddressable
 * ones.
 *
 * A device reaches a pixel by its position within the buffer, counted in
 * the width the platform expresses the size of a block of memory in. A
 * buffer whose pixel count, byte count or allocation size runs past that
 * width has no address for its far end however much memory is to hand,
 * so it is refused here rather than allocated and then indexed past: the
 * caller answers with limitcheck, which PLRM 8.2 gives for a limit of
 * the implementation rather than of the machine. What the machine will
 * actually give is the allocator's answer and not this one -- a size
 * this reports is a size that can be expressed and addressed, not a size
 * that is available -- and a caller whose allocation then fails answers
 * VMerror.
 *
 * Refusing before allocating also keeps a page nobody can reach from
 * asking the system for the memory to hold it.
 */
int xpost_device_raster_bytes(int w, int h, size_t pixel, size_t reserve,
                              size_t *bytes);

/**
 * @brief The block a raster of that many bytes sits in.
 *
 * A size expresses a raster no machine holds, so a count above half the
 * address space is answered here rather than put to an allocator: the
 * refusal then names the page it came from, and the devices that keep a
 * buffer of their own take their block the one way.
 *
 * @param[in] bytes what the raster and whatever sits in front of it come to
 * @return the block, or NULL for a count no allocator hands out
 */
void *xpost_device_raster_block(size_t bytes);

/**
 * @}
 */

#endif
