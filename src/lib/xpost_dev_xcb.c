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

#include <stdlib.h> /* abs */
#include <stddef.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_aux.h>

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
#include "xpost_op_dict.h" /* call xpost_op_any_load operator for convenience */
#include "xpost_dev_driver.h" /* device contract and shared helpers */
#include "xpost_dev_xcb.h" /* check prototypes */

#define XCB_ALL_PLANES ~0

typedef struct
{
    xcb_connection_t *c;
    xcb_screen_t *scr;
    xcb_drawable_t win;
    int width, height;
    xcb_pixmap_t img;
    xcb_gcontext_t gc;
    xcb_colormap_t cmap;
} PrivateData;

static int _flush(Xpost_Context *ctx, Xpost_Object devdic);

static
unsigned int _event_handler_opcode;

static Xpost_Object namePrivate;
static Xpost_Object namewidth;
static Xpost_Object nameheight;
static Xpost_Object namedotcopydict;
static Xpost_Object namenativecolorspace;
static Xpost_Object nameDeviceRGB;

static
int _event_handler(Xpost_Context *ctx,
                   Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    xcb_generic_event_t *event;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    event = xcb_poll_for_event(private.c);
    if (event)
    {
        switch(event->response_type & ~0x80)
        {
            case XCB_EXPOSE:
                _flush(ctx, devdic);
                break;
            default:
                break;
        }
        free(event);
    }
    else if (xcb_connection_has_error(private.c))
        return unregistered;

    return 0;
}


static
unsigned int _create_cont_opcode;

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
       then call _create_cont, by continuation. */
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons_opcode(_create_cont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic,
                                         //xpost_name_cons(ctx, ".copydict")
                                         namedotcopydict)))
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
    xcb_screen_iterator_t iter;
    xcb_get_geometry_reply_t *geom;
    integer width = w.int_.val;
    integer height = h.int_.val;
    int scrno;
    unsigned char depth;
    int ret;

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

    /* create xcb connection
       and create and map window */
    private.c = xcb_connect(NULL, &scrno);
    if (xcb_connection_has_error(private.c))
    {
        XPOST_LOG_ERR("Fail to connect to the X server");
        return unregistered;
    }

    iter = xcb_setup_roots_iterator(xcb_get_setup(private.c));
    for (; iter.rem; --scrno, xcb_screen_next(&iter))
    {
        if (scrno == 0)
        {
            private.scr = iter.data;
            break;
        }
    }

    geom = xcb_get_geometry_reply(private.c, xcb_get_geometry(private.c, private.scr->root), 0);
    if (!geom)
    {
        XPOST_LOG_ERR("Fail to the geometry of the root window");
        xcb_disconnect(private.c);
        return unregistered;
    }

    depth = geom->depth;
    free(geom);

    /* a page larger than the screen opens in a smaller window with the
       page scaled to fit: the adopted dimensions land back in the
       device dictionary, and the ratio rides /windowscale for the
       default matrix to fold in */
    {
        int sw = private.scr->width_in_pixels;
        int sh = private.scr->height_in_pixels;
        double s = 1.0;

        if (sw > 0 && width > sw)
            s = (double)sw / (double)width;
        if (sh > 0 && (double)height * s > (double)sh)
            s = (double)sh / (double)height;
        if (s < 1.0)
        {
            width = (integer)((double)width * s + 0.5);
            height = (integer)((double)height * s + 0.5);
            if (width < 1) width = 1;
            if (height < 1) height = 1;
            private.width = width;
            private.height = height;
            ret = xpost_dict_put(ctx, devdic, namewidth, xpost_int_cons(width));
            if (!ret)
                ret = xpost_dict_put(ctx, devdic, nameheight,
                                     xpost_int_cons(height));
            if (!ret)
                ret = xpost_dict_put(ctx, devdic,
                                     xpost_name_cons(ctx, "windowscale"),
                                     xpost_real_cons((real)s));
            if (ret)
            {
                xcb_disconnect(private.c);
                return ret;
            }
        }
    }

    private.win = xcb_generate_id(private.c);
    {
        unsigned int mask = XCB_CW_BACK_PIXMAP |
                            XCB_CW_BACK_PIXEL |
                            XCB_CW_EVENT_MASK;
        unsigned int value[3];
        value[0] = XCB_NONE;
        value[1] = private.scr->white_pixel;
        value[2] = XCB_EVENT_MASK_EXPOSURE;
        xcb_create_window(private.c, XCB_COPY_FROM_PARENT,
                          private.win, private.scr->root,
                          0, 0,
                          width, height,
                          5,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          private.scr->root_visual,
                          mask,
                          value);

        /* set title */
        xcb_change_property(private.c,
                            XCB_PROP_MODE_REPLACE,
                            private.win,
                            XCB_ATOM_WM_NAME,
                            XCB_ATOM_STRING,
                            8,
                            sizeof("Xpost") - 1,
                            "Xpost");
    }
