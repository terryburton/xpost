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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <stdlib.h> /* abs */
//#include <stdio.h>
#include <string.h>

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
#include "xpost_dev_driver.h" /* device contract and shared helpers */
#include "xpost_dev_generic.h"
#include "xpost_dev_bgr.h" /* check prototypes */

typedef struct
{
    unsigned char blue, green, red;
} Xpost_Bgr_Pixel;

typedef struct
{
    int width, height, byte_stride;
    Xpost_Bgr_Pixel data[1];
} Xpost_Bgr_Buffer;

typedef struct
{
    int width, height;
    /*
     * add additional members to private struct
     */
    Xpost_Bgr_Buffer *buf;
    int bufowned; /* the device malloc'd buf and has not handed it to the
                     client through OutputBufferOut, so Destroy frees it */
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

    //printf("create\n");
    //fflush(0);
    /* call device class's ps-level .copydict procedure,
       //call base-class's Create procedure (to initialize ImgData array)
       then call _create_cont, by continuation. */
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_operator_cons_opcode(_create_cont_opcode)))
        return execstackoverflow;

    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic, namedotcopydict)))
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
    Xpost_Object privatestr;
    PrivateData private;
    integer width = w.int_.val;
    integer height = h.int_.val;
    int ret;
    //printf("create_cont\n");

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

    {
        /* allocate buffer header and array */
        {
            size_t bytes;

            if (!xpost_device_raster_bytes(width, height,
                                           sizeof(Xpost_Bgr_Pixel), &bytes))
            {
                XPOST_LOG_ERR("%d a page of %dx%d has more pixels than a"
                              " raster can be indexed by", limitcheck,
                              (int)width, (int)height);
                return limitcheck;
            }
            private.buf = malloc(sizeof(Xpost_Bgr_Buffer) + bytes);
        }
        if (!private.buf)
            return VMerror;
        private.bufowned = 1;
    }

    /* save private data struct in string */
    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
    {
        /* the record is the only thing that would have named the
           buffer, and it is not going to */
        if (private.bufowned)
            free(private.buf);
        return VMerror;
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
        Xpost_Bgr_Pixel *p = &private.buf->data[(size_t)iy * private.width + ix];

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
        Xpost_Bgr_Pixel pixel;
        pixel.blue = b;
        pixel.green = g;
        pixel.red = r;
        private.buf->data[(size_t)iy * private.width + ix] = pixel;
    }

    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;

    return 0;
}

/* Fill the buffer directly rather than through a loop over PutPix.

   A device that does not offer this takes the base class's, which walks
   the rectangle a pixel at a time and reaches the buffer through the
   operator dispatch for each of them. Every page begins with an
   erasepage over the whole of it, so that walk is the page's own area in
   dispatches before a program has drawn anything.

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
    Xpost_Bgr_Pixel pixel;
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
        Xpost_Bgr_Pixel *row = private.buf->data + (size_t)iy * private.width;

        for (ix = x0; ix <= x1; ix++)
            row[ix] = pixel;
    }

    return 0;
}

/* Read a pixel back in the device's stored channel scale, the same one
   PutPix writes. The class this device copies reads the base class's
   row array, which this device does not have, so the inherited method
   would answer undefined; a slot the class dictionary offers has to
   work. A pixel outside the raster reads as the ground, and so does
   every pixel of an instance whose buffer has been released. */
static
int _getpix(Xpost_Context *ctx,
            Xpost_Object x,
            Xpost_Object y,
            Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int ix, iy;
    Xpost_Bgr_Pixel pixel;

    ix = xpost_dev_num_to_int(x);
    iy = xpost_dev_num_to_int(y);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    if (!private.buf ||
        (ix < 0) || (ix >= private.width) ||
        (iy < 0) || (iy >= private.height))
        pixel.red = pixel.green = pixel.blue = 0;
    else
        pixel = private.buf->data[(size_t)iy * private.width + ix];

    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(pixel.red));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(pixel.green));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(pixel.blue));

    return 0;
}

static
int _flush(Xpost_Context *ctx,
           Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    return 0;
}


