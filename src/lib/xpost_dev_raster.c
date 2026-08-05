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
#include <stdio.h>  /* FIXME: remove once printf() is removed */
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
#include "xpost_dev_raster.h" /* check prototypes */

enum Xpost_PixelFormat { RGB, ARGB, BGR, BGRA };

typedef struct
{
    unsigned char blue, green, red, alpha;
} Xpost_Raster_BGRA_Pixel;

typedef struct
{
    unsigned char blue, green, red;
} Xpost_Raster_BGR_Pixel;

typedef struct
{
    unsigned char red, green, blue;
} Xpost_Raster_RGB_Pixel;

typedef struct
{
    unsigned char alpha, red, green, blue;
} Xpost_Raster_ARGB_Pixel;

typedef struct
{
    int width, height, byte_stride;
    /*(Xpost_Raster_*_Pixel)*/ char *data[1];
} Xpost_Raster_Buffer;

typedef struct
{
    int width, height;
    enum Xpost_PixelFormat pixelformat;
    /*
     * add additional members to private struct
     */
    Xpost_Raster_Buffer *buf;
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
    Xpost_Object sd;
    Xpost_Object subdevice;
    Xpost_Object privatestr;
    PrivateData private;
    integer width = w.int_.val;
    integer height = h.int_.val;
    Xpost_Object inbufstr;
    int ret;
    //printf("create_cont\n");

    sd = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);
    subdevice = xpost_dict_get(ctx, sd, xpost_name_cons(ctx, "SUBDEVICE"));
    if (xpost_object_get_type(subdevice) == invalidtype)
    {
        subdevice = xpost_string_cons(ctx, sizeof("rgb") - 1, "rgb");
    }
    XPOST_LOG_INFO("</SUBDEVICE %*s>", subdevice.comp_.sz, xpost_string_get_pointer(ctx, subdevice));
    {
        /* The name is compared against its own length: a shorter one
           read as though it were four bytes long would take whatever
           follows it in memory with it. A name that matches none of
           them leaves the format the one the device takes when no name
           is given at all, rather than leaving it unset. */
        const char *sub = xpost_string_get_pointer(ctx, subdevice);
        unsigned int sublen = subdevice.comp_.sz;

        private.pixelformat = RGB;
        if ((sublen == 4) && (memcmp(sub, "argb", 4) == 0))
            private.pixelformat = ARGB;
        else if ((sublen == 3) && (memcmp(sub, "rgb", 3) == 0))
            private.pixelformat = RGB;
        else if ((sublen == 4) && (memcmp(sub, "bgra", 4) == 0))
            private.pixelformat = BGRA;
        else if ((sublen == 3) && (memcmp(sub, "bgr", 3) == 0))
            private.pixelformat = BGR;
    }

    /* create a string to contain device data structure */
    privatestr = xpost_string_cons(ctx, sizeof(PrivateData), NULL);
    if (xpost_object_get_type(privatestr) == invalidtype)
    {
        XPOST_LOG_ERR("cannot allocat private data structure");
        return unregistered;
    }
    ret = xpost_dict_put(ctx, devdic, namePrivate, privatestr);
    if (ret)
        return ret;

    private.width = width;
    private.height = height;

    /*
     *
     * initialize additional members of private struct
     *
     */

    inbufstr = xpost_dict_get(ctx, sd, xpost_name_cons(ctx, "OutputBufferIn"));
    if (xpost_object_get_type(inbufstr) == stringtype)
    {
        unsigned char *inbuf;

        memcpy(&inbuf, xpost_string_get_pointer(ctx, inbufstr), sizeof(inbuf));
        private.buf = (Xpost_Raster_Buffer *)inbuf;
        private.bufowned = 0; /* the client's memory, never ours to free */
    }
    else
    {
        /* allocate buffer header and array */
        switch(private.pixelformat)
        {
            default:
                return unregistered;
            case ARGB:
                private.buf = malloc(sizeof(Xpost_Raster_Buffer) +
                                     sizeof(Xpost_Raster_ARGB_Pixel) * width * height);
                break;
            case RGB:
                private.buf = malloc(sizeof(Xpost_Raster_Buffer) +
                                     sizeof(Xpost_Raster_RGB_Pixel) * width * height);
                break;
            case BGRA:
                private.buf = malloc(sizeof(Xpost_Raster_Buffer) +
                                     sizeof(Xpost_Raster_BGRA_Pixel) * width * height);
                break;
            case BGR:
                private.buf = malloc(sizeof(Xpost_Raster_Buffer) +
                                     sizeof(Xpost_Raster_BGR_Pixel) * width * height);
                break;
        }
        private.buf->height = height;
        private.buf->width = width;
        private.bufowned = 1;
    }

    /* save private data struct in string */
    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;

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

    /* check bounds */
    if (ix < 0 || ix >= xpost_dict_get(ctx, devdic, namewidth).int_.val)
        return 0;
    if (iy < 0 || iy >= xpost_dict_get(ctx, devdic, nameheight).int_.val)
        return 0;

    switch(private.pixelformat)
    {
        case BGRA:
        {
            Xpost_Raster_BGRA_Pixel pixel;

            pixel.blue = b;
            pixel.green = g;
            pixel.red = r;
            pixel.alpha = 255;
            ((Xpost_Raster_BGRA_Pixel*)private.buf->data)[iy * private.buf->width + ix] = pixel;
        }
        break;
        case BGR:
        {
            Xpost_Raster_BGR_Pixel pixel;

            pixel.blue = b;
            pixel.green = g;
            pixel.red = r;
            ((Xpost_Raster_BGR_Pixel*)private.buf->data)[iy * private.buf->width + ix] = pixel;
        }
        break;
        case ARGB:
        {
            Xpost_Raster_ARGB_Pixel pixel;

            pixel.alpha = 255;
            pixel.red = r;
            pixel.green = g;
            pixel.blue = b;
            ((Xpost_Raster_ARGB_Pixel*)private.buf->data)[iy * private.buf->width + ix] = pixel;
        }
        break;
        case RGB:
        {
            Xpost_Raster_RGB_Pixel pixel;

            pixel.red = r;
            pixel.green = g;
            pixel.blue = b;
            ((Xpost_Raster_RGB_Pixel*)private.buf->data)[iy * private.buf->width + ix] = pixel;
        }
        break;
    }

    /* save private data struct in string */
    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;

    return 0;
}