#if 0
    {
        xcb_wm_hints_t hints;
        hints.flags = XCB_WM_HINT_INPUT;
        hints.input = 0;
        xcb_set_wm_hints(private.c, private.win, &hints);
    }
#endif
    xcb_map_window(private.c, private.win);
    xcb_flush(private.c);

    private.img = xcb_generate_id(private.c);
    xcb_create_pixmap(private.c,
                      depth, private.img,
                      private.win, private.width, private.height);

    /* create graphics context
       and initialize drawing parameters */
    private.gc = xcb_generate_id(private.c);
    {
        unsigned int values[2] =
            {
                private.scr->black_pixel,
                private.scr->white_pixel
            };
        xcb_create_gc(private.c, private.gc, private.win,
                      XCB_GC_FOREGROUND | XCB_GC_BACKGROUND,
                      values);
    }

    //private.cmap = private.scr->default_colormap;
    /* create colormap */
    private.cmap = xcb_generate_id(private.c);
    xcb_create_colormap(private.c, XCB_COLORMAP_ALLOC_NONE, private.cmap,
                        private.win, private.scr->root_visual);

    xpost_context_install_event_handler(ctx,
                                        xpost_operator_cons_opcode(_event_handler_opcode),
                                        devdic);


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

    /* fold numbers per the driver contract; xcb colour channels are 16-bit */
    r = xpost_dev_num_to_scaled(red, 65535.0);
    g = xpost_dev_num_to_scaled(green, 65535.0);
    b = xpost_dev_num_to_scaled(blue, 65535.0);
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
        xcb_alloc_color_reply_t *rep;
        unsigned int value;
        xcb_point_t p;
        p.x = ix;
        p.y = iy;

        rep = xcb_alloc_color_reply(private.c,
                                    xcb_alloc_color(private.c, private.cmap,
                                                    r, g, b),
                                    0);
        if (!rep)
            return unregistered;

        value = rep->pixel;
        free(rep);
        xcb_change_gc(private.c, private.gc, XCB_GC_FOREGROUND, &value);

        xcb_poly_point(private.c, XCB_COORD_MODE_ORIGIN,
                       private.img, private.gc, 1, &p);
    }

    /* save private data struct in string */
    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;

    return 0;
}

static
int _getpix(Xpost_Context *ctx,
            Xpost_Object x,
            Xpost_Object y,
            Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    (void)x;
    (void)y;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* ?? I don't know ...
       make a 1-pixel image and use copy_area?  ... */
    return 0;
}

static
int _drawline(Xpost_Context *ctx,
              Xpost_Object red,
              Xpost_Object green,
              Xpost_Object blue,
              Xpost_Object x1,
              Xpost_Object y1,
              Xpost_Object x2,
              Xpost_Object y2,
              Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int r, g, b, ix1, iy1, ix2, iy2;

    /* fold numbers per the driver contract; xcb colour channels are 16-bit */
    r = xpost_dev_num_to_scaled(red, 65535.0);
    g = xpost_dev_num_to_scaled(green, 65535.0);
    b = xpost_dev_num_to_scaled(blue, 65535.0);
    ix1 = xpost_dev_num_to_int(x1);
    iy1 = xpost_dev_num_to_int(y1);
    ix2 = xpost_dev_num_to_int(x2);
    iy2 = xpost_dev_num_to_int(y2);

    XPOST_LOG_INFO("_drawline(%d, %d, %d, %d)",
                   ix1, iy1, ix2, iy2);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    {
        xcb_alloc_color_reply_t *rep;
        unsigned int value;

        rep = xcb_alloc_color_reply(private.c,
                                    xcb_alloc_color(private.c, private.cmap,
                                                    r, g, b),
                                    0);
        if (!rep)
            return unregistered;

        value = rep->pixel;
        free(rep);
        xcb_change_gc(private.c, private.gc, XCB_GC_FOREGROUND, &value);
    }

    {
        xcb_point_t points[2];

        points[0].x = ix1;
        points[0].y = iy1;
        points[1].x = ix2;
        points[1].y = iy2;
        xcb_poly_line(private.c, XCB_COORD_MODE_ORIGIN,
                      private.img, private.gc, 2, points);
    }

    return 0;
}