static
int _emit(Xpost_Context *ctx,
          Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* a released raster has nothing left to emit */
    if (!private.buf)
        return 0;

    /* pass data back to client application; the buffer then belongs to
       the client (the API documents the handed-out buffer as the
       caller's to free): Destroy must leave it alone from here on */
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

    if (private.buf && private.bufowned)
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
int newbgrdevice(Xpost_Context *ctx,
                 Xpost_Object width,
                 Xpost_Object height)
{
    Xpost_Object classdic;
    int ret;

    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    ret = xpost_op_any_load(ctx, xpost_name_cons(ctx, "bgrDEVICE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic, xpost_name_cons(ctx, "Create"))))
        return execstackoverflow;

    return 0;
}

static
unsigned int _loadbgrdevicecont_opcode;

/* Specializes or sub-classes the .xpost_PPMIMAGE device class.
   load .xpost_PPMIMAGE
   load and call ps procedure .copydict which leaves copy on stack
   call loadbgrdevicecont by continuation.
 */
static
int loadbgrdevice(Xpost_Context *ctx)
{
    Xpost_Object classdic;
    int ret;

    ret = xpost_op_privatedict_load(ctx, xpost_name_cons(ctx, ".xpost_PPMIMAGE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_operator_cons_opcode(_loadbgrdevicecont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic, namedotcopydict)))
        return execstackoverflow;

    return 0;
}

/* replace procedures in the class with newly created special operators.
   defines the device class bgrDEVICE in userdict.
   defines a new operator in userdict: newbgrdevice
 */
static
int loadbgrdevicecont(Xpost_Context *ctx,
                      Xpost_Object classdic)
{
    /* this device's method suite; the arities follow from its
       declared colour space */
    static const Xpost_Dev_Method methods[] =
    {
        { "Create", "bgrCreate", (Xpost_Op_Func)_create, XPOST_DEV_M_CREATE },
        { "PutPix", "bgrPutPix", (Xpost_Op_Func)_putpix, XPOST_DEV_M_PUTPIX },
        { "FillRect", "bgrFillRect", (Xpost_Op_Func)_fillrect, XPOST_DEV_M_RECT },
        { "GetPix", "bgrGetPix", (Xpost_Op_Func)_getpix, XPOST_DEV_M_GETPIX },
        { "BlendPix", "bgrBlendPix", (Xpost_Op_Func)_blendpix, XPOST_DEV_M_BLEND },
        { "Emit", "bgrEmit", (Xpost_Op_Func)_emit, XPOST_DEV_M_PAGE },
        { "Flush", "bgrFlush", (Xpost_Op_Func)_flush, XPOST_DEV_M_PAGE },
        { "Destroy", "bgrDestroy", (Xpost_Op_Func)_destroy, XPOST_DEV_M_PAGE }
    };

    Xpost_Object userdict;
    Xpost_Object op;
    int ret;

    ret = xpost_dict_put(ctx, classdic, namenativecolorspace, nameDeviceRGB);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "bgrCreateCont", (Xpost_Op_Func)_create_cont, 3, integertype, integertype, dicttype);
    _create_cont_opcode = op.mark_.padw;

    ret = xpost_dev_class_install(ctx, classdic, 3, 1,
                                  methods, XPOST_DEV_METHOD_COUNT(methods));
    if (ret)
        return ret;








    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);

    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "bgrDEVICE"), classdic);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "newbgrdevice", (Xpost_Op_Func)newbgrdevice, 2, integertype, integertype);
    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "newbgrdevice"), op);
    if (ret)
        return ret;

    return 0;
}

/*
   install the loadXXXdevice which may be called during graphics initialization
   to produce the operator newXXXdevice which instantiates the device dictionary.
*/
int xpost_oper_init_bgr_device_ops(Xpost_Context *ctx,
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
    op = xpost_operator_cons(ctx, "loadbgrdevice", (Xpost_Op_Func)loadbgrdevice, 0); INSTALL;
    op = xpost_operator_cons(ctx, "loadbgrdevicecont", (Xpost_Op_Func)loadbgrdevicecont, 1, dicttype);
    _loadbgrdevicecont_opcode = op.mark_.padw;

    return 0;
}
