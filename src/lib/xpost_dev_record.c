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

/*
 * The device that writes a page down instead of painting it, and the
 * replay that paints a page it wrote down.
 *
 * Its marking methods put each call into an Xpost_Record and mark
 * nothing, so what it costs follows the number of marks rather than the
 * size of the page; its Emit builds a device that does paint, plays
 * every mark into it and puts out that device's page. See
 * doc/NEWINTERNALS for what the record is for and src/lib/xpost_record.h
 * for what it holds.
 *
 * The class this specialises is data/recorddev.ps, which declares
 * exactly the five marking methods this file records -- the whole
 * reason five kinds are enough is that every other call the machinery
 * can make is resolved above a device that declines to declare it.
 *
 * The operands are written down as they arrived. Which pixels a
 * coordinate names is the painting device's answer and is taken when
 * the mark is played, so a device with a different idea of where its
 * rows begin plays the same record to the pixels its own contract
 * gives.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_error.h"
#include "xpost_dict.h"
#include "xpost_string.h"
#include "xpost_array.h"
#include "xpost_name.h"

#include "xpost_operator.h"
#include "xpost_op_dict.h"
#include "xpost_dev_generic.h" /* the ground a read answers */
#include "xpost_dev_driver.h"  /* device contract and shared helpers */
#include "xpost_op_path.h"     /* XPOST_PATH_BREAK: a subpath separator */
#include "xpost_record.h"
#include "xpost_dev_record.h"

/* The device's colour space is DeviceRGB, so every mark carries three
   components; the class declares it and this is the count the record
   is made with. */
#define RECORD_NCOMP 3

typedef struct
{
    int width, height;
    Xpost_Record *rec;
} PrivateData;

static Xpost_Object namePrivate;
static Xpost_Object namewidth;
static Xpost_Object nameheight;
static Xpost_Object namedotcopydict;
static Xpost_Object namedotplaypage;
static Xpost_Object namenativecolorspace;
static Xpost_Object nameslot[5];

/* The keys of the blit dictionary the image painter builds, which is
   what an image entry is written from and what one is played back
   through. They are taken up once, at start-up, because both directions
   walk the whole set and a lookup by text on the way past every one of
   them would be the walk's cost rather than its content. */
enum
{
    BK_ROWS, BK_DEVW, BK_DEVH, BK_NAT, BK_RGBROWS, BK_CMYK,
    BK_W, BK_NCOMP, BK_BUF, BK_BUFS, BK_XOFF, BK_XSCALE, BK_YOFF,
    BK_YSCALE, BK_CX0, BK_CY0, BK_CX1, BK_CY1, BK_CSPANS, BK_MBITS,
    BK_MROWB, BK_MRANGES, BK_LUT, BK_DLUTS, BK_TLUT, BK_TLUTR,
    BK_TLUTG, BK_TLUTB, BK_INTERP, BK_PREV, BK_PREVS, BK_Y, BK_LAST,
    BK_HTCELL, BK_HTW, BK_HTH, BK_IMGDATA, BK_DIMENSIONS,
    BK_COUNT
};

static const char *const _bdname[BK_COUNT] =
{
    "rows", "devw", "devh", "nat", "rgbrows", "cmyk",
    "w", "ncomp", "buf", "bufs", "xoff", "xscale", "yoff",
    "yscale", "cx0", "cy0", "cx1", "cy1", "cspans", "mbits",
    "mrowb", "mranges", "lut", "dluts", "tlut", "tlutr",
    "tlutg", "tlutb", "interp", "prev", "prevs", "y", "last",
    ".htcell", ".htw", ".hth", "ImgData", "dimensions"
};

static Xpost_Object namebdkey[BK_COUNT];

static unsigned int _create_cont_opcode;
static unsigned int _replay_step_opcode;
static unsigned int _loadrecorddevicecont_opcode;

/* The slot the device that paints is asked through for each kind of
   mark. A record holds the call, so playing it is making the call. */
static Xpost_Object _slot(Xpost_Record_Kind kind)
{
    return nameslot[(int)kind];
}

/* Load this instance's state, or answer that it has none. */
static int _private_get(Xpost_Context *ctx, Xpost_Object devdic,
                        Xpost_Object *privatestr, PrivateData *private)
{
    return xpost_dev_private_get(ctx, devdic, namePrivate, privatestr,
                                 private, sizeof *private);
}

/* Write one mark down. The colour is the three components the device's
   space takes, in the range they arrived in: folding one to a channel
   is the painting device's business and is done when the mark is
   played, by whichever device plays it. */
static int _mark(Xpost_Context *ctx, Xpost_Object devdic,
                 Xpost_Record_Kind kind,
                 const Xpost_Object *comp,
                 const real *ops, int nops)
{
    Xpost_Object privatestr;
    PrivateData private;
    real colour[RECORD_NCOMP];
    int i;

    for (i = 0; i < RECORD_NCOMP; i++)
        colour[i] = (real)xpost_object_number(comp[i]);

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;

    /* a released record takes no marks, as a released raster takes none */
    if (!private.rec)
        return 0;

    if (!xpost_record_mark(private.rec, kind, colour, ops, nops))
        return VMerror;
    return 0;
}

static int _putpix(Xpost_Context *ctx,
                   Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                   Xpost_Object x, Xpost_Object y,
                   Xpost_Object devdic)
{
    Xpost_Object comp[RECORD_NCOMP];
    real ops[2];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    ops[0] = (real)xpost_object_number(x);
    ops[1] = (real)xpost_object_number(y);
    return _mark(ctx, devdic, XPOST_RECORD_PUTPIX, comp, ops, 2);
}

static int _blendpix(Xpost_Context *ctx,
                     Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                     Xpost_Object cov, Xpost_Object x, Xpost_Object y,
                     Xpost_Object devdic)
{
    Xpost_Object comp[RECORD_NCOMP];
    real ops[3];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    ops[0] = (real)xpost_object_number(cov);
    ops[1] = (real)xpost_object_number(x);
    ops[2] = (real)xpost_object_number(y);
    return _mark(ctx, devdic, XPOST_RECORD_BLENDPIX, comp, ops, 3);
}

