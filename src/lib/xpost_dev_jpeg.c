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

#ifdef HAVE_STDLIB_H
# undef HAVE_STDLIB_H
#endif

#ifdef HAVE_LIBJPEG

#include <stddef.h> /* offsetof */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jpeglib.h>
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
#include "xpost_dev_jpeg.h" /* check prototypes */

typedef struct _JPEG_error_mgr *emptr;
struct _JPEG_error_mgr
{
   struct jpeg_error_mgr pub;
   jmp_buf setjmp_buffer;
};

typedef struct
{
    unsigned char red, green, blue;
} Xpost_Jpeg_Pixel;

typedef struct
{
    int width, height, byte_stride;
    /* the block this raster is part of. A client is handed the raster
       and gives the block back, so the block's own address is kept
       here, immediately before the raster, where the release entry
       point reads it. */
    void *block;
    Xpost_Jpeg_Pixel data[1];
} Xpost_Jpeg_Buffer;

/* Say that the block's address is immediately before the raster rather
   than leave it to hold by luck: the release entry point reaches it by
   stepping one pointer back from the address the client holds. (A
   negative array size rather than _Static_assert: this builds as C99
   with -pedantic-errors, which rejects the latter.) */
typedef char xpost_jpeg_block_precedes_the_raster[
    offsetof(Xpost_Jpeg_Buffer, data)
    == offsetof(Xpost_Jpeg_Buffer, block) + sizeof(void *) ? 1 : -1];

/* A JPEG stream holds exactly one image, so the file a page is
   compressed into belongs to the page and not to the device: it is
   opened when a page is emitted and closed before that emit returns.
   What the instance keeps between pages is the raster. */
typedef struct
{
    int width;
    int height;
    /*
     * add additional members to private struct
     */
    Xpost_Jpeg_Buffer *buf;
    /* the device allocated buf and has not handed it to the client
       through OutputBufferOut, so Destroy frees it */
    int bufowned;
} PrivateData;

static Xpost_Object namePrivate;
static Xpost_Object namewidth;
static Xpost_Object nameheight;
static Xpost_Object namedotcopydict;
static Xpost_Object namenativecolorspace;
static Xpost_Object nameDeviceRGB;


static unsigned int _create_cont_opcode;

static void
_JPEGFatalErrorHandler(j_common_ptr cinfo)
{
   emptr errmgr;

   errmgr = (emptr) cinfo->err;
   longjmp(errmgr->setjmp_buffer, 1);
   return;
}

/* The library's message emitters, replaced so that it writes nothing to
   the process's error stream: what it has to say about an image arrives
   through the fatal handler above, which longjmps back to the caller. */
static void
_JPEGErrorHandler(j_common_ptr cinfo)
{
   (void)cinfo;
}

static void
_JPEGErrorHandler2(j_common_ptr cinfo, int msg_level)
{
   (void)cinfo;
   (void)msg_level;
}

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

    /*
     *
     * initialize additional members of private struct
     *
     */

    /* allocate buffer header and array */
    {
        size_t bytes;

        if (!xpost_device_raster_bytes(width, height,
                                       sizeof(Xpost_Jpeg_Pixel),
                                       sizeof(Xpost_Jpeg_Buffer), &bytes))
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

    /* the page starts white; this format carries no transparency, so a
       pixel the job never marks is written out as it stands here */
    {
        Xpost_Dev_Raster_Offset i, n;
        Xpost_Jpeg_Pixel init;

        n = (Xpost_Dev_Raster_Offset)width * (Xpost_Dev_Raster_Offset)height;
        init.red = init.green = init.blue = 255;
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

/* One channel of a coverage-weighted blend: the ground moved toward the
   ink by the fraction c/255, rounded to the nearest whole level. Rounding
   is about a distance and has no sign, so the half step is taken away
   from zero at both ends -- C division truncates toward zero, and a
   half added regardless of direction rounds a darkening step the short
   way, leaving full ink over the opposite ground a level short of it. */
static int _blendchannel(int dst, int src, int c)
{
    int d = (src - dst) * c;

    return dst + (d < 0 ? (d - 127) / 255 : (d + 127) / 255);
}

/* Blend a coverage-weighted pixel: each channel moves toward the colour
   by cov/255. The text operators use this for the partly covered pixels
   at a glyph's edges, and a device without it inherits the base class's,
   which blends into a raster held as PostScript arrays -- this device
   keeps its pixels in a buffer of its own instead, so it needs its own.
*/
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
    c = xpost_dev_num_to_int(cov);
    ix = xpost_dev_num_to_int(x);
    iy = xpost_dev_num_to_int(y);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released raster takes no marks: the recorded dimensions outlive
       the buffer, so the bounds check below does not stand in for this */
    if (!private.buf)
        return 0;

    if ((ix < 0) || (ix >= private.width) ||
        (iy < 0) || (iy >= private.height))
        return 0;

    if (c <= 0)
        return 0;
    if (c > 255)
        c = 255;

    {
        Xpost_Jpeg_Pixel *p = &private.buf->data
            [xpost_dev_raster_offset(ix, iy, private.width)];

        p->red = (unsigned char)_blendchannel(p->red, r, c);
        p->green = (unsigned char)_blendchannel(p->green, g, c);
        p->blue = (unsigned char)_blendchannel(p->blue, b, c);
    }

    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;

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

    /* a released raster takes no marks */
    if (!private.buf)
        return 0;

    /* check bounds */
    if ((ix < 0) || (ix >= private.width) ||
        (iy < 0) || (iy >= private.height))
        return 0;

    {
        Xpost_Jpeg_Pixel pixel;
        pixel.blue = b;
        pixel.green = g;
        pixel.red = r;
        private.buf->data[xpost_dev_raster_offset(ix, iy, private.width)]
            = pixel;
    }

    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;

    return 0;
}

/* Fill the buffer directly rather than through a loop over PutPix.

   A device that does not offer this takes the base class's, which walks
   the rectangle a pixel at a time and reaches the buffer through the
   operator dispatch for each of them. Every page begins with an
   erasepage over the whole of it, so that walk costs the page's own area
   in dispatches before a program has drawn anything.

   The rectangle is the contract's: an inclusive span, normalised and
   clipped to the device by the shared helpers, so a rectangle given
   inside out or reaching past an edge covers what the other devices
   cover. */
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
    Xpost_Jpeg_Pixel pixel;
    int ix, iy, x0, y0, x1, y1;

    pixel.red   = (unsigned char)xpost_dev_num_to_byte(red);
    pixel.green = (unsigned char)xpost_dev_num_to_byte(green);
    pixel.blue  = (unsigned char)xpost_dev_num_to_byte(blue);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released raster takes no marks */
    if (!private.buf)
        return 0;

    xpost_dev_rect_normalize(xpost_object_number(x), xpost_object_number(y),
                             xpost_object_number(w), xpost_object_number(h),
                             &x0, &y0, &x1, &y1);
    if (!xpost_dev_rect_clip(&x0, &y0, &x1, &y1,
                             private.width, private.height))
        return 0;

    for (iy = y0; iy <= y1; iy++)
    {
        Xpost_Jpeg_Pixel *row = private.buf->data
                              + xpost_dev_raster_offset(0, iy, private.width);

        for (ix = x0; ix <= x1; ix++)
            row[ix] = pixel;
    }

    return 0;
}