static
int _fillrect(Xpost_Context *ctx,
              Xpost_Object red,
              Xpost_Object green,
              Xpost_Object blue,
              Xpost_Object x,
              Xpost_Object y,
              Xpost_Object width,
              Xpost_Object height,
              Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int i,j;
    int r, g, b;
    int x0, y0, x1, y1;

    /* fold numbers per the driver contract; xcb colour channels are 16-bit */
    r = xpost_dev_num_to_scaled(red, 65535.0);
    g = xpost_dev_num_to_scaled(green, 65535.0);
    b = xpost_dev_num_to_scaled(blue, 65535.0);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    /* the contract's rectangle: inclusive span, clipped to the device */
    if (!xpost_dev_rect_normalize(xpost_dev_num_to_int(x),
                                  xpost_dev_num_to_int(y),
                                  xpost_dev_num_to_int(width),
                                  xpost_dev_num_to_int(height),
                                  private.width, private.height,
                                  &x0, &y0, &x1, &y1))
        return 0;

    {
        xcb_alloc_color_reply_t *rep;
        unsigned int value;

        rep = xcb_alloc_color_reply(private.c,
                                    xcb_alloc_color(private.c, private.cmap,
                                                    r, g, b),
                                    0);
        if (!rep)
            return unregistered;

        value = rep->pixel;
        free(rep);
        xcb_change_gc(private.c, private.gc, XCB_GC_FOREGROUND, &value);

        for (i = y0; i <= y1; i++)
        {
            for (j = x0; j <= x1; j++)
            {
                xcb_point_t p;
                p.x = j;
                p.y = i;

                xcb_poly_point(private.c, XCB_COORD_MODE_ORIGIN,
                               private.img, private.gc, 1, &p);
            }
        }
    }
    return 0;
}

static
int _fillpoly(Xpost_Context *ctx,
              Xpost_Object red,
              Xpost_Object green,
              Xpost_Object blue,
              Xpost_Object poly,
              Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int r, g, b;

    /* fold numbers per the driver contract; xcb colour channels are 16-bit */
    r = xpost_dev_num_to_scaled(red, 65535.0);
    g = xpost_dev_num_to_scaled(green, 65535.0);
    b = xpost_dev_num_to_scaled(blue, 65535.0);

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    {
        xcb_point_t *points;
        int i;
        xcb_alloc_color_reply_t *rep;
        unsigned int value;

        rep = xcb_alloc_color_reply(private.c,
                                    xcb_alloc_color(private.c, private.cmap,
                                                    r, g, b),
                                    0);
        if (!rep)
            return unregistered;

        value = rep->pixel;
        free(rep);
        xcb_change_gc(private.c, private.gc, XCB_GC_FOREGROUND, &value);

        points = malloc((poly.comp_.sz //+ 1
                    ) * sizeof *points);
        for (i = 0; i < poly.comp_.sz; i++)
        {
            Xpost_Object pair;
            pair = xpost_array_get(ctx, poly, i);
            points[i].x = xpost_dev_num_to_int(xpost_array_get(ctx, pair, 0));
            points[i].y = xpost_dev_num_to_int(xpost_array_get(ctx, pair, 1));
        }
        //points[i].x = points[0].x;
        //points[i].y = points[0].y;

        xcb_fill_poly(private.c, private.img, private.gc,
                      XCB_POLY_SHAPE_NONCONVEX,
                      XCB_COORD_MODE_ORIGIN,
                      poly.comp_.sz, //+ 1
                      points);
        free(points);
    }

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

    xcb_copy_area(private.c, private.img, private.win, private.gc,
                  0, 0, 0, 0, private.width, private.height);
    xcb_flush(private.c);

    return 0;
}

/* Emit here is the same as Flush
   But Flush is called (if available) by all raster operators
   for smoother previewing.
 */
static
int (*_emit)(Xpost_Context *ctx, Xpost_Object devdic) = _flush;

static
int _destroy(Xpost_Context *ctx,
             Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!xpost_dev_private_get(ctx, devdic, namePrivate,
                               &privatestr, &private, sizeof(private)))
        return undefined;

    if (private.c)
    {
        xpost_context_install_event_handler(ctx, null, null);

        xcb_disconnect(private.c);
        private.c = NULL;
        /* store the cleared connection back so a repeated destroy is a
           no-op instead of disconnecting freed state */
        if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;
    }

    return 0;
}



/* operator function to instantiate a new window device.
   installed in userdict by calling 'loadXXXdevice'.
 */