static int _drawline(Xpost_Context *ctx,
                     Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                     Xpost_Object x1, Xpost_Object y1,
                     Xpost_Object x2, Xpost_Object y2,
                     Xpost_Object devdic)
{
    Xpost_Object comp[RECORD_NCOMP];
    real ops[4];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    ops[0] = (real)xpost_object_number(x1);
    ops[1] = (real)xpost_object_number(y1);
    ops[2] = (real)xpost_object_number(x2);
    ops[3] = (real)xpost_object_number(y2);
    return _mark(ctx, devdic, XPOST_RECORD_DRAWLINE, comp, ops, 4);
}

static int _fillrect(Xpost_Context *ctx,
                     Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                     Xpost_Object x, Xpost_Object y,
                     Xpost_Object w, Xpost_Object h,
                     Xpost_Object devdic)
{
    Xpost_Object comp[RECORD_NCOMP];
    real ops[4];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    ops[0] = (real)xpost_object_number(x);
    ops[1] = (real)xpost_object_number(y);
    ops[2] = (real)xpost_object_number(w);
    ops[3] = (real)xpost_object_number(h);
    return _mark(ctx, devdic, XPOST_RECORD_FILLRECT, comp, ops, 4);
}

/* A polygon is a point list with its subpaths separated, and the
   separators are part of the shape: the interior is settled by scanning
   the subpaths together, so a polygon written down without them replays
   as a region with its holes filled in. The run written down is the
   vertex count and then a pair per element, a separator being the pair
   the packed path already writes a subpath break as. */
static int _fillpoly(Xpost_Context *ctx,
                     Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                     Xpost_Object poly,
                     Xpost_Object devdic)
{
    Xpost_Object comp[RECORD_NCOMP];
    real *ops;
    int n, i, ret;

    comp[0] = red; comp[1] = green; comp[2] = blue;
    n = (int)poly.comp_.sz;
    ops = malloc((size_t)(1 + 2 * n) * sizeof *ops);
    if (!ops)
        return VMerror;
    ops[0] = (real)n;
    for (i = 0; i < n; i++)
    {
        Xpost_Object pair = xpost_array_get(ctx, poly, i);

        if (xpost_object_get_type(pair) == arraytype && pair.comp_.sz == 2)
        {
            ops[1 + 2 * i] = (real)xpost_object_number(xpost_array_get(ctx, pair, 0));
            ops[2 + 2 * i] = (real)xpost_object_number(xpost_array_get(ctx, pair, 1));
        }
        else
        {
            ops[1 + 2 * i] = XPOST_PATH_BREAK;
            ops[2 + 2 * i] = XPOST_PATH_BREAK;
        }
    }
    ret = _mark(ctx, devdic, XPOST_RECORD_FILLPOLY, comp, ops, 1 + 2 * n);
    free(ops);
    return ret;
}

/* Read a pixel back. A record holds no pixel to read, so every read
   answers the ground, which is the answer the contract gives wherever a
   device holds no pixel to answer from. The value is in the channel
   scale the colour raster this device's page is played into stores. */
static int _getpix(Xpost_Context *ctx,
                   Xpost_Object x, Xpost_Object y,
                   Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int r, g, b;

    (void)x;
    (void)y;
    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;

    xpost_device_ground_channels(ctx, devdic, &r, &g, &b);
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(r));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(g));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(b));
    return 0;
}

/* What the blit dictionary carries, read out by key. A key it does not
   carry answers the default, which is what the row writer makes of one
   that is absent. */
static Xpost_Object _bdget(Xpost_Context *ctx, Xpost_Object bd, int k)
{
    return xpost_dict_get(ctx, bd, namebdkey[k]);
}

static int _bdint(Xpost_Context *ctx, Xpost_Object bd, int k, int dflt)
{
    Xpost_Object o = _bdget(ctx, bd, k);
    int t = xpost_object_get_type(o);

    if (t == integertype || t == booleantype)
        return o.int_.val;
    return dflt;
}

static real _bdreal(Xpost_Context *ctx, Xpost_Object bd, int k, real dflt)
{
    return (real)xpost_dev_dict_number(ctx, bd, namebdkey[k], (double)dflt);
}

/* A string the dictionary carries, as bytes, or nothing where it does
   not carry one long enough to be read as far as it will be read. */
static const unsigned char *_bdstr(Xpost_Context *ctx, Xpost_Object bd,
                                   int k, unsigned int need)
{
    Xpost_Object o = _bdget(ctx, bd, k);

    if (xpost_object_get_type(o) != stringtype || o.comp_.sz < need)
        return NULL;
    return (const unsigned char *)xpost_string_get_pointer(ctx, o);
}

/* blitdict rows IMAGE  .recordimage  -
   Write a sampled image down as one entry, rather than as the run of
   one-pixel rectangles a device holding no rows of its own would
   otherwise be painted it a sample at a time.
 *
 * What arrives is the dictionary the image painter builds before it
 * writes its first row and the rows it would have written: the
 * transform placing the image, the region resolved above the device,
 * and the colour tables the painter baked out of the graphics state.
 * The tables are what is kept rather than the state they came from --
 * a replay happens when the page is put out, by which time the
 * transfer functions and the colour space that decoded the image have
 * moved on, and there is no asking them again.
 */
