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
    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    xpost_stack_push(ctx->lo, ctx->os, classdic);
    xpost_dict_put(ctx, classdic, namewidth, width);
    xpost_dict_put(ctx, classdic, nameheight, height);

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
    //printf("create_cont\n");

    /* create a string to contain device data structure */
    privatestr = xpost_string_cons(ctx, sizeof(PrivateData), NULL);
    if (xpost_object_get_type(privatestr) == invalidtype)
    {
        XPOST_LOG_ERR("cannot allocate private data structure");
        return unregistered;
    }
    xpost_dict_put(ctx, devdic, namePrivate, privatestr);

    private.width = width;
    private.height = height;

    /*
     *
     * initialize additional members of private struct
     *
     */

    {
        /* allocate buffer header and array */
        private.buf = malloc(sizeof(Xpost_Bgr_Buffer) + sizeof(Xpost_Bgr_Pixel)*width*height);
        private.bufowned = 1;
    }

    /* save private data struct in string */
    xpost_dev_private_put(ctx, privatestr, &private, sizeof(private));

    /* return device instance dictionary to ps */
    xpost_stack_push(ctx->lo, ctx->os, devdic);
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

    if ((ix < 0) || (ix >= private.width) ||
        (iy < 0) || (iy >= private.height))
        return 0;

    if (c <= 0)
        return 0;
    if (c > 255)
        c = 255;

    {
        Xpost_Bgr_Pixel *p = &private.buf->data[iy * private.width + ix];

        p->red = (unsigned char)(p->red + ((r - p->red) * c + 127) / 255);
        p->green = (unsigned char)(p->green + ((g - p->green) * c + 127) / 255);
        p->blue = (unsigned char)(p->blue + ((b - p->blue) * c + 127) / 255);
    }

    xpost_dev_private_put(ctx, privatestr, &private, sizeof(private));

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
    if ((ix < 0) || (ix >= private.width) ||
        (iy < 0) || (iy >= private.height))
        return 0;

    {
        Xpost_Bgr_Pixel pixel;
        pixel.blue = b;
        pixel.green = g;
        pixel.red = r;
        private.buf->data[iy * private.width + ix] = pixel;
    }

    xpost_dev_private_put(ctx, privatestr, &private, sizeof(private));

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
        xpost_dev_private_put(ctx, privatestr, &private, sizeof(private));
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
    xpost_dev_private_put(ctx, privatestr, &private, sizeof(private));

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
    Xpost_Object userdict;
    Xpost_Object op;
    int ret;

    ret = xpost_dict_put(ctx, classdic, namenativecolorspace, nameDeviceRGB);

    op = xpost_operator_cons(ctx, "bgrCreateCont", (Xpost_Op_Func)_create_cont, 1, 3, integertype, integertype, dicttype);
    _create_cont_opcode = op.mark_.padw;
    op = xpost_operator_cons(ctx, "bgrCreate", (Xpost_Op_Func)_create, 1, 3, integertype, integertype, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "Create"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "bgrPutPix", (Xpost_Op_Func)_putpix, 0, 6,
                             numbertype, numbertype, numbertype,
                             numbertype, numbertype,
                             dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "PutPix"), op);
    if (ret)
        return 0;

    op = xpost_operator_cons(ctx, "bgrBlendPix", (Xpost_Op_Func)_blendpix, 0, 7,
                             numbertype, numbertype, numbertype, numbertype,
                             numbertype, numbertype, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "BlendPix"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "bgrEmit", (Xpost_Op_Func)_emit, 0, 1, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "Emit"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "bgrFlush", (Xpost_Op_Func)_flush, 0, 1, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "Flush"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "bgrDestroy", (Xpost_Op_Func)_destroy, 0, 1, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "Destroy"), op);
    if (ret)
        return ret;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);

    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "bgrDEVICE"), classdic);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "newbgrdevice", (Xpost_Op_Func)newbgrdevice, 1, 2, integertype, integertype);
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
    unsigned int optadr;
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

    xpost_memory_table_get_addr(ctx->gl,
                                XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE,
                                &optadr);
    optab = (Xpost_Operator *)(ctx->gl->base + optadr);
    op = xpost_operator_cons(ctx, "loadbgrdevice", (Xpost_Op_Func)loadbgrdevice, 1, 0); INSTALL;
    op = xpost_operator_cons(ctx, "loadbgrdevicecont", (Xpost_Op_Func)loadbgrdevicecont, 1, 1, dicttype);
    _loadbgrdevicecont_opcode = op.mark_.padw;

    return 0;
}
