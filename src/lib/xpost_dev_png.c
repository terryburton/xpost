/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * Copyright (C) 2013-2016, Vincent Torri
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_LIBPNG

#include <stddef.h> /* offsetof */
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <setjmp.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h" /* access memory */
#include "xpost_object.h" /* work with objects */
#include "xpost_stack.h"  /* push results on stack */
#include "xpost_context.h" /* state */
#include "xpost_error.h"
#include "xpost_dict.h" /* get/put values in dicts */
#include "xpost_string.h" /* get/put values in strings */
#include "xpost_array.h"
#include "xpost_name.h" /* create names */

#include "xpost_operator.h" /* create operators */
#include "xpost_op_dict.h" /* call load operator for convenience */
#include "xpost_dev_generic.h" /* the page file opener */
#include "xpost_dev_driver.h" /* device contract and shared helpers */
#include "xpost_dev_png.h" /* check prototypes */

typedef struct
{
    unsigned char red, green, blue, alpha;
} Xpost_Png_Pixel;

typedef struct
{
    int width, height, byte_stride;
    /* the block this raster is part of. A client is handed the raster
       and gives the block back, so the block's own address is kept
       here, immediately before the raster, where the release entry
       point reads it. */
    void *block;
    Xpost_Png_Pixel data[1];
} Xpost_Png_Buffer;

/* Say that the block's address is immediately before the raster rather
   than leave it to hold by luck: the release entry point reaches it by
   stepping one pointer back from the address the client holds. (A
   negative array size rather than _Static_assert: this builds as C99
   with -pedantic-errors, which rejects the latter.) */
typedef char xpost_png_block_precedes_the_raster[
    offsetof(Xpost_Png_Buffer, data)
    == offsetof(Xpost_Png_Buffer, block) + sizeof(void *) ? 1 : -1];

/* A PNG stream holds exactly one image, so the file and the writer that
   fills it belong to the page and not to the device: both are made when
   a page is emitted and finished before that emit returns. What the
   instance keeps between pages is the raster and the two settings the
   pages are written under. */
typedef struct
{
    int width;
    int height;
    /*
     * add additional members to private struct
     */
    Xpost_Png_Buffer *buf;
    unsigned int interlaced : 1;
    unsigned int alpha : 1;
    /* the device allocated buf and has not handed it to the client
       through OutputBufferOut, so Destroy frees it */
    unsigned int bufowned : 1;
} PrivateData;

static Xpost_Object namePrivate;
static Xpost_Object namewidth;
static Xpost_Object nameheight;
static Xpost_Object namedotcopydict;
static Xpost_Object namenativecolorspace;
static Xpost_Object nameDeviceRGB;


static unsigned int _create_cont_opcode;

/* create an instance of the device
   using the class .copydict procedure */
static
int _create(Xpost_Context *ctx,
            Xpost_Object width,
            Xpost_Object height,
            Xpost_Object classdic)
{
    int ret;

    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    xpost_stack_push(ctx->lo, ctx->os, classdic);
    ret = xpost_dict_put(ctx, classdic, namewidth, width);
    if (ret)
        return ret;
    ret = xpost_dict_put(ctx, classdic, nameheight, height);
    if (ret)
        return ret;

    /* call device class's ps-level .copydict procedure,
       //call base-class's Create procedure (to initialize ImgData array)
       then call _create_cont, by continuation. */
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons_opcode(_create_cont_opcode)))
        return execstackoverflow;

    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_dict_get(ctx, classdic, namedotcopydict)))
        return execstackoverflow;

    return 0;
}

/* initialize the C-level data
   and define in the device instance */
