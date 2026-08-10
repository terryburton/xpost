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
 * @brief report the bytes a raster of @p w by @p h pixels of @p pixel
 *        bytes each needs, or refuse a page this interpreter cannot address
 *
 * Returns non-zero having written the byte count to @p bytes, or zero
 * having written nothing, in which case the caller answers limitcheck.
 *
 * A page of no extent is reported rather than refused: it comes to no
 * bytes and is built, and whatever a device makes of an empty page it
 * makes on its own terms. A negative extent is not a page and is refused
 * with the unaddressable ones.
 *
 * A device indexes its raster by a pixel's position within it, and the
 * arithmetic that reaches a row is done in the width the interpreter
 * counts pixels in. A page whose pixels outnumber what that width counts
 * cannot be addressed however much memory is to hand, so it is refused
 * here rather than allocated and then indexed past: the caller answers
 * with limitcheck, which PLRM 8.2 gives for a limit of the implementation
 * rather than of the machine.
 *
 * Refusing before allocating also keeps a page nobody can draw from
 * asking the system for the memory to hold it.
 */
int xpost_device_raster_bytes(int w, int h, size_t pixel, size_t *bytes);

/**
 * @}
 */

#endif