static int _recordimage(Xpost_Context *ctx,
                        Xpost_Object bd,
                        Xpost_Object rows,
                        Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    Xpost_Record_Image img;
    Xpost_Object o;
    const unsigned char **runs = NULL;
    unsigned char dl[4 * 256];
    unsigned char tl[3 * 256];
    int mranges[8];
    real *cspans = NULL;
    unsigned int nrun;
    int i, ret = 0;

    memset(&img, 0, sizeof img);
    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;
    /* a released record takes an image as it takes a mark: not at all */
    if (!private.rec)
        return 0;

    img.width = _bdint(ctx, bd, BK_W, 0);
    img.ncomp = _bdint(ctx, bd, BK_NCOMP, 0);
    img.nat = _bdint(ctx, bd, BK_NAT, 0);
    img.rgbrows = _bdint(ctx, bd, BK_RGBROWS, 0);
    img.cmyk = _bdint(ctx, bd, BK_CMYK, 0);
    img.interp = _bdint(ctx, bd, BK_INTERP, 0);
    img.xoff = _bdreal(ctx, bd, BK_XOFF, 0);
    img.xscale = _bdreal(ctx, bd, BK_XSCALE, 1);
    img.yoff = _bdreal(ctx, bd, BK_YOFF, 0);
    img.yscale = _bdreal(ctx, bd, BK_YSCALE, 1);
    img.cx0 = _bdreal(ctx, bd, BK_CX0, 0);
    img.cy0 = _bdreal(ctx, bd, BK_CY0, 0);
    img.cx1 = _bdreal(ctx, bd, BK_CX1, 0);
    img.cy1 = _bdreal(ctx, bd, BK_CY1, 0);
    img.mrowb = _bdint(ctx, bd, BK_MROWB, 0);

    /* one buffer a component says the rows come a plane at a time, and
       the run handed over is then a plane rather than a row */
    img.planar = xpost_object_get_type(_bdget(ctx, bd, BK_BUFS)) == arraytype;
    if (img.ncomp < 1 || img.ncomp > 4 || img.width < 1
     || img.nat < 1 || img.nat > 3)
        return rangecheck;

    if (xpost_object_get_type(rows) != arraytype)
        return typecheck;
    nrun = rows.comp_.sz;
    if (nrun < 1)
        return rangecheck;
    img.height = (int)(img.planar ? nrun / (unsigned int)img.ncomp : nrun);
    if (img.height < 1)
        return rangecheck;

    /* The rows are the painter's own buffers, refilled for the row
       after, so what is handed over is where each one is now and the
       record takes its own copy. */
    runs = malloc((size_t)nrun * sizeof *runs);
    if (!runs)
        return VMerror;
    for (i = 0; i < (int)nrun; i++)
    {
        unsigned int need = (unsigned int)img.width
                          * (img.planar ? 1u : (unsigned int)img.ncomp);

        o = xpost_array_get(ctx, rows, i);
        if (xpost_object_get_type(o) != stringtype || o.comp_.sz < need)
        {
            ret = typecheck;
            goto out;
        }
        runs[i] = (const unsigned char *)xpost_string_get_pointer(ctx, o);
    }

    /* the tables the painter baked: one for a single-component space,
       which has everything in it, or per-component decode with the
       transfer applied after the conversion */
    img.lut = img.ncomp == 1
        ? _bdstr(ctx, bd, BK_LUT, 256u * (unsigned int)img.nat) : NULL;
    if (img.ncomp == 1 && !img.lut)
    {
        /* an entry with no table to decode by is one the writer would
           refuse when it came to be played, which is a page short of a
           mark rather than a call that failed */
        ret = typecheck;
        goto out;
    }
    if (img.ncomp > 1)
    {
        o = _bdget(ctx, bd, BK_DLUTS);
        if (xpost_object_get_type(o) != arraytype
         || o.comp_.sz < (unsigned int)img.ncomp)
        {
            ret = typecheck;
            goto out;
        }
        for (i = 0; i < img.ncomp; i++)
        {
            Xpost_Object d = xpost_array_get(ctx, o, i);

            if (xpost_object_get_type(d) != stringtype || d.comp_.sz < 256)
            {
                ret = typecheck;
                goto out;
            }
            memcpy(dl + i * 256, xpost_string_get_pointer(ctx, d), 256);
        }
        img.dluts = dl;
    }
    img.tlut = _bdstr(ctx, bd, BK_TLUT, 256u);
    if (img.ncomp > 1 && !img.tlut)
    {
        ret = typecheck;
        goto out;
    }
    if (img.nat == 3)
    {
        const unsigned char *p;
        int k;

        for (k = 0; k < 3; k++)
        {
            p = _bdstr(ctx, bd, BK_TLUTR + k, 256u);
            if (!p)
                break;
            memcpy(tl + k * 256, p, 256);
        }
        if (k == 3)
            img.tlutrgb = tl;
    }

    img.mbits = img.mrowb > 0
        ? _bdstr(ctx, bd, BK_MBITS,
                 (unsigned int)img.mrowb * (unsigned int)img.height) : NULL;
    if (!img.mbits)
        img.mrowb = 0;

    o = _bdget(ctx, bd, BK_MRANGES);
    if (xpost_object_get_type(o) == arraytype && o.comp_.sz <= 8)
    {
        for (i = 0; i < (int)o.comp_.sz; i++)
            mranges[i] = (int)xpost_object_number(xpost_array_get(ctx, o, i));
        img.nranges = (int)o.comp_.sz;
        img.mranges = mranges;
    }

    o = _bdget(ctx, bd, BK_CSPANS);
    if (xpost_object_get_type(o) == arraytype && o.comp_.sz >= 4)
    {
        img.nspan = (int)(o.comp_.sz / 4);
        cspans = malloc((size_t)img.nspan * 4 * sizeof *cspans);
        if (!cspans)
        {
            ret = VMerror;
            goto out;
        }
        for (i = 0; i < img.nspan * 4; i++)
            cspans[i] = (real)xpost_object_number(xpost_array_get(ctx, o, i));
        img.cspans = cspans;
    }

    if (!xpost_record_image(private.rec, &img, runs, (int)nrun))
        ret = VMerror;

out:
    free(cspans);
    free(runs);
    return ret;
}

/* Paint an image entry into the device that paints, for device rows
   @p lo to @p hi.
 *
 * The rows are written through the same writer that wrote them the
 * first time, driven against the target's raster: a second
 * implementation of sampling would be a second set of rounding
 * decisions and the two pages would part company somewhere nobody
 * looked.
 *
 * The run of rows is clipped to by choosing which of the image's rows
 * to write and by narrowing the region they are written through -- not
 * by trimming what was recorded, which would leave a record that could
 * only be played back one way.
 */