static
int _create_cont(Xpost_Context *ctx,
                 Xpost_Object w,
                 Xpost_Object h,
                 Xpost_Object devdic)
{
    PrivateData private;
    Xpost_Object privatestr;
    Xpost_Object ud;
    Xpost_Object interlaced_o;
    int width, height;
    int ret;
    //printf("create_cont\n");

    /* The page the program asked for, as the extent of the buffer that
       will hold it. Every device here holds a whole page in one block,
       so the two carry the same numbers; a page naming an extent no
       buffer's row arithmetic carries is refused before anything is
       built for it. */
    if (!xpost_dev_buffer_extent(w.int_.val, &width)
     || !xpost_dev_buffer_extent(h.int_.val, &height))
    {
        XPOST_LOG_ERR("%d a page of %ldx%ld names an extent no raster"
                      " carries", limitcheck,
                      (long)w.int_.val, (long)h.int_.val);
        return limitcheck;
    }

    /* create a string to contain device data structure */
    ret = xpost_handle_cons(ctx, devdic, namePrivate, &privatestr,
                            XPOST_HANDLE_DEVICE, sizeof(PrivateData));
    if (ret)
        return ret;

    private.width = width;
    private.height = height;
    {
        Xpost_Object alpha_o = xpost_dict_get(ctx, devdic,
                                              xpost_name_cons(ctx, "AlphaChannel"));
        private.alpha = xpost_object_get_type(alpha_o) == booleantype
                     && alpha_o.int_.val;
    }

    /*
     *
     * initialize additional members of private struct
     *
     */

    ud = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    interlaced_o = xpost_dict_get(ctx, ud,
                                  xpost_name_cons(ctx, "png_interlaced"));

    if (xpost_object_get_type(interlaced_o) == invalidtype)
        private.interlaced = PNG_INTERLACE_NONE;
    else
    {
        if (interlaced_o.int_.val)
        {
#ifdef PNG_WRITE_INTERLACING_SUPPORTED
            private.interlaced = PNG_INTERLACE_ADAM7;
#else
            private.interlaced = PNG_INTERLACE_NONE;
#endif
        }
        else
            private.interlaced = PNG_INTERLACE_NONE;
    }
    XPOST_LOG_INFO("PNG interlacing: %s",
                   (private.interlaced == PNG_INTERLACE_ADAM7) ? "Adam7" : "none");

    /* allocate buffer header and array */
    {
        size_t bytes;

        if (!xpost_device_raster_bytes(width, height,
                                       sizeof(Xpost_Png_Pixel),
                                       sizeof(Xpost_Png_Buffer), &bytes))
        {
            XPOST_LOG_ERR("%d a raster for a page of %dx%d is larger than"
                          " this platform addresses", limitcheck,
                          width, height);
            return limitcheck;
        }
        private.buf = malloc(bytes);
    }
    /* the size was one this platform expresses and addresses; whether
       the memory for it is there is the machine's answer, and a page the
       machine will not hold is a memory error rather than a limit of
       this interpreter */
    if (!private.buf)
    {
        XPOST_LOG_ERR("cannot allocate buffer memory");
        return VMerror;
    }
    private.buf->block = private.buf;
    private.bufowned = 1;

    /* the page starts opaque white; the alpha device starts fully
       transparent, so only marks made by the job carry opacity and an
       erased page is see-through */
    {
        Xpost_Dev_Raster_Offset i, n;
        Xpost_Png_Pixel init;

        n = (Xpost_Dev_Raster_Offset)width * (Xpost_Dev_Raster_Offset)height;
        init.red = init.green = init.blue = 255;
        init.alpha = private.alpha ? 0 : 255;
        for (i = 0; i < n; i++)
            private.buf->data[i] = init;
    }

    /* save private data struct in string */
    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
    {
        /* the record is the only thing that would have named the buffer,
           and it is not going to */
        free(private.buf);
        return unregistered;
    }

    /* return device instance dictionary to ps */
    xpost_stack_push(ctx->lo, ctx->os, devdic);
    return 0;
}

static
int _putpix(Xpost_Context *ctx,
            Xpost_Object red,
            Xpost_Object green,
            Xpost_Object blue,
            Xpost_Object x,
            Xpost_Object y,
            Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int r, g, b, ix, iy;

    /* fold numbers per the driver contract */
    r = xpost_dev_num_to_byte(red);
    g = xpost_dev_num_to_byte(green);
    b = xpost_dev_num_to_byte(blue);
    ix = xpost_dev_num_to_int(x);
    iy = xpost_dev_num_to_int(y);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released raster takes no marks: the recorded dimensions outlive
       the buffer, so the bounds check below does not stand in for this */
    if (!private.buf)
        return 0;

    /* check bounds */
    if ((ix < 0) || (ix >= private.width) ||
        (iy < 0) || (iy >= private.height))
        return 0;

    {
        Xpost_Png_Pixel pixel;
        pixel.blue = b;
        pixel.green = g;
        pixel.red = r;
        pixel.alpha = 255;
        private.buf->data[xpost_dev_raster_offset(ix, iy, private.width)]
            = pixel;
    }

    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;

    return 0;
}