static
int newxcbdevice(Xpost_Context *ctx,
                 Xpost_Object width,
                 Xpost_Object height)
{
    Xpost_Object classdic;
    int ret;

    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    ret = xpost_op_any_load(ctx, xpost_name_cons(ctx, "xcbDEVICE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_dict_get(ctx, classdic, xpost_name_cons(ctx, "Create"))))
        return execstackoverflow;

    return 0;
}

static
unsigned int _loadxcbdevicecont_opcode;

/* Specializes or sub-classes the .xpost_PPMIMAGE device class.
   load .xpost_PPMIMAGE
   load and call ps procedure .copydict which leaves copy on stack
   call loadxcbdevicecont by continuation.
 */
static
int loadxcbdevice(Xpost_Context *ctx)
{
    Xpost_Object classdic;
    int ret;

    ret = xpost_op_privatedict_load(ctx, xpost_name_cons(ctx, ".xpost_PPMIMAGE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons_opcode(_loadxcbdevicecont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic,
                                         //xpost_name_cons(ctx, ".copydict")
                                         namedotcopydict)))
        return execstackoverflow;

    return 0;
}

/* replace procedures in the class with newly created special operators.
   defines the device class xcbDEVICE in userdict.
   defines a new operator in userdict: newxcbdevice
 */
static
int loadxcbdevicecont(Xpost_Context *ctx,
                      Xpost_Object classdic)
{
    Xpost_Object userdict;
    Xpost_Object op;
    int ret;

    ret = xpost_dict_put(ctx, classdic,
                         //xpost_name_cons(ctx, "nativecolorspace"),
                         namenativecolorspace,
                         //xpost_name_cons(ctx, "DeviceRGB")
                         nameDeviceRGB);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbCreateCont", (Xpost_Op_Func)_create_cont, 1, 3,
                             integertype, integertype, dicttype);
    _create_cont_opcode = op.mark_.padw;
    op = xpost_operator_cons(ctx, "xcbCreate", (Xpost_Op_Func)_create, 1, 3,
                             integertype, integertype, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "Create"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbPutPix", (Xpost_Op_Func)_putpix, 0, 6,
                             numbertype, numbertype, numbertype, /* r g b color values */
                             numbertype, numbertype, /* x y coords */
                             dicttype); /* devdic */
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "PutPix"), op);
    if (ret)
        return ret;

    /* Paint glyphs without blending their edges. The blend the text
       operators would otherwise use reads the pixel already there, which
       for a window means asking the server for it one pixel at a time;
       the base class's blend reaches for a raster of PostScript arrays
       this device does not keep at all, and answers undefined. Declaring
       one bit of text alpha takes the aliased path, which paints through
       PutPix above. */
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "TextAlphaBits"),
                         xpost_int_cons(1));
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbGetPix", (Xpost_Op_Func)_getpix, 3, 3, numbertype, numbertype, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "GetPix"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbDrawLine", (Xpost_Op_Func)_drawline, 0, 8,
                             numbertype, numbertype, numbertype, /* r g b color values */
                             numbertype, numbertype, /* x1 y1 */
                             numbertype, numbertype, /* x2 y2 */
                             dicttype); /* devdic */
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "DrawLine"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbFillRect", (Xpost_Op_Func)_fillrect, 0, 8,
                             numbertype, numbertype, numbertype, /* r g b color values */
                             numbertype, numbertype, /* x y */
                             numbertype, numbertype, /* width height */
                             dicttype); /* devdic */
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "FillRect"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbFillPoly", (Xpost_Op_Func)_fillpoly, 0, 5,
                             numbertype, numbertype, numbertype,
                             arraytype, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "FillPoly"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbEmit", (Xpost_Op_Func)_emit, 0, 1, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "Emit"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbFlush", (Xpost_Op_Func)_flush, 0, 1, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "Flush"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbDestroy", (Xpost_Op_Func)_destroy, 0, 1, dicttype);
    ret = xpost_dict_put(ctx, classdic, xpost_name_cons(ctx, "Destroy"), op);
    if (ret)
        return ret;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);

    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "xcbDEVICE"), classdic);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "newxcbdevice", (Xpost_Op_Func)newxcbdevice, 1, 2, integertype, integertype);
    ret = xpost_dict_put(ctx, userdict, xpost_name_cons(ctx, "newxcbdevice"), op);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "xcbEventHandler", (Xpost_Op_Func)_event_handler, 0, 1, dicttype);
    _event_handler_opcode = op.mark_.padw;

    return 0;
}

/*
   install the loadXXXdevice which may be called during graphics initialization
   to produce the operator newXXXdevice which instantiates the device dictionary.
*/
int xpost_oper_init_xcb_device_ops (Xpost_Context *ctx,
                Xpost_Object sd)
{
    unsigned int optadr;
    Xpost_Operator *optab;
    Xpost_Object n,op;

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
    op = xpost_operator_cons(ctx, "loadxcbdevice", (Xpost_Op_Func)loadxcbdevice, 1, 0); INSTALL;
    op = xpost_operator_cons(ctx, "loadxcbdevicecont", (Xpost_Op_Func)loadxcbdevicecont, 1, 1, dicttype);
    _loadxcbdevicecont_opcode = op.mark_.padw;
    //printf("initxcbops\n");

    return 0;
}