/* Read a pixel back in the device's stored channel scale, the same one
   PutPix writes. The class this device copies reads the base class's
   row array, which this device does not have, so the inherited method
   would answer undefined; a slot the class dictionary offers has to
   work. A pixel outside the raster reads as the page's ground, and so
   does every pixel of an instance whose buffer has been released. */
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
        Xpost_Jpeg_Pixel pixel = private.buf->data
            [xpost_dev_raster_offset(ix, iy, private.width)];

        r = pixel.red; g = pixel.green; b = pixel.blue;
    }

    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(r));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(g));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(b));

    return 0;
}

/* Write the page: one JPEG image, whole, into the file settled for this
   page. The file is opened here and closed here, so a job's second page
   is a second file rather than an append to a stream that holds one
   image. */
static
int _emit(Xpost_Context *ctx,
          Xpost_Object devdic)
{
    struct jpeg_compress_struct cinfo;
    struct _JPEG_error_mgr jerr;
    Xpost_Object ud;
    Xpost_Object quality_o;
    Xpost_Object privatestr;
    PrivateData private;
    FILE *f;
    unsigned char *data;
    JSAMPROW *jbuf;
    int quality;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released instance has no raster to compress */
    if (!private.buf)
        return 0;

    f = xpost_device_page_open(ctx, devdic);
    if (!f)
    {
        XPOST_LOG_ERR("cannot open the file this JPEG page is written to");
        return ioerror;
    }

    ud = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    quality_o = xpost_dict_get(ctx, ud, xpost_name_cons(ctx, "jpeg_quality"));

    if (xpost_object_get_type(quality_o) == invalidtype)
        quality = 90;
    else
        quality = quality_o.int_.val;
    XPOST_LOG_INFO("JPEG quality: %d", quality);

    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&(jerr.pub));
    jerr.pub.error_exit = _JPEGFatalErrorHandler;
    jerr.pub.emit_message = _JPEGErrorHandler2;
    jerr.pub.output_message = _JPEGErrorHandler;
    if (setjmp(jerr.setjmp_buffer))
    {
        jpeg_destroy_compress(&cinfo);
        xpost_device_page_close(f);
        return undefined;
    }
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, f);
    cinfo.image_width = private.width;
    cinfo.image_height = private.height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    cinfo.optimize_coding = FALSE;
    cinfo.dct_method = JDCT_ISLOW; /* JDCT_FLOAT JDCT_IFAST(quality loss) */
    if (quality < 60)
        cinfo.dct_method = JDCT_IFAST;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    if (quality >= 90)
    {
        cinfo.comp_info[0].h_samp_factor = 1;
        cinfo.comp_info[0].v_samp_factor = 1;
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;
    }
    jpeg_start_compress(&cinfo, TRUE);
    /* a row at a time, stepping by the bytes one row of the buffer
       holds: the step is counted in the width a size is expressed in,
       which a row of a wide page runs past when counted in an int */
    data = (unsigned char *)private.buf->data;
    while (cinfo.next_scanline < cinfo.image_height)
    {
        jbuf = (JSAMPROW *) (&data);
        jpeg_write_scanlines(&cinfo, jbuf, 1);
        data += (Xpost_Dev_Raster_Offset)private.width
              * sizeof(Xpost_Jpeg_Pixel);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
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

static
int _destroy(Xpost_Context *ctx,
             Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* the raster is all the instance holds: each page's file was closed
       as that page was written. A raster handed to the client is the
       client's to give back */
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
int newjpegdevice(Xpost_Context *ctx,
                  Xpost_Object width,
                  Xpost_Object height)
{
    Xpost_Object classdic;
    int ret;

    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    ret = xpost_op_any_load(ctx, xpost_name_cons(ctx, "jpegDEVICE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_dict_get(ctx, classdic, xpost_name_cons(ctx, "Create"))))
        return execstackoverflow;

    return 0;
}

static
unsigned int _loadjpegdevicecont_opcode;

/* Specializes or sub-classes the .xpost_PPMIMAGE device class.
   load .xpost_PPMIMAGE
   load and call ps procedure .copydict which leaves copy on stack
   call loadjpegdevicecont by continuation.
 */
static
int loadjpegdevice(Xpost_Context *ctx)
{
    Xpost_Object classdic;
    int ret;

    ret = xpost_op_privatedict_load(ctx, xpost_name_cons(ctx, ".xpost_PPMIMAGE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons_opcode(_loadjpegdevicecont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_dict_get(ctx, classdic, namedotcopydict)))
        return execstackoverflow;

    return 0;
}

/* replace procedures in the class with newly created special operators.
   defines the device class jpegDEVICE in userdict.
   defines a new operator in userdict: newjpegdevice
 */
static
int loadjpegdevicecont(Xpost_Context *ctx,
                      Xpost_Object classdic)
{
    /* this device's method suite; the arities follow from its
       declared colour space */
    static const Xpost_Dev_Method methods[] =
    {
        { "Create", "jpegCreate", (Xpost_Op_Func)_create, XPOST_DEV_M_CREATE },
        { "PutPix", "jpegPutPix", (Xpost_Op_Func)_putpix, XPOST_DEV_M_PUTPIX },
        { "FillRect", "jpegFillRect", (Xpost_Op_Func)_fillrect, XPOST_DEV_M_RECT },
        { "GetPix", "jpegGetPix", (Xpost_Op_Func)_getpix, XPOST_DEV_M_GETPIX },
        { "BlendPix", "jpegBlendPix", (Xpost_Op_Func)_blendpix, XPOST_DEV_M_BLEND },
        { "Emit", "jpegEmit", (Xpost_Op_Func)_emit, XPOST_DEV_M_PAGE },
        { "Destroy", "jpegDestroy", (Xpost_Op_Func)_destroy, XPOST_DEV_M_PAGE }
    };

    Xpost_Object userdict;
    Xpost_Object op;
    int ret;

    ret = xpost_dict_put(ctx, classdic, namenativecolorspace, nameDeviceRGB);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "jpegCreateCont", (Xpost_Op_Func)_create_cont, 3, integertype, integertype, dicttype);
    _create_cont_opcode = op.mark_.padw;

    ret = xpost_dev_class_install(ctx, classdic, 3, 1,
                                  methods, XPOST_DEV_METHOD_COUNT(methods));
    if (ret)
        return ret;







    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);

    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "jpegDEVICE"), classdic);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "newjpegdevice", (Xpost_Op_Func)newjpegdevice, 2, integertype, integertype);
    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "newjpegdevice"), op);
    if (ret)
        return ret;

    return 0;
}