/* Read a pixel back in the device's stored channel scale, the same one
   PutPix writes, whichever of the four pixel layouts the buffer was
   created with. The class this device copies reads the base class's row
   array, which this device does not have, so the inherited method would
   answer undefined; a slot the class dictionary offers has to work. A
   pixel outside the raster reads as the ground. */
static
int _getpix(Xpost_Context *ctx,
            Xpost_Object x,
            Xpost_Object y,
            Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int ix, iy;
    int r = 0, g = 0, b = 0;

    ix = xpost_dev_num_to_int(x);
    iy = xpost_dev_num_to_int(y);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    if (ix >= 0 && ix < private.width && iy >= 0 && iy < private.height)
    {
        int i = iy * private.buf->width + ix;

        switch (private.pixelformat)
        {
            case BGRA:
            {
                Xpost_Raster_BGRA_Pixel p =
                    ((Xpost_Raster_BGRA_Pixel *)private.buf->data)[i];
                r = p.red; g = p.green; b = p.blue;
            }
            break;
            case BGR:
            {
                Xpost_Raster_BGR_Pixel p =
                    ((Xpost_Raster_BGR_Pixel *)private.buf->data)[i];
                r = p.red; g = p.green; b = p.blue;
            }
            break;
            case ARGB:
            {
                Xpost_Raster_ARGB_Pixel p =
                    ((Xpost_Raster_ARGB_Pixel *)private.buf->data)[i];
                r = p.red; g = p.green; b = p.blue;
            }
            break;
            case RGB:
            {
                Xpost_Raster_RGB_Pixel p =
                    ((Xpost_Raster_RGB_Pixel *)private.buf->data)[i];
                r = p.red; g = p.green; b = p.blue;
            }
            break;
        }
    }

    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(r));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(g));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(b));

    return 0;
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

    if (ix < 0 || ix >= xpost_dict_get(ctx, devdic, namewidth).int_.val)
        return 0;
    if (iy < 0 || iy >= xpost_dict_get(ctx, devdic, nameheight).int_.val)
        return 0;

    if (c <= 0)
        return 0;
    if (c > 255)
        c = 255;