/* Read a pixel back in the device's stored channel scale, the same one
   PutPix writes. The class this device copies reads the base class's
   row array, which this device does not have, so the inherited method
   would answer undefined; a slot the class dictionary offers has to
   work. A pixel outside the raster reads as the page's ground, and so
   does every pixel of an instance whose buffer has been released. The
   alpha device clears its page through /Erase and so records no ground;
   what it reads is the white that erase leaves under the transparency,
   which is the answer for a device that has none recorded. */
static
int _getpix(Xpost_Context *ctx,
            Xpost_Object x,
            Xpost_Object y,
            Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int ix, iy, r, g, b;

    ix = xpost_dev_num_to_int(x);
    iy = xpost_dev_num_to_int(y);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    if (!private.buf ||
        (ix < 0) || (ix >= private.width) ||
        (iy < 0) || (iy >= private.height))
        xpost_device_ground_channels(ctx, devdic, &r, &g, &b);
    else
    {
        Xpost_Png_Pixel pixel = private.buf->data
            [xpost_dev_raster_offset(ix, iy, private.width)];

        r = pixel.red; g = pixel.green; b = pixel.blue;
    }

    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(r));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(g));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(b));

    return 0;
}

/* A blend coverage as the fraction of full ink it is: 0 leaves the
   ground alone, 255 lays the colour down whole. The value is folded into
   that range because the source-over blend below only stays between its
   endpoints while the weight does: past 255 the composited opacity runs
   past full and wraps in the byte it is stored in, so a fully covered
   pixel comes out completely transparent. The generic rasteriser folds a
   coverage the same way. */
static int _coverage(Xpost_Object cov)
{
    int c = xpost_dev_num_to_int(cov);

    if (c < 0) return 0;
    if (c > 255) return 255;
    return c;
}

/* Blend a coverage-weighted pixel: each channel moves toward the colour
   by cov/255. The text operators use this for glyph edge pixels when the
   device renders anti-aliased text. */
static
int _blendpix(Xpost_Context *ctx,
              Xpost_Object red,
              Xpost_Object green,
              Xpost_Object blue,
              Xpost_Object cov,
              Xpost_Object x,
              Xpost_Object y,
              Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int r, g, b, c, ix, iy;

    r = xpost_dev_num_to_byte(red);
    g = xpost_dev_num_to_byte(green);
    b = xpost_dev_num_to_byte(blue);
    c = _coverage(cov);
    ix = xpost_dev_num_to_int(x);
    iy = xpost_dev_num_to_int(y);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released raster takes no marks */
    if (!private.buf)
        return 0;

    if ((ix < 0) || (ix >= private.width) ||
        (iy < 0) || (iy >= private.height))
        return 0;

    {
        Xpost_Png_Pixel *p = &private.buf->data
            [xpost_dev_raster_offset(ix, iy, private.width)];
        int da = p->alpha;
        int oa = c + (da * (255 - c) + 127) / 255;

        if (oa == 0)
            return 0;
        /* source over: the ink contributes c, the ground its own
           opacity of what c leaves uncovered */
        p->red   = (unsigned char)((r * c + p->red   * da * (255 - c) / 255 + oa / 2) / oa);
        p->green = (unsigned char)((g * c + p->green * da * (255 - c) / 255 + oa / 2) / oa);
        p->blue  = (unsigned char)((b * c + p->blue  * da * (255 - c) / 255 + oa / 2) / oa);
        p->alpha = (unsigned char)oa;
    }

    return 0;
}

/* C fast-path for the base-class PS FillRect: fills the buffer directly
   rather than looping over PutPix per pixel. The only caller is erasepage
   (full-page clear), which dominates page-emission time when done in PS. */
static
int _fillrect(Xpost_Context *ctx,
              Xpost_Object red,
              Xpost_Object green,
              Xpost_Object blue,
              Xpost_Object x,
              Xpost_Object y,
              Xpost_Object w,
              Xpost_Object h,
              Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    Xpost_Png_Pixel pixel;
    int ix, iy, x0, y0, x1, y1;

    /* fold numbers per the driver contract */
    pixel.red   = (unsigned char)xpost_dev_num_to_byte(red);
    pixel.green = (unsigned char)xpost_dev_num_to_byte(green);
    pixel.blue  = (unsigned char)xpost_dev_num_to_byte(blue);
    pixel.alpha = 255;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released raster takes no marks */
    if (!private.buf)
        return 0;

    /* the contract's rectangle: inclusive span, clipped to the device */
    xpost_dev_rect_normalize(xpost_object_number(x), xpost_object_number(y),
                             xpost_object_number(w), xpost_object_number(h),
                             &x0, &y0, &x1, &y1);
    if (!xpost_dev_rect_clip(&x0, &y0, &x1, &y1,
                             private.width, private.height))
        return 0;

    for (iy = y0; iy <= y1; iy++)
    {
        Xpost_Png_Pixel *row = private.buf->data
                             + xpost_dev_raster_offset(0, iy, private.width);
        for (ix = x0; ix <= x1; ix++)
            row[ix] = pixel;
    }

    return 0;
}