/*
   install the loadXXXdevice which may be called during graphics initialization
   to produce the operator newXXXdevice which instantiates the device dictionary.
*/
int xpost_oper_init_jpeg_device_ops(Xpost_Context *ctx,
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
    op = xpost_operator_cons(ctx, "loadjpegdevice", (Xpost_Op_Func)loadjpegdevice, 0); INSTALL;
    op = xpost_operator_cons(ctx, "loadjpegdevicecont", (Xpost_Op_Func)loadjpegdevicecont, 1, dicttype);
    _loadjpegdevicecont_opcode = op.mark_.padw;

    return 0;
}

XPAPI void
xpost_dev_jpeg_options_set(Xpost_Context *ctx, int quality)
{
    char buf[32];
    char *def[1];

    if ((quality < 0) || (quality > 100))
    {
        XPOST_LOG_ERR("wrong quality value for the JPEG device (%d)",
                      quality);
        return;
    }

    snprintf(buf, sizeof(buf), "jpeg_quality=%d", quality);
    def[0] = buf;
    xpost_add_definitions(ctx, 1, def);
}

#else /* ! HAVE_LIBJPEG */

#include "xpost.h"

XPAPI void
xpost_dev_jpeg_options_set(Xpost_Context *ctx, int quality)
{
    (void)ctx;
    (void)quality;
}

#endif