static int _play_image(Xpost_Context *ctx,
                       const Xpost_Record_Image *img,
                       Xpost_Object targetdic,
                       real lo, real hi)
{
    Xpost_Object bd, rows, dims;
    Xpost_Object buf = null, prev = null;
    Xpost_Object bufs = null, prevs = null;
    unsigned int mode = ctx->vmmode;
    unsigned int rowbytes;
    real cy0, cy1;
    int devh, y, y0, y1, c;
    int ret = 0;

    rows = xpost_dict_get(ctx, targetdic, namebdkey[BK_IMGDATA]);
    if (xpost_object_get_type(rows) != arraytype)
    {
        /* the device being played into keeps no rows an image can be
           written into, so this mark cannot be made -- the same answer
           as a device missing one of the marking methods */
        XPOST_LOG_ERR("%d a recorded image has no rows to play it into",
                      undefined);
        return undefined;
    }
    dims = xpost_dict_get(ctx, targetdic, namebdkey[BK_DIMENSIONS]);
    if (xpost_object_get_type(dims) != arraytype || dims.comp_.sz < 2)
        return typecheck;
    devh = (int)xpost_object_number(xpost_array_get(ctx, dims, 1));

    if (!xpost_record_image_rows(img, lo, hi, &y0, &y1))
        return 0;
    cy0 = img->cy0 < lo ? lo : img->cy0;
    cy1 = img->cy1 > hi + 1 ? hi + 1 : img->cy1;

    rowbytes = (unsigned int)img->width
             * (img->planar ? 1u : (unsigned int)img->ncomp);

    /* Built in local memory whatever the run was allocating in: it
       lives as long as this call, and global memory is not collected. */
    ctx->vmmode = LOCAL;
    bd = xpost_dict_cons(ctx, 40);
    if (xpost_object_get_type(bd) != dicttype)
    {
        ctx->vmmode = mode;
        return VMerror;
    }

#define PUT(k, v) do { \
        ret = xpost_dict_put(ctx, bd, namebdkey[k], (v)); \
        if (ret) goto out; \
    } while (0)
#define PUTSTR(k, p, n) do { \
        Xpost_Object s_ = xpost_string_cons(ctx, (unsigned int)(n), \
                                            (const char *)(p)); \
        if (xpost_object_get_type(s_) != stringtype) \
            { ret = VMerror; goto out; } \
        PUT(k, s_); \
    } while (0)

    PUT(BK_ROWS, rows);
    PUT(BK_DEVW, xpost_array_get(ctx, dims, 0));
    PUT(BK_DEVH, xpost_int_cons(devh));
    PUT(BK_NAT, xpost_int_cons(img->nat));
    PUT(BK_RGBROWS, xpost_bool_cons(img->rgbrows));
    PUT(BK_CMYK, xpost_bool_cons(img->cmyk));
    PUT(BK_W, xpost_int_cons(img->width));
    PUT(BK_NCOMP, xpost_int_cons(img->ncomp));
    PUT(BK_XOFF, xpost_real_cons(img->xoff));
    PUT(BK_XSCALE, xpost_real_cons(img->xscale));
    PUT(BK_YOFF, xpost_real_cons(img->yoff));
    PUT(BK_YSCALE, xpost_real_cons(img->yscale));
    PUT(BK_CX0, xpost_real_cons(img->cx0));
    PUT(BK_CY0, xpost_real_cons(cy0));
    PUT(BK_CX1, xpost_real_cons(img->cx1));
    PUT(BK_CY1, xpost_real_cons(cy1));

    if (img->lut)
        PUTSTR(BK_LUT, img->lut, 256 * img->nat);
    if (img->dluts)
    {
        Xpost_Object a = xpost_array_cons(ctx, (unsigned int)img->ncomp);

        if (xpost_object_get_type(a) != arraytype)
            { ret = VMerror; goto out; }
        for (c = 0; c < img->ncomp; c++)
        {
            Xpost_Object s = xpost_string_cons(ctx, 256,
                (const char *)(img->dluts + c * 256));

            if (xpost_object_get_type(s) != stringtype)
                { ret = VMerror; goto out; }
            ret = xpost_array_put(ctx, a, c, s);
            if (ret)
                goto out;
        }
        PUT(BK_DLUTS, a);
    }
    if (img->tlut)
        PUTSTR(BK_TLUT, img->tlut, 256);
    if (img->tlutrgb)
    {
        PUTSTR(BK_TLUTR, img->tlutrgb, 256);
        PUTSTR(BK_TLUTG, img->tlutrgb + 256, 256);
        PUTSTR(BK_TLUTB, img->tlutrgb + 512, 256);
    }
    if (img->mbits)
    {
        PUTSTR(BK_MBITS, img->mbits, (size_t)img->mrowb * img->height);
        PUT(BK_MROWB, xpost_int_cons(img->mrowb));
    }
    if (img->nranges)
    {
        Xpost_Object a = xpost_array_cons(ctx, (unsigned int)img->nranges);

        if (xpost_object_get_type(a) != arraytype)
            { ret = VMerror; goto out; }
        for (c = 0; c < img->nranges; c++)
        {
            ret = xpost_array_put(ctx, a, c, xpost_int_cons(img->mranges[c]));
            if (ret)
                goto out;
        }
        PUT(BK_MRANGES, a);
    }
    if (img->nspan)
    {
        Xpost_Object a = xpost_array_cons(ctx, (unsigned int)img->nspan * 4);

        if (xpost_object_get_type(a) != arraytype)
            { ret = VMerror; goto out; }
        for (c = 0; c < img->nspan * 4; c++)
        {
            ret = xpost_array_put(ctx, a, c,
                                  xpost_real_cons(img->cspans[c]));
            if (ret)
                goto out;
        }
        PUT(BK_CSPANS, a);
    }
    /* the screen a bilevel device thresholds through is that device's
       and not the image's, so it is taken from the one being painted */
    if (xpost_object_get_type(
            xpost_dict_get(ctx, targetdic, namebdkey[BK_HTCELL])) == stringtype)
    {
        PUT(BK_HTCELL, xpost_dict_get(ctx, targetdic, namebdkey[BK_HTCELL]));
        PUT(BK_HTW, xpost_dict_get(ctx, targetdic, namebdkey[BK_HTW]));
        PUT(BK_HTH, xpost_dict_get(ctx, targetdic, namebdkey[BK_HTH]));
    }

    /* the row in hand, and -- where the samples are blended -- the row
       before it. The first row blends with itself, which is what the
       painting did with the first row it had. */
    if (img->planar)
    {
        bufs = xpost_array_cons(ctx, (unsigned int)img->ncomp);
        if (img->interp)
            prevs = xpost_array_cons(ctx, (unsigned int)img->ncomp);
        if (xpost_object_get_type(bufs) != arraytype
         || (img->interp && xpost_object_get_type(prevs) != arraytype))
            { ret = VMerror; goto out; }
        for (c = 0; c < img->ncomp; c++)
        {
            Xpost_Object s = xpost_string_cons(ctx, rowbytes, NULL);
            Xpost_Object p = img->interp
                ? xpost_string_cons(ctx, rowbytes, NULL) : null;

            if (xpost_object_get_type(s) != stringtype
             || (img->interp && xpost_object_get_type(p) != stringtype))
                { ret = VMerror; goto out; }
            ret = xpost_array_put(ctx, bufs, c, s);
            if (!ret && img->interp)
                ret = xpost_array_put(ctx, prevs, c, p);
            if (ret)
                goto out;
        }
        PUT(BK_BUFS, bufs);
        if (img->interp)
            PUT(BK_PREVS, prevs);
    }
    else
    {
        buf = xpost_string_cons(ctx, rowbytes, NULL);
        if (xpost_object_get_type(buf) != stringtype)
            { ret = VMerror; goto out; }
        PUT(BK_BUF, buf);
        if (img->interp)
        {
            prev = xpost_string_cons(ctx, rowbytes, NULL);
            if (xpost_object_get_type(prev) != stringtype)
                { ret = VMerror; goto out; }
            PUT(BK_PREV, prev);
        }
    }
    if (img->interp)
        PUT(BK_INTERP, xpost_bool_cons(1));

    /* the two the loop restates, put here so that the dictionary has
       the shape it will keep before any row buffer is filled: a key
       arriving later could grow it, and what a row was written into is
       read back by where it is rather than by where it was */
    PUT(BK_Y, xpost_int_cons(0));
    if (img->interp)
        PUT(BK_LAST, xpost_bool_cons(0));

    for (y = y0; y < y1; y++)
    {
        int p = y > 0 ? y - 1 : 0;

        if (img->planar)
        {
            for (c = 0; c < img->ncomp; c++)
            {
                memcpy(xpost_string_get_pointer(ctx,
                           xpost_array_get(ctx, bufs, c)),
                       img->samples + ((size_t)y * img->ncomp + c) * rowbytes,
                       rowbytes);
                if (img->interp)
                    memcpy(xpost_string_get_pointer(ctx,
                               xpost_array_get(ctx, prevs, c)),
                           img->samples
                           + ((size_t)p * img->ncomp + c) * rowbytes,
                           rowbytes);
            }
        }
        else
        {
            memcpy(xpost_string_get_pointer(ctx, buf),
                   img->samples + (size_t)y * rowbytes, rowbytes);
            if (img->interp)
                memcpy(xpost_string_get_pointer(ctx, prev),
                       img->samples + (size_t)p * rowbytes, rowbytes);
        }
        PUT(BK_Y, xpost_int_cons(y));
        if (img->interp)
            PUT(BK_LAST, xpost_bool_cons(y == img->height - 1));
        ret = xpost_dev_blit_row(ctx, bd);
        if (ret)
            goto out;
    }