/* Write the page: one PNG image, whole, into the file settled for this
   page. The file and the writer are made here and finished here, so a
   job's second page is a second file rather than an append to a stream
   that holds one image. */
static
int _emit(Xpost_Context *ctx,
          Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    Xpost_Object ud;
    Xpost_Object compression_level_o;
    FILE *f;
    png_structp png_ptr;
    png_infop info_ptr = NULL;
    png_color_8 sig_bit;
    unsigned char *data;
    png_bytep row_ptr;
    int compression_level;
    int num_passes = 1;
    int pass;
    int y;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released instance has no raster to write */
    if (!private.buf)
        return 0;

    f = xpost_device_page_open(ctx, devdic);
    if (!f)
    {
        XPOST_LOG_ERR("cannot open the file this PNG page is written to");
        return ioerror;
    }

    ud = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    compression_level_o = xpost_dict_get(ctx, ud,
                                         xpost_name_cons(ctx, "png_compression_level"));
    if (xpost_object_get_type(compression_level_o) == invalidtype)
        compression_level = 3;
    else
        compression_level = compression_level_o.int_.val;
    XPOST_LOG_INFO("PNG compresion level: %d", compression_level);

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        xpost_device_page_close(f);
        return VMerror;
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, NULL);
        xpost_device_page_close(f);
        return VMerror;
    }

    /* libpng reports errors by longjmp: aim it at this call, which is
       also where everything it was given is released */
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        xpost_device_page_close(f);
        return ioerror;
    }

    png_init_io(png_ptr, f);
    png_set_IHDR(png_ptr, info_ptr,
                 private.width, private.height, 8,
                 private.alpha ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB,
                 private.interlaced,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    sig_bit.red = 8;
    sig_bit.green = 8;
    sig_bit.blue = 8;
    sig_bit.alpha = 8;
    png_set_sBIT(png_ptr, info_ptr, &sig_bit);

    png_set_compression_level(png_ptr, compression_level);
    png_write_info(png_ptr, info_ptr);
    png_set_shift(png_ptr, &sig_bit);
    png_set_packing(png_ptr);
    if (!private.alpha)
        /* rows carry a fourth byte per pixel; skip it when writing RGB */
        png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);

#ifdef PNG_WRITE_INTERLACING_SUPPORTED
    num_passes = png_set_interlace_handling(png_ptr);
#endif

    /* a row at a time, stepping by the bytes one row of the buffer
       holds: the step is counted in the width a size is expressed in,
       which a row of a wide page runs past when counted in an int */
    for (pass = 0; pass < num_passes; pass++)
    {
        data = (unsigned char *)private.buf->data;
        for (y = 0; y < private.height; y++)
        {
            row_ptr = (png_bytep)data;
            png_write_rows(png_ptr, &row_ptr, 1);
            data += (Xpost_Dev_Raster_Offset)private.width
                  * sizeof(Xpost_Png_Pixel);
        }
    }

    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    xpost_device_page_close(f);

    /* pass data back to client application; the raster then belongs to
       the client, which gives the block it sits in back through the
       release entry point, so Destroy must leave it alone from here on */
    if (xpost_dev_output_buffer_handoff(ctx, (unsigned char *)private.buf->data))
    {
        private.bufowned = 0;
        if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
            return VMerror;
    }

    return 0;
}

/* clear the page to fully transparent: the alpha device's erasepage.
   An explicit white fill stays opaque; only the page reset is clear. */
static
int _erase(Xpost_Context *ctx,
           Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    Xpost_Dev_Raster_Offset i, n;
    Xpost_Png_Pixel init;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released raster has no page to clear */
    if (!private.buf)
        return 0;

    init.red = init.green = init.blue = 255;
    init.alpha = 0;
    n = (Xpost_Dev_Raster_Offset)private.width
      * (Xpost_Dev_Raster_Offset)private.height;
    for (i = 0; i < n; i++)
        private.buf->data[i] = init;

    return 0;
}