#define XPOST_RASTER_BLEND(dst, src) \
    ((dst) + (((src) - (dst)) * c + 127) / 255)

    switch(private.pixelformat)
    {
        case BGRA:
        {
            Xpost_Raster_BGRA_Pixel *p = &((Xpost_Raster_BGRA_Pixel *)
                private.buf->data)[iy * private.buf->width + ix];

            p->red = (unsigned char)XPOST_RASTER_BLEND(p->red, r);
            p->green = (unsigned char)XPOST_RASTER_BLEND(p->green, g);
            p->blue = (unsigned char)XPOST_RASTER_BLEND(p->blue, b);
            p->alpha = 255;
        }
        break;
        case BGR:
        {
            Xpost_Raster_BGR_Pixel *p = &((Xpost_Raster_BGR_Pixel *)
                private.buf->data)[iy * private.buf->width + ix];

            p->red = (unsigned char)XPOST_RASTER_BLEND(p->red, r);
            p->green = (unsigned char)XPOST_RASTER_BLEND(p->green, g);
            p->blue = (unsigned char)XPOST_RASTER_BLEND(p->blue, b);
        }
        break;
        case ARGB:
        {
            Xpost_Raster_ARGB_Pixel *p = &((Xpost_Raster_ARGB_Pixel *)
                private.buf->data)[iy * private.buf->width + ix];

            p->red = (unsigned char)XPOST_RASTER_BLEND(p->red, r);
            p->green = (unsigned char)XPOST_RASTER_BLEND(p->green, g);
            p->blue = (unsigned char)XPOST_RASTER_BLEND(p->blue, b);
            p->alpha = 255;
        }
        break;
        case RGB:
        {
            Xpost_Raster_RGB_Pixel *p = &((Xpost_Raster_RGB_Pixel *)
                private.buf->data)[iy * private.buf->width + ix];

            p->red = (unsigned char)XPOST_RASTER_BLEND(p->red, r);
            p->green = (unsigned char)XPOST_RASTER_BLEND(p->green, g);
            p->blue = (unsigned char)XPOST_RASTER_BLEND(p->blue, b);
        }
        break;
    }

#undef XPOST_RASTER_BLEND

    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;

    return 0;
}

/* C fast-path for the base-class per-pixel FillRect. erasepage clears the
   whole page through FillRect, so this is on the hot path for every page. */
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
    int ix, iy, x0, y0, x1, y1, r, g, b, stride;

    /* fold numbers per the driver contract */
    r = xpost_dev_num_to_byte(red);
    g = xpost_dev_num_to_byte(green);
    b = xpost_dev_num_to_byte(blue);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* the contract's rectangle: inclusive span, clipped to the buffer */
    xpost_dev_rect_normalize(xpost_object_number(x), xpost_object_number(y),
                             xpost_object_number(w), xpost_object_number(h),
                             &x0, &y0, &x1, &y1);
    if (!xpost_dev_rect_clip(&x0, &y0, &x1, &y1,
                             private.buf->width, private.buf->height))
        return 0;
    stride = private.buf->width;

#define RASTER_FILLRECT(TYPE, SET) \
    do { \
        TYPE pix; \
        SET \
        for (iy = y0; iy <= y1; iy++) \
        { \
            TYPE *row = (TYPE *)private.buf->data + (size_t)iy * stride; \
            for (ix = x0; ix <= x1; ix++) \
                row[ix] = pix; \
        } \
    } while (0)

    switch (private.pixelformat)
    {
        case BGRA:
            RASTER_FILLRECT(Xpost_Raster_BGRA_Pixel,
                pix.blue = b; pix.green = g; pix.red = r; pix.alpha = 255;);
            break;
        case BGR:
            RASTER_FILLRECT(Xpost_Raster_BGR_Pixel,
                pix.blue = b; pix.green = g; pix.red = r;);
            break;
        case ARGB:
            RASTER_FILLRECT(Xpost_Raster_ARGB_Pixel,
                pix.alpha = 255; pix.red = r; pix.green = g; pix.blue = b;);
            break;
        case RGB:
            RASTER_FILLRECT(Xpost_Raster_RGB_Pixel,
                pix.red = r; pix.green = g; pix.blue = b;);
            break;
    }