#undef PUTSTR
#undef PUT
out:
    ctx->vmmode = mode;
    return ret;
}

/* IMAGE  .recordcost  marks images bytes
   What a record holds and what holding it costs. The mechanism is worth
   having exactly while a record is smaller than the raster it saves
   holding, which is a measurement and not a guess, and this is where
   the measurement is taken. */
static int _recordcost(Xpost_Context *ctx,
                       Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons((integer)xpost_record_count(private.rec)));
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons((integer)xpost_record_image_count(private.rec)));
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons((integer)xpost_record_bytes(private.rec)));
    return 0;
}

/* Play one mark into the device that paints, then come back for the
   next.
 *
 * A device method may be a procedure, and what runs a procedure is the
 * interpreter, so a mark is played by leaving the call on the stacks
 * and returning: the method runs, and this continuation runs after it
 * with the walk's own operands where the call left them.
 */
static int _replay_step(Xpost_Context *ctx,
                        Xpost_Object recdic,
                        Xpost_Object targetdic,
                        Xpost_Object idx,
                        Xpost_Object lo,
                        Xpost_Object hi)
{
    Xpost_Object privatestr;
    PrivateData private;
    Xpost_Record_Kind kind;
    const real *colour;
    const real *ops;
    Xpost_Object method;
    Xpost_Object poly = null;
    size_t at;
    int nops, i;

    if (!_private_get(ctx, recdic, &privatestr, &private))
        return undefined;
    if (!private.rec)
        return 0;
    /* The rows asked for choose which marks are played, and the marks
       between are stepped over rather than played and dropped: what
       makes a page affordable to paint a run of rows at a time is that
       playing it into a set of runs costs the marks each run meets
       rather than every mark once per run. */
    if (!xpost_record_next(private.rec, (size_t)idx.int_.val,
                           (real)xpost_object_number(lo),
                           (real)xpost_object_number(hi), &at))
        return 0;   /* played out */
    if (!xpost_record_get(private.rec, at, &kind, &colour, &ops, &nops))
        return 0;

    /* An image is not one of the marking calls and is not made by
       calling one: its rows are written into the target's raster
       through the writer that wrote them the first time. Nothing is
       left on the stacks for a method to consume, so the walk goes
       straight on.

       The rows painted are the whole of the target's page. A replay
       into part of it hands that part down here instead, and the entry
       writes the rows meeting it -- which is where a band enters. */
    if (kind == XPOST_RECORD_IMAGE)
    {
        const Xpost_Record_Image *img;
        Xpost_Object dims;
        int ret;

        img = xpost_record_image_get(private.rec,
                                     nops > 0 ? (size_t)ops[0] : (size_t)-1);
        if (!img)
        {
            XPOST_LOG_ERR("%d a recorded image names no entry", undefined);
            return undefined;
        }
        dims = xpost_dict_get(ctx, targetdic, namebdkey[BK_DIMENSIONS]);
        if (xpost_object_get_type(dims) != arraytype || dims.comp_.sz < 2)
            return typecheck;
        /* an image is held to the rows asked for the same way a mark is:
           the replay chooses the sample rows that reach them and narrows
           the region it paints to them, so a run of rows takes only its
           own part of an image that crosses it */
        ret = _play_image(ctx, img, targetdic,
                          (real)xpost_object_number(lo),
                          (real)xpost_object_number(hi));
        if (ret)
            return ret;

        xpost_stack_push(ctx->lo, ctx->os, recdic);
        xpost_stack_push(ctx->lo, ctx->os, targetdic);
        xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(idx.int_.val + 1));
        xpost_stack_push(ctx->lo, ctx->os, lo);
        xpost_stack_push(ctx->lo, ctx->os, hi);
        if (!xpost_stack_push(ctx->lo, ctx->es,
                              xpost_operator_cons_opcode(_replay_step_opcode)))
            return execstackoverflow;
        return 0;
    }

    method = xpost_dict_get(ctx, targetdic, _slot(kind));
    if (xpost_object_get_type(method) == invalidtype ||
        xpost_object_get_type(method) == nulltype)
    {
        /* the device being played into does not offer one of the five
           marking methods a record holds, so the mark cannot be made */
        XPOST_LOG_ERR("%d a recorded mark has no method to play it into",
                      undefined);
        return undefined;
    }

    /* A polygon is given back as a run of coordinates and the device
       takes an array, so the array is built here. It is built in local
       memory whatever the run was allocating in: it lives as long as
       the call it is made for, and global memory is not collected. */
    if (kind == XPOST_RECORD_FILLPOLY)
    {
        unsigned int mode = ctx->vmmode;
        int n = (int)ops[0];

        ctx->vmmode = LOCAL;
        poly = xpost_array_cons(ctx, (unsigned int)n);
        if (xpost_object_get_type(poly) != arraytype)
        {
            ctx->vmmode = mode;
            return VMerror;
        }
        for (i = 0; i < n; i++)
        {
            Xpost_Object pair;
            int ret;

            if (ops[1 + 2 * i] == XPOST_PATH_BREAK)
            {
                ret = xpost_array_put(ctx, poly, i, null);
                if (ret)
                {
                    ctx->vmmode = mode;
                    return ret;
                }
                continue;
            }
            pair = xpost_array_cons(ctx, 2);
            if (xpost_object_get_type(pair) != arraytype)
            {
                ctx->vmmode = mode;
                return VMerror;
            }
            ret = xpost_array_put(ctx, pair, 0,
                                  xpost_real_cons(ops[1 + 2 * i]));
            if (!ret)
                ret = xpost_array_put(ctx, pair, 1,
                                      xpost_real_cons(ops[2 + 2 * i]));
            if (!ret)
                ret = xpost_array_put(ctx, poly, i, pair);
            if (ret)
            {
                ctx->vmmode = mode;
                return ret;
            }
        }
        ctx->vmmode = mode;
    }

    /* the walk's own operands, under the call's: the method consumes
       what it was given and leaves these where this continuation reads
       them */
    xpost_stack_push(ctx->lo, ctx->os, recdic);
    xpost_stack_push(ctx->lo, ctx->os, targetdic);
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons((integer)at + 1));
    xpost_stack_push(ctx->lo, ctx->os, lo);
    xpost_stack_push(ctx->lo, ctx->os, hi);

    for (i = 0; i < RECORD_NCOMP; i++)
        xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons(colour[i]));
    if (kind == XPOST_RECORD_FILLPOLY)
        xpost_stack_push(ctx->lo, ctx->os, poly);
    else
        for (i = 0; i < nops; i++)
            xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons(ops[i]));
    xpost_stack_push(ctx->lo, ctx->os, targetdic);

    /* the continuation goes on first, so the call runs before it */
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_operator_cons_opcode(_replay_step_opcode)))
        return execstackoverflow;
    if (xpost_object_get_type(method) == operatortype)
    {
        if (!xpost_stack_push(ctx->lo, ctx->es, method))
            return execstackoverflow;
    }
    else
    {
        xpost_stack_push(ctx->lo, ctx->os, method);
        if (!xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, exec)))
            return execstackoverflow;
    }
    return 0;
}