static
int _destroy(Xpost_Context *ctx,
             Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* the raster is all the instance holds: each page's file and writer
       were finished as that page was written. A raster handed to the
       client is the client's to give back */
    if (private.bufowned)
        free(private.buf);
    private.buf = NULL;
    private.bufowned = 0;
    /* store the cleared pointer back so a repeated destroy is a no-op */
    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;
    return 0;
}

/* operator function to instantiate a new window device.
   installed in userdict by calling 'loadXXXdevice'.
 */
static
int newpngdevice(Xpost_Context *ctx,
                 Xpost_Object width,
                 Xpost_Object height)
{
    Xpost_Object classdic;
    int ret;

    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    ret = xpost_op_any_load(ctx, xpost_name_cons(ctx, "pngDEVICE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_dict_get(ctx, classdic, xpost_name_cons(ctx, "Create"))))
        return execstackoverflow;

    return 0;
}

static
int newpngalphadevice(Xpost_Context *ctx,
                      Xpost_Object width,
                      Xpost_Object height)
{
    Xpost_Object classdic;
    int ret;

    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    ret = xpost_op_any_load(ctx, xpost_name_cons(ctx, "pngalphaDEVICE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_dict_get(ctx, classdic, xpost_name_cons(ctx, "Create"))))
        return execstackoverflow;

    return 0;
}

static
unsigned int _loadpngdevicecont_opcode;
static
unsigned int _loadpngalphadevicecont_opcode;

/* Specializes or sub-classes the PPMIMAGE device class.
   load PPMIMAGE
   load and call ps procedure .copydict which leaves copy on stack
   call loadpngdevicecont by continuation.
 */
static
int loadpngdevice(Xpost_Context *ctx)
{
    Xpost_Object classdic;
    int ret;

    ret = xpost_op_privatedict_load(ctx, xpost_name_cons(ctx, ".xpost_PPMIMAGE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons_opcode(_loadpngdevicecont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_dict_get(ctx, classdic, namedotcopydict)))
        return execstackoverflow;

    return 0;
}

static
int loadpngalphadevice(Xpost_Context *ctx)
{
    Xpost_Object classdic;
    int ret;

    ret = xpost_op_privatedict_load(ctx, xpost_name_cons(ctx, ".xpost_PPMIMAGE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons_opcode(_loadpngalphadevicecont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_dict_get(ctx, classdic, namedotcopydict)))
        return execstackoverflow;

    return 0;
}

/* replace procedures in the class with newly created special operators.
   defines the device class (pngDEVICE or pngalphaDEVICE) in userdict
   and the matching newXXXdevice operator. The alpha class carries
   /AlphaChannel for Create and an /Erase method for erasepage. */
static
int _loaddevicecont_common(Xpost_Context *ctx,
                           Xpost_Object classdic,
                           int alpha)
{
    /* this device's method suite; the arities follow from DeviceRGB */
    static const Xpost_Dev_Method methods[] =
    {
        { "Create",   "pngCreate",   (Xpost_Op_Func)_create,   XPOST_DEV_M_CREATE },
        { "PutPix",   "pngPutPix",   (Xpost_Op_Func)_putpix,   XPOST_DEV_M_PUTPIX },
        { "GetPix",   "pngGetPix",   (Xpost_Op_Func)_getpix,   XPOST_DEV_M_GETPIX },
        { "FillRect", "pngFillRect", (Xpost_Op_Func)_fillrect, XPOST_DEV_M_RECT   },
        { "BlendPix", "pngBlendPix", (Xpost_Op_Func)_blendpix, XPOST_DEV_M_BLEND  },
        { "Emit",     "pngEmit",     (Xpost_Op_Func)_emit,     XPOST_DEV_M_PAGE   },
        { "Destroy",  "pngDestroy",  (Xpost_Op_Func)_destroy,  XPOST_DEV_M_PAGE   }
    };
    /* the alpha device clears to transparent rather than to white, so
       it answers erasepage itself */
    static const Xpost_Dev_Method alphamethods[] =
    {
        { "Erase", "pngErase", (Xpost_Op_Func)_erase, XPOST_DEV_M_PAGE }
    };

    Xpost_Object userdict;
    Xpost_Object op;
    int ret;

    ret = xpost_dict_put(ctx, classdic, namenativecolorspace, nameDeviceRGB);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "pngCreateCont", (Xpost_Op_Func)_create_cont, 3, integertype, integertype, dicttype);
    _create_cont_opcode = op.mark_.padw;

    ret = xpost_dev_class_install(ctx, classdic, 3, 1,
                                  methods, XPOST_DEV_METHOD_COUNT(methods));
    if (ret)
        return ret;

    if (alpha)
    {
        ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "AlphaChannel"), xpost_bool_cons(1));
        if (ret)
            return ret;
        ret = xpost_dev_class_install(ctx, classdic, 3, 1, alphamethods,
                                      XPOST_DEV_METHOD_COUNT(alphamethods));
        if (ret)
            return ret;
    }

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);

    ret = xpost_dict_put(ctx, userdict,
                         xpost_name_cons(ctx, alpha ? "pngalphaDEVICE" : "pngDEVICE"),
                         classdic);
    if (ret)
        return ret;

    if (alpha)
        op = xpost_operator_cons(ctx, "newpngalphadevice", (Xpost_Op_Func)newpngalphadevice, 2, integertype, integertype);
    else
        op = xpost_operator_cons(ctx, "newpngdevice", (Xpost_Op_Func)newpngdevice, 2, integertype, integertype);
    ret = xpost_dict_put(ctx, userdict,
                         xpost_name_cons(ctx, alpha ? "newpngalphadevice" : "newpngdevice"),
                         op);
    if (ret)
        return ret;

    return 0;
}

static
int loadpngdevicecont(Xpost_Context *ctx,
                      Xpost_Object classdic)
{
    return _loaddevicecont_common(ctx, classdic, 0);
}

static
int loadpngalphadevicecont(Xpost_Context *ctx,
                           Xpost_Object classdic)
{
    return _loaddevicecont_common(ctx, classdic, 1);
}

/*
   install the loadXXXdevice which may be called during graphics initialization
   to produce the operator newXXXdevice which instantiates the device dictionary.
*/
int xpost_oper_init_png_device_ops(Xpost_Context *ctx,
                                   Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;

    /* factor-out name lookups from the operators (optimization) */
    if (xpost_object_get_type((namePrivate = xpost_name_cons(ctx, "Private"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namewidth = xpost_name_cons(ctx, "width"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameheight = xpost_name_cons(ctx, "height"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namedotcopydict = xpost_name_cons(ctx, ".copydict"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namenativecolorspace = xpost_name_cons(ctx, "nativecolorspace"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameDeviceRGB = xpost_name_cons(ctx, "DeviceRGB"))) == invalidtype)
        return VMerror;

    optab = xpost_operator_table(ctx->gl);
    op = xpost_operator_cons(ctx, "loadpngdevice", (Xpost_Op_Func)loadpngdevice, 0); INSTALL;
    op = xpost_operator_cons(ctx, "loadpngdevicecont", (Xpost_Op_Func)loadpngdevicecont, 1, dicttype);
    _loadpngdevicecont_opcode = op.mark_.padw;
    op = xpost_operator_cons(ctx, "loadpngalphadevice", (Xpost_Op_Func)loadpngalphadevice, 0); INSTALL;
    op = xpost_operator_cons(ctx, "loadpngalphadevicecont", (Xpost_Op_Func)loadpngalphadevicecont, 1, dicttype);
    _loadpngalphadevicecont_opcode = op.mark_.padw;

    return 0;
}

XPAPI void
xpost_dev_png_options_set(Xpost_Context *ctx,
                          int compression_level,
                          int interlaced)
{
    char buf1[32];
    char buf2[32];
    char *defs[2];

    if ((compression_level < 0) || (compression_level > 9))
    {
        XPOST_LOG_ERR("wrong compression level for the PNG device (%d)",
                      compression_level);
        return;
    }

    snprintf(buf1, sizeof(buf1),
             "png_compression_level=%d", compression_level);
    snprintf(buf2, sizeof(buf2),
             "png_interlaced=%d", interlaced ? 1 : 0);
    defs[0] = buf1;
    defs[1] = buf2;
    xpost_add_definitions(ctx, 2, defs);
}

#else /* ! HAVE_LIBPNG */

#include "xpost.h"

XPAPI void
xpost_dev_png_options_set(Xpost_Context *ctx,
                          int compression_level,
                          int interlaced)
{
    (void)ctx;
    (void)compression_level;
    (void)interlaced;
}

#endif