#undef RASTER_FILLRECT

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
int newrasterdevice(Xpost_Context *ctx,
                    Xpost_Object width,
                    Xpost_Object height)
{
    Xpost_Object classdic;
    int ret;

    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    ret = xpost_op_any_load(ctx, xpost_name_cons(ctx, "rasterDEVICE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic, xpost_name_cons(ctx, "Create"))))
        return execstackoverflow;

    return 0;
}

static
unsigned int _loadrasterdevicecont_opcode;

/* Specializes or sub-classes the .xpost_PPMIMAGE device class.
   load .xpost_PPMIMAGE
   load and call ps procedure .copydict which leaves copy on stack
   call loadrasterdevicecont by continuation.
 */
static
int loadrasterdevice (Xpost_Context *ctx)
{
    Xpost_Object classdic;
    int ret;

    ret = xpost_op_privatedict_load(ctx, xpost_name_cons(ctx, ".xpost_PPMIMAGE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_operator_cons_opcode(_loadrasterdevicecont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic, namedotcopydict)))
        return execstackoverflow;

    return 0;
}

/* replace procedures in the class with newly created special operators.
   defines the device class rasterDEVICE in userdict.
   defines a new operator in userdict: newrasterdevice
 */
static
int loadrasterdevicecont(Xpost_Context *ctx,
                         Xpost_Object classdic)
{
    /* this device's method suite; the arities follow from its
       declared colour space */
    static const Xpost_Dev_Method methods[] =
    {
        { "Create", "rasterCreate", (Xpost_Op_Func)_create, XPOST_DEV_M_CREATE },
        { "PutPix", "rasterPutPix", (Xpost_Op_Func)_putpix, XPOST_DEV_M_PUTPIX },
        { "GetPix", "rasterGetPix", (Xpost_Op_Func)_getpix, XPOST_DEV_M_GETPIX },
        { "BlendPix", "rasterBlendPix", (Xpost_Op_Func)_blendpix, XPOST_DEV_M_BLEND },
        { "FillRect", "rasterFillRect", (Xpost_Op_Func)_fillrect, XPOST_DEV_M_RECT },
        { "Emit", "rasterEmit", (Xpost_Op_Func)_emit, XPOST_DEV_M_PAGE },
        { "Flush", "rasterFlush", (Xpost_Op_Func)_flush, XPOST_DEV_M_PAGE },
        { "Destroy", "rasterDestroy", (Xpost_Op_Func)_destroy, XPOST_DEV_M_PAGE }
    };

    Xpost_Object userdict;
    Xpost_Object op;
    int ret;

    ret = xpost_dict_put(ctx, classdic, namenativecolorspace, nameDeviceRGB);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "rasterCreateCont", (Xpost_Op_Func)_create_cont, 1, 3, integertype, integertype, dicttype);
    _create_cont_opcode = op.mark_.padw;

    ret = xpost_dev_class_install(ctx, classdic, 3, 1,
                                  methods, XPOST_DEV_METHOD_COUNT(methods));
    if (ret)
        return ret;









    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);

    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "rasterDEVICE"), classdic);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "newrasterdevice", (Xpost_Op_Func)newrasterdevice, 1, 2, integertype, integertype);
    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "newrasterdevice"), op);
    if (ret)
        return ret;

    return 0;
}

/*
   install the loadXXXdevice which may be called during graphics initialization
   to produce the operator newXXXdevice which instantiates the device dictionary.
*/
int xpost_oper_init_raster_device_ops (Xpost_Context *ctx,
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
    op = xpost_operator_cons(ctx, "loadrasterdevice", (Xpost_Op_Func)loadrasterdevice, 1, 0); INSTALL;
    op = xpost_operator_cons(ctx, "loadrasterdevicecont", (Xpost_Op_Func)loadrasterdevicecont, 1, 1, dicttype);
    _loadrasterdevicecont_opcode = op.mark_.padw;

    return 0;
}