/* What a replay refuses whatever rows it is asked for, settled once
   rather than mark by mark.

   A mark carries one colour value per component of the space it was made
   in, and it is played by handing those values to a method whose
   operands the receiving device's own space decides -- so a record made
   in one space and played into a device declaring another puts each
   value in the place of a different one, and paints a colour nobody
   named. And a record played into the device holding it writes down what
   it plays: the run it is walking grows by a mark for every mark taken
   from it and there is no end to reach. */
static int _replay_refuse(Xpost_Context *ctx,
                          Xpost_Object recdic,
                          Xpost_Object targetdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!_private_get(ctx, recdic, &privatestr, &private))
        return undefined;
    /* a record short of a mark describes a page it cannot reproduce, and
       what would be painted from it is a page missing something -- which
       looks like a page. It is refused here, where a caller still has an
       error to see, rather than played back short. */
    if (private.rec && xpost_record_failed(private.rec))
    {
        XPOST_LOG_ERR("%d a record short of a mark cannot be played back",
                      VMerror);
        return VMerror;
    }
    if (xpost_dict_compare_objects(ctx, recdic, targetdic) == 0)
    {
        XPOST_LOG_ERR("%d a record cannot be played into the device holding"
                      " it", rangecheck);
        return rangecheck;
    }
    if (xpost_dict_compare_objects(
            ctx, xpost_dict_get(ctx, recdic, namenativecolorspace),
            xpost_dict_get(ctx, targetdic, namenativecolorspace)) != 0)
    {
        XPOST_LOG_ERR("%d a record is played into a device whose colour space"
                      " is not the one its marks were made in", rangecheck);
        return rangecheck;
    }
    return 0;
}

/* Start the walk: the marks that reach rows lo to hi, in the order they
   were made, which is the order they were painted in and so the order
   they must be painted in again. */
static int _replay_walk(Xpost_Context *ctx,
                        Xpost_Object recdic,
                        Xpost_Object targetdic,
                        Xpost_Object lo,
                        Xpost_Object hi)
{
    xpost_stack_push(ctx->lo, ctx->os, recdic);
    xpost_stack_push(ctx->lo, ctx->os, targetdic);
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(0));
    xpost_stack_push(ctx->lo, ctx->os, lo);
    xpost_stack_push(ctx->lo, ctx->os, hi);
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_operator_cons_opcode(_replay_step_opcode)))
        return execstackoverflow;
    return 0;
}

/* recdev pagedev lo hi  .replaypage  -
   Play the marks that reach rows lo to hi into a device that paints.

   The rows are a run of the page's own, and what they are for is the
   caller's: successive runs paint a page in a raster the size of a run,
   and the rows a window shows paint what someone is looking at. A mark
   meeting the run at all is played whole and the device it is played
   into keeps what it holds of it, so a shape crossing the far edge of a
   run is played into the run beyond as well and each keeps its part. */
static int _replaypage_rows(Xpost_Context *ctx,
                            Xpost_Object recdic,
                            Xpost_Object targetdic,
                            Xpost_Object lo,
                            Xpost_Object hi)
{
    int ret;

    ret = _replay_refuse(ctx, recdic, targetdic);
    if (ret)
        return ret;
    return _replay_walk(ctx, recdic, targetdic, lo, hi);
}

/* recdev pagedev  .replaypage  -
   Play every mark a record holds, which is the rows its marks reach.
   A record holding no mark paints nothing and reaches no row, so there
   is no run to name and nothing to walk. */
static int _replaypage(Xpost_Context *ctx,
                       Xpost_Object recdic,
                       Xpost_Object targetdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    real lo, hi;
    int ret;

    ret = _replay_refuse(ctx, recdic, targetdic);
    if (ret)
        return ret;
    if (!_private_get(ctx, recdic, &privatestr, &private))
        return undefined;
    if (!private.rec || !xpost_record_extent(private.rec, &lo, &hi))
        return 0;
    return _replay_walk(ctx, recdic, targetdic,
                        xpost_real_cons(lo), xpost_real_cons(hi));
}

/* create an instance of the device, using the class .copydict procedure */
static int _create(Xpost_Context *ctx,
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

    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_operator_cons_opcode(_create_cont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic, namedotcopydict)))
        return execstackoverflow;
    return 0;
}

/* make the record and name it from the instance */
static int _create_cont(Xpost_Context *ctx,
                        Xpost_Object w,
                        Xpost_Object h,
                        Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    int width, height;
    int ret;

    /* The page the program asked for. A record holds no raster, so what
       the extent settles here is what the marking methods are held
       against and what the device the page is played into is built at,
       which is a page extent either way. */
    if (!xpost_dev_buffer_extent(w.int_.val, &width)
     || !xpost_dev_buffer_extent(h.int_.val, &height))
    {
        XPOST_LOG_ERR("%d a page of %ldx%ld names an extent no raster"
                      " carries", limitcheck,
                      (long)w.int_.val, (long)h.int_.val);
        return limitcheck;
    }

    ret = xpost_handle_cons(ctx, devdic, namePrivate, &privatestr,
                            XPOST_HANDLE_DEVICE, sizeof(PrivateData));
    if (ret)
        return ret;

    private.width = width;
    private.height = height;
    private.rec = xpost_record_new(RECORD_NCOMP);
    if (!private.rec)
        return VMerror;

    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
    {
        /* the state is the only thing that would have named the record,
           and it is not going to */
        xpost_record_free(private.rec);
        return VMerror;
    }

    xpost_stack_push(ctx->lo, ctx->os, devdic);
    return 0;
}

/* Put out the page the record holds, which means painting it: the class
   carries the procedure that builds a device to paint into, plays the
   record through it and puts out its page, and this hands the instance
   to it. What runs a procedure is the interpreter, so it is left on the
   execution stack rather than called. */
static int _emit(Xpost_Context *ctx,
                 Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    Xpost_Object play;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;

    /* a released record has nothing left to put out */
    if (!private.rec)
        return 0;

    /* and one short of a mark has a page it cannot paint whole, so it
       puts out nothing rather than a page missing something */
    if (xpost_record_failed(private.rec))
    {
        XPOST_LOG_ERR("%d a page short of a mark is not put out", VMerror);
        return VMerror;
    }

    play = xpost_dict_get(ctx, devdic, namedotplaypage);
    if (!xpost_object_is_exe(play))
        return undefined;

    xpost_stack_push(ctx->lo, ctx->os, devdic);
    if (!xpost_stack_push(ctx->lo, ctx->es, play))
        return execstackoverflow;
    return 0;
}

static int _destroy(Xpost_Context *ctx,
                    Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;

    xpost_record_free(private.rec);
    private.rec = NULL;
    /* store the cleared pointer back so a repeated destroy is a no-op */
    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof(private)))
        return VMerror;
    return 0;
}

/* operator function to instantiate a new recording device */
static int newrecorddevice(Xpost_Context *ctx,
                           Xpost_Object width,
                           Xpost_Object height)
{
    Xpost_Object classdic;
    int ret;

    xpost_stack_push(ctx->lo, ctx->os, width);
    xpost_stack_push(ctx->lo, ctx->os, height);
    ret = xpost_op_any_load(ctx, xpost_name_cons(ctx, "recordDEVICE"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic,
                                         xpost_name_cons(ctx, "Create"))))
        return execstackoverflow;
    return 0;
}

/* Specialise the .xpost_RECORD class: load it, copy it, and continue
   below with the copy. */
static int loadrecorddevice(Xpost_Context *ctx)
{
    Xpost_Object classdic;
    int ret;

    ret = xpost_op_privatedict_load(ctx, xpost_name_cons(ctx, ".xpost_RECORD"));
    if (ret)
        return ret;
    classdic = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_operator_cons_opcode(_loadrecorddevicecont_opcode)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es,
                          xpost_dict_get(ctx, classdic, namedotcopydict)))
        return execstackoverflow;
    return 0;
}

/* Fill the class's method slots with this device's operators and define
   the class and its maker in userdict. */
static int loadrecorddevicecont(Xpost_Context *ctx,
                                Xpost_Object classdic)
{
    /* This device's whole suite. It is the five marking methods a
       record holds and nothing else that marks: a method it did not
       bring is resolved above the device into these, and one it brought
       would be a call the record has no entry for. */
    static const Xpost_Dev_Method methods[] =
    {
        { "Create", "recordCreate", (Xpost_Op_Func)_create, XPOST_DEV_M_CREATE },
        { "PutPix", "recordPutPix", (Xpost_Op_Func)_putpix, XPOST_DEV_M_PUTPIX },
        { "GetPix", "recordGetPix", (Xpost_Op_Func)_getpix, XPOST_DEV_M_GETPIX },
        { "BlendPix", "recordBlendPix", (Xpost_Op_Func)_blendpix, XPOST_DEV_M_BLEND },
        { "DrawLine", "recordDrawLine", (Xpost_Op_Func)_drawline, XPOST_DEV_M_LINE },
        { "FillRect", "recordFillRect", (Xpost_Op_Func)_fillrect, XPOST_DEV_M_RECT },
        { "FillPoly", "recordFillPoly", (Xpost_Op_Func)_fillpoly, XPOST_DEV_M_POLY },
        { "Emit", "recordEmit", (Xpost_Op_Func)_emit, XPOST_DEV_M_PAGE },
        { "Destroy", "recordDestroy", (Xpost_Op_Func)_destroy, XPOST_DEV_M_PAGE }
    };

    Xpost_Object userdict;
    Xpost_Object op;
    int ret;

    op = xpost_operator_cons(ctx, "recordCreateCont",
                             (Xpost_Op_Func)_create_cont, 3,
                             integertype, integertype, dicttype);
    _create_cont_opcode = op.mark_.padw;

    ret = xpost_dev_class_install(ctx, classdic, RECORD_NCOMP, 1,
                                  methods, XPOST_DEV_METHOD_COUNT(methods));
    if (ret)
        return ret;

    /* How a sampled image reaches the record. It is not one of the
       device methods and is not dispatched as one: the image painter
       looks for it, and finding it writes the image down instead of
       painting it a rectangle per sample into a device that keeps no
       rows. A device method here would be a marking call the record
       holds no entry for, which is the one thing this class must not
       declare (tests/check-device-skeleton.sh). */
    op = xpost_operator_cons(ctx, "recordImage",
                             (Xpost_Op_Func)_recordimage, 3,
                             dicttype, arraytype, dicttype);
    ret = xpost_dict_put(ctx, classdic,
                         xpost_name_cons(ctx, ".recordimage"), op);
    if (ret)
        return ret;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);

    ret = xpost_dict_put(ctx, userdict,
                         xpost_name_cons(ctx, "recordDEVICE"), classdic);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "newrecorddevice",
                             (Xpost_Op_Func)newrecorddevice, 2,
                             integertype, integertype);
    ret = xpost_dict_put(ctx, userdict,
                         xpost_name_cons(ctx, "newrecorddevice"), op);
    if (ret)
        return ret;

    return 0;
}

int xpost_oper_init_record_device_ops (Xpost_Context *ctx,
                Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;
    int i;

    /* factor-out name lookups from the operators (optimization) */
    if (xpost_object_get_type((namePrivate = xpost_name_cons(ctx, "Private"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namewidth = xpost_name_cons(ctx, "width"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameheight = xpost_name_cons(ctx, "height"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namedotcopydict = xpost_name_cons(ctx, ".copydict"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namedotplaypage = xpost_name_cons(ctx, ".playpage"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namenativecolorspace = xpost_name_cons(ctx, "nativecolorspace"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameslot[XPOST_RECORD_PUTPIX] = xpost_name_cons(ctx, "PutPix"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameslot[XPOST_RECORD_BLENDPIX] = xpost_name_cons(ctx, "BlendPix"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameslot[XPOST_RECORD_DRAWLINE] = xpost_name_cons(ctx, "DrawLine"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameslot[XPOST_RECORD_FILLRECT] = xpost_name_cons(ctx, "FillRect"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameslot[XPOST_RECORD_FILLPOLY] = xpost_name_cons(ctx, "FillPoly"))) == invalidtype)
        return VMerror;
    for (i = 0; i < BK_COUNT; i++)
        if (xpost_object_get_type((namebdkey[i] = xpost_name_cons(ctx, _bdname[i])))
            == invalidtype)
            return VMerror;

    optab = xpost_operator_table(ctx->gl);
    op = xpost_operator_cons(ctx, "loadrecorddevice", (Xpost_Op_Func)loadrecorddevice, 0); INSTALL;
    op = xpost_operator_cons(ctx, "loadrecorddevicecont", (Xpost_Op_Func)loadrecorddevicecont, 1, dicttype);
    _loadrecorddevicecont_opcode = op.mark_.padw;

    /* The replay is registered here rather than with the device's
       methods, because a run reaches it through .internaldict and what
       puts an operator there is the relocation pass that runs once,
       during start-up, before any device has been made. */
    op = xpost_operator_cons(ctx, ".replaypage", (Xpost_Op_Func)_replaypage, 2,
                             dicttype, dicttype);
    op = xpost_operator_cons(ctx, ".replaypage",
                             (Xpost_Op_Func)_replaypage_rows, 4,
                             dicttype, dicttype, numbertype, numbertype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".replaystep", (Xpost_Op_Func)_replay_step, 5,
                             dicttype, dicttype, integertype,
                             numbertype, numbertype);
    _replay_step_opcode = op.mark_.padw;
    op = xpost_operator_cons(ctx, ".recordcost", (Xpost_Op_Func)_recordcost, 1,
                             dicttype); INSTALL;

    return 0;
}
