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
 * Which device paints the page is the run's, named beside the device in
 * the selection this run was started with: -d record:pgm plays into the
 * grayscale raster and -d record on its own into the colour one. The
 * class carries the roster of the devices a record can be played into
 * and this file settles one of them at load, because the choice decides
 * the shape of every marking method here -- a mark carries one value per
 * component of the target's colour space, so a grayscale target and a
 * colour one are two suites of entry points and not one suite told
 * which it is.
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

/* The most components a mark can carry here, which is the widest of the
   colour spaces a device this record plays into declares: three for the
   colour rasters, one for the grayscale one. It bounds the buffers the
   entry points below build a colour in; how many of them a given record
   uses is the record's own count, settled when the device is made from
   the class the run selected a target for. */
#define RECORD_MAXCOMP 3

typedef struct
{
    int width, height;
    /* the components a mark of this record carries, which is the
       component count of the device its page is played into */
    int ncomp;
    Xpost_Record *rec;
    /* how many times a recorded image has been painted through this
       device, which is what .recordplays answers */
    unsigned int plays;
    /* and how many recorded marks have been played through it, which is
       what .recordplayed answers */
    unsigned int played;
    /* and how many sample rows of a recorded image have gone through
       the image writer, which is what .recordimagerows answers */
    unsigned int imgrows;
} PrivateData;

static Xpost_Object namePrivate;
static Xpost_Object namewidth;
static Xpost_Object nameheight;
static Xpost_Object namedotcopydict;
static Xpost_Object namedotplaypage;
static Xpost_Object namedotground;
static Xpost_Object namenativecolorspace;
static Xpost_Object namedotncomp;
static Xpost_Object nametextalphabits;
static Xpost_Object namedotplaytargets;
static Xpost_Object namedotplayloaders;
static Xpost_Object namedotplayclass;
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
    BK_HTCELL, BK_HTW, BK_HTH, BK_IMGDATA, BK_DIMENSIONS, BK_DEV,
    BK_COUNT
};

static const char *const _bdname[BK_COUNT] =
{
    "rows", "devw", "devh", "nat", "rgbrows", "cmyk",
    "w", "ncomp", "buf", "bufs", "xoff", "xscale", "yoff",
    "yscale", "cx0", "cy0", "cx1", "cy1", "cspans", "mbits",
    "mrowb", "mranges", "lut", "dluts", "tlut", "tlutr",
    "tlutg", "tlutb", "interp", "prev", "prevs", "y", "last",
    ".htcell", ".htw", ".hth", "ImgData", "dimensions", "dev"
};

static Xpost_Object namebdkey[BK_COUNT];

static unsigned int _create_cont_opcode;
static unsigned int _replay_step_opcode;
static unsigned int _loadrecorddevicecont_opcode;

/* The slot the device that paints is asked through for each kind of
   mark. A record holds the call, so playing it is making the call. */
static Xpost_Object _slot(Xpost_Record_Kind kind)
{
    /* The five marking calls are the only kinds played by calling a
       method. An image is written through the row writer that wrote it,
       and a screen is put back on the device rather than painted, so
       neither names a method the target declares and neither reaches
       here; answering nothing for them keeps that true of any kind
       added beside them. */
    if ((int)kind < 0
        || (size_t)kind >= sizeof nameslot / sizeof *nameslot)
        return null;
    return nameslot[(int)kind];
}

/* Load this instance's state, or answer that it has none. */
static int _private_get(Xpost_Context *ctx, Xpost_Object devdic,
                        Xpost_Object *privatestr, PrivateData *private)
{
    return xpost_dev_private_get(ctx, devdic, namePrivate, privatestr,
                                 private, sizeof *private);
}

/* Whether a rectangle covers every pixel of the page.
 *
 * The pixels it covers are taken from its operands by the normaliser the
 * fill itself takes them from, so what counts as covering here and what
 * the fill paints there are one statement rather than two that agree on
 * the cases anyone tried.
 */
static int _covers_page(const real *ops, int width, int height)
{
    int x0, y0, x1, y1;

    xpost_dev_rect_normalize((double)ops[0], (double)ops[1],
                             (double)ops[2], (double)ops[3],
                             &x0, &y0, &x1, &y1);
    return x0 <= 0 && y0 <= 0 && x1 >= width - 1 && y1 >= height - 1;
}

/* Write one mark down. The colour is the components the device's space
   takes, in the range they arrived in: folding one to a channel is the
   painting device's business and is done when the mark is played, by
   whichever device plays it.
 *
 * @p ncomp is the count the entry point calling this was installed with,
 * and the record holds its marks at the count it was made with. The two
 * are one number read twice -- both come from the class -- and a mark of
 * a shape the record has no room for is refused rather than written
 * down, since a colour read back at another count is a colour nobody
 * named.
 */
static int _mark(Xpost_Context *ctx, Xpost_Object devdic,
                 Xpost_Record_Kind kind,
                 const Xpost_Object *comp, int ncomp,
                 const real *ops, int nops)
{
    Xpost_Object privatestr;
    PrivateData private;
    real colour[RECORD_MAXCOMP];
    int i;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;

    /* a released record takes no marks, as a released raster takes none */
    if (!private.rec)
        return 0;

    if (ncomp != private.ncomp)
    {
        XPOST_LOG_ERR("%d a mark of %d colour values is offered to a record"
                      " holding %d", rangecheck, ncomp, private.ncomp);
        return rangecheck;
    }
    for (i = 0; i < ncomp; i++)
        colour[i] = (real)xpost_object_number(comp[i]);

    /* A rectangle covering the page paints over every mark before it, so
       none of them is on the page any longer and the record gives them
       up: what it holds from here is the page this rectangle begins.
       This is where a page boundary reaches a recorder. Clearing the
       page arrives as a rectangle covering it -- the class declares no
       Erase, which is the rule that keeps a record complete -- and a
       page is cleared as it starts, so a record's lifetime is a page's
       and not a job's (doc/NEWINTERNALS).

       Reading the boundary off the marks rather than off the emission is
       what keeps a page put out twice right: copypage transmits a page
       without erasing it (PLRM 8.2), and the page after it is the same
       page with more on it, so the marks before it are still the page's
       and are still held. */
    if (kind == XPOST_RECORD_FILLRECT && nops == 4
        && _covers_page(ops, private.width, private.height))
        xpost_record_clear(private.rec);

    if (!xpost_record_mark(private.rec, kind, colour, ops, nops))
        return VMerror;
    return 0;
}

/* A polygon is a point list with its subpaths separated, and the
   separators are part of the shape: the interior is settled by scanning
   the subpaths together, so a polygon written down without them replays
   as a region with its holes filled in. The run written down is the
   vertex count and then a pair per element, a separator being the pair
   the packed path already writes a subpath break as. */
static int _polymark(Xpost_Context *ctx, Xpost_Object devdic,
                     const Xpost_Object *comp, int ncomp,
                     Xpost_Object poly)
{
    real *ops;
    int n, i, ret;

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
    ret = _mark(ctx, devdic, XPOST_RECORD_FILLPOLY, comp, ncomp,
                ops, 1 + 2 * n);
    free(ops);
    return ret;
}

/* Read a pixel back. A record holds no pixel to read, so every read
   answers the ground, which is the answer the contract gives wherever a
   device holds no pixel to answer from. The values are in the channel
   scale the raster this device's page is played into stores, and there
   are as many of them as that raster's colour space takes -- a read is
   the one method whose operands do not follow the colour space, so one
   entry point serves a record of either shape and takes the count from
   the record rather than from where it was installed. */
static int _getpix(Xpost_Context *ctx,
                   Xpost_Object x, Xpost_Object y,
                   Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    Xpost_Object ground;
    int i;

    (void)x;
    (void)y;
    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;

    ground = xpost_dict_get(ctx, devdic, namedotground);
    for (i = 0; i < private.ncomp; i++)
    {
        /* A page that was never cleared has no ground recorded, and
           every device a record plays into makes its raster white, so
           that is what a read off such a page owes. */
        int v = 255;

        if (xpost_object_get_type(ground) == arraytype
            && ground.comp_.sz >= (unsigned int)private.ncomp)
            v = xpost_dev_num_to_scaled(xpost_array_get(ctx, ground, i),
                                        255.0);
        xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(v));
    }
    return 0;
}

/*
 * The colour suite: the entry points of a record whose target declares
 * DeviceRGB, so that every mark carries three components.
 */

static int _putpix(Xpost_Context *ctx,
                   Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                   Xpost_Object x, Xpost_Object y,
                   Xpost_Object devdic)
{
    Xpost_Object comp[3];
    real ops[2];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    ops[0] = (real)xpost_object_number(x);
    ops[1] = (real)xpost_object_number(y);
    return _mark(ctx, devdic, XPOST_RECORD_PUTPIX, comp, 3, ops, 2);
}

static int _blendpix(Xpost_Context *ctx,
                     Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                     Xpost_Object cov, Xpost_Object x, Xpost_Object y,
                     Xpost_Object devdic)
{
    Xpost_Object comp[3];
    real ops[3];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    ops[0] = (real)xpost_object_number(cov);
    ops[1] = (real)xpost_object_number(x);
    ops[2] = (real)xpost_object_number(y);
    return _mark(ctx, devdic, XPOST_RECORD_BLENDPIX, comp, 3, ops, 3);
}

static int _drawline(Xpost_Context *ctx,
                     Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                     Xpost_Object x1, Xpost_Object y1,
                     Xpost_Object x2, Xpost_Object y2,
                     Xpost_Object devdic)
{
    Xpost_Object comp[3];
    real ops[4];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    ops[0] = (real)xpost_object_number(x1);
    ops[1] = (real)xpost_object_number(y1);
    ops[2] = (real)xpost_object_number(x2);
    ops[3] = (real)xpost_object_number(y2);
    return _mark(ctx, devdic, XPOST_RECORD_DRAWLINE, comp, 3, ops, 4);
}

static int _fillrect(Xpost_Context *ctx,
                     Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                     Xpost_Object x, Xpost_Object y,
                     Xpost_Object w, Xpost_Object h,
                     Xpost_Object devdic)
{
    Xpost_Object comp[3];
    real ops[4];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    ops[0] = (real)xpost_object_number(x);
    ops[1] = (real)xpost_object_number(y);
    ops[2] = (real)xpost_object_number(w);
    ops[3] = (real)xpost_object_number(h);
    return _mark(ctx, devdic, XPOST_RECORD_FILLRECT, comp, 3, ops, 4);
}

static int _fillpoly(Xpost_Context *ctx,
                     Xpost_Object red, Xpost_Object green, Xpost_Object blue,
                     Xpost_Object poly,
                     Xpost_Object devdic)
{
    Xpost_Object comp[3];

    comp[0] = red; comp[1] = green; comp[2] = blue;
    return _polymark(ctx, devdic, comp, 3, poly);
}

/*
 * The grayscale suite: the same five marks, for a record whose target
 * declares DeviceGray and whose marks therefore carry one component.
 *
 * A method's operand count follows the device's colour space, and it is
 * fixed when the class is installed (xpost_dev_class_install,
 * src/lib/xpost_dev_driver.h) -- so an entry point is written for a
 * count rather than told one, and these are the same calls at the other
 * count. What each does with what it received is the shared code above.
 */

static int _putpix_g(Xpost_Context *ctx,
                     Xpost_Object grey,
                     Xpost_Object x, Xpost_Object y,
                     Xpost_Object devdic)
{
    Xpost_Object comp[1];
    real ops[2];

    comp[0] = grey;
    ops[0] = (real)xpost_object_number(x);
    ops[1] = (real)xpost_object_number(y);
    return _mark(ctx, devdic, XPOST_RECORD_PUTPIX, comp, 1, ops, 2);
}

static int _blendpix_g(Xpost_Context *ctx,
                       Xpost_Object grey,
                       Xpost_Object cov, Xpost_Object x, Xpost_Object y,
                       Xpost_Object devdic)
{
    Xpost_Object comp[1];
    real ops[3];

    comp[0] = grey;
    ops[0] = (real)xpost_object_number(cov);
    ops[1] = (real)xpost_object_number(x);
    ops[2] = (real)xpost_object_number(y);
    return _mark(ctx, devdic, XPOST_RECORD_BLENDPIX, comp, 1, ops, 3);
}

static int _drawline_g(Xpost_Context *ctx,
                       Xpost_Object grey,
                       Xpost_Object x1, Xpost_Object y1,
                       Xpost_Object x2, Xpost_Object y2,
                       Xpost_Object devdic)
{
    Xpost_Object comp[1];
    real ops[4];

    comp[0] = grey;
    ops[0] = (real)xpost_object_number(x1);
    ops[1] = (real)xpost_object_number(y1);
    ops[2] = (real)xpost_object_number(x2);
    ops[3] = (real)xpost_object_number(y2);
    return _mark(ctx, devdic, XPOST_RECORD_DRAWLINE, comp, 1, ops, 4);
}

static int _fillrect_g(Xpost_Context *ctx,
                       Xpost_Object grey,
                       Xpost_Object x, Xpost_Object y,
                       Xpost_Object w, Xpost_Object h,
                       Xpost_Object devdic)
{
    Xpost_Object comp[1];
    real ops[4];

    comp[0] = grey;
    ops[0] = (real)xpost_object_number(x);
    ops[1] = (real)xpost_object_number(y);
    ops[2] = (real)xpost_object_number(w);
    ops[3] = (real)xpost_object_number(h);
    return _mark(ctx, devdic, XPOST_RECORD_FILLRECT, comp, 1, ops, 4);
}

static int _fillpoly_g(Xpost_Context *ctx,
                       Xpost_Object grey,
                       Xpost_Object poly,
                       Xpost_Object devdic)
{
    Xpost_Object comp[1];

    comp[0] = grey;
    return _polymark(ctx, devdic, comp, 1, poly);
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
 * Where that target keeps its raster in a buffer of its own it has no
 * rows to be written into, and the writer is driven against the device
 * itself: the same sampling, handed to the target's own rectangle fill
 * one run of columns at a time. What decides which of the two a target
 * takes is the target -- a device holding rows is written into them --
 * and the picture is the same picture either way, since only the last
 * step of the one writer differs.
 *
 * The run of rows is clipped to by choosing which of the image's rows
 * to write and by narrowing the region they are written through -- not
 * by trimming what was recorded, which would leave a record that could
 * only be played back one way.
 */
static int _play_image(Xpost_Context *ctx,
                       const Xpost_Record_Image *img,
                       Xpost_Object targetdic,
                       real lo, real hi,
                       int *nrows)
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
    if (xpost_object_get_type(rows) != arraytype
        && xpost_object_get_type(
               xpost_dict_get(ctx, targetdic, _slot(XPOST_RECORD_FILLRECT)))
           != operatortype)
    {
        /* the device being played into keeps no rows an image can be
           written into and declares no compiled fill it can be painted
           into a span at a time, so this mark cannot be made -- the
           same answer as a device missing one of the marking
           methods */
        XPOST_LOG_ERR("%d a recorded image has nowhere to play it into",
                      undefined);
        return undefined;
    }
    dims = xpost_dict_get(ctx, targetdic, namebdkey[BK_DIMENSIONS]);
    if (xpost_object_get_type(dims) != arraytype || dims.comp_.sz < 2)
        return typecheck;
    devh = (int)xpost_object_number(xpost_array_get(ctx, dims, 1));

    *nrows = 0;
    if (!xpost_record_image_rows(img, lo, hi, &y0, &y1))
        return 0;
    /* The sample rows this run of the page's rows takes, which is what
       a band of a picture costs against the whole of it. A run the
       picture's rows do not reach names an empty span rather than a
       span running backwards, and an empty one costs nothing. */
    *nrows = y1 > y0 ? y1 - y0 : 0;
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

    /* the rows the picture is written into, or the device it is
       painted into where it keeps none */
    if (xpost_object_get_type(rows) == arraytype)
        PUT(BK_ROWS, rows);
    else
        PUT(BK_DEV, targetdic);
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

/* IMAGE  .recordplays  int
   How many times this device has painted a recorded image.

   An image is the one entry whose replay costs the picture rather than a
   mark, so what a band replay of one costs is the count of bands the
   image reaches -- and no page can say whether that is what was paid.
   A row written twice carries what one write left, so an image played
   again over rows it has already been played into leaves the page it
   left the first time. The quantity is stated here for the same reason
   .recordcost states the other one: what the mechanism is judged on is
   not what the page shows. */
static int _recordplays(Xpost_Context *ctx,
                        Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons((integer)private.plays));
    return 0;
}

/* How many screens the record holds, which is how many times the page
   changed the screen it was being painted under, and one more for the
   screen it opened under. A page whose target does not screen holds
   none. */
static int _recordscreens(Xpost_Context *ctx,
                          Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons((integer)
                                    xpost_record_screen_count(private.rec)));
    return 0;
}

/* IMAGE  .recordimagerows  int
   How many sample rows of a recorded image this device has put through
   the image writer.

   What .recordplays says is how many times a picture was painted, and a
   replay that painted the whole of it into every run of the page's rows
   would say exactly what one painting each run its own part of it says.
   The page cannot tell them apart either: a row written outside the run
   the device is holding is dropped against the row it was about to be
   written to, so the picture comes out the same and the cost is the
   picture times the runs. This is the quantity that separates them --
   the rows the writer was handed, which for a run of the page's rows is
   the samples reaching that run and not the samples there are. */
static int _recordimagerows(Xpost_Context *ctx,
                            Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons((integer)private.imgrows));
    return 0;
}

/* IMAGE  .recordplayed  int
   How many recorded marks this device has played.

   What a page put out a run of rows at a time costs is the marks each
   run meets rather than every mark once per run, and no page can say
   which it paid: a mark played into a run of rows it does not reach
   paints nothing, so a replay handed the whole page for every band puts
   out the page a replay handed each band's own rows puts out. The
   quantity is stated here for the same reason .recordcost and
   .recordplays state theirs -- what the mechanism is judged on is not
   what the page shows. */
static int _recordplayed(Xpost_Context *ctx,
                         Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons((integer)private.played));
    return 0;
}

/* Whether one mark leaves a run of rows the ground and nothing else: a
   rectangle at exactly the colour the page was cleared to, covering
   every row of the run and the whole width of the page.

   The pixels it covers are taken from its operands by the normaliser
   the fill itself takes them from, so what counts as covering here and
   what the fill paints there are one statement rather than two that
   agree on the cases anyone tried.

   The colour is compared as it was written down, before any fold to a
   stored channel. Two colours a fold would bring to one byte are
   answered no here, which costs a pass over the rows rather than a page
   that is missing what they held. */
static int _grounds(Xpost_Context *ctx, Xpost_Object devdic,
                    const Xpost_Record *rec, size_t at,
                    real lo, real hi, int width, int ncomp)
{
    Xpost_Object ground;
    Xpost_Record_Kind kind;
    const real *colour;
    const real *ops;
    int nops, i, x0, y0, x1, y1;

    ground = xpost_dict_get(ctx, devdic, namedotground);
    if (xpost_object_get_type(ground) != arraytype ||
        ground.comp_.sz < (unsigned int)ncomp)
        return 0;   /* a page that was never cleared has no ground */
    if (!xpost_record_get(rec, at, &kind, &colour, &ops, &nops))
        return 0;
    if (kind != XPOST_RECORD_FILLRECT || nops < 4)
        return 0;
    for (i = 0; i < ncomp; i++)
        if ((real)xpost_object_number(xpost_array_get(ctx, ground, i))
            != colour[i])
            return 0;
    xpost_dev_rect_normalize((double)ops[0], (double)ops[1],
                             (double)ops[2], (double)ops[3],
                             &x0, &y0, &x1, &y1);
    return x0 <= 0 && x1 >= width - 1
        && (double)y0 <= (double)lo && (double)y1 >= (double)hi;
}

/* recdev lo hi  .recordground  bool
   Whether a run of the page's rows comes to nothing but the ground.

   A run that does need not be painted at all: the ground is what a
   device holding no pixel over a row answers, and what an emitted page
   carries there, so leaving such a run unpainted puts out the page
   painting it would have put out. The band loop asks this of each band
   and passes over the ones that answer yes (data/recorddev.ps).

   What decides it is the last mark reaching the run, since everything
   before it is painted over wherever it covers and nothing follows it.
   A run no mark reaches at all is not the ground: what a device shows
   over such a run is its raster as it was made, which is the ground
   only where the page was cleared to it, and a record says a page was
   cleared by holding the rectangle that cleared it.

   The answer is no wherever anything here is not as it should be, that
   being the direction that costs a pass over rows rather than a page
   missing what those rows held. */
static int _recordground(Xpost_Context *ctx,
                         Xpost_Object devdic,
                         Xpost_Object lo,
                         Xpost_Object hi)
{
    Xpost_Object privatestr;
    PrivateData private;
    size_t at;
    int answer = 0;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;
    if (private.rec && private.width > 0 &&
        xpost_record_last(private.rec, (real)xpost_object_number(lo),
                          (real)xpost_object_number(hi), &at))
        answer = _grounds(ctx, devdic, private.rec, at,
                          (real)xpost_object_number(lo),
                          (real)xpost_object_number(hi), private.width,
                          private.ncomp);
    xpost_stack_push(ctx->lo, ctx->os, xpost_bool_cons(answer));
    return 0;
}

/* Put a cell on the device that paints. It is what playing a screen
   entry comes to, and what a raster is given before it is moved onto
   its first band. */
static int _install_screen(Xpost_Context *ctx, Xpost_Object targetdic,
                           const unsigned char *cell, int w, int h)
{
    Xpost_Object s;
    unsigned int mode;
    int ret;

    /* the cell is made in the memory the device it is put on lives in:
       a dictionary in global memory holding a string from local memory
       is the reference across banks that a restore would leave
       dangling, and the check refuses it */
    mode = ctx->vmmode;
    ctx->vmmode = (targetdic.tag & XPOST_OBJECT_TAG_DATA_FLAG_BANK)
                ? GLOBAL : LOCAL;
    s = xpost_string_cons(ctx, (unsigned int)w * (unsigned int)h,
                          (const char *)cell);
    ctx->vmmode = mode;
    if (xpost_object_get_type(s) != stringtype)
        return VMerror;
    /* Literal, because the cell is read by name from a procedure as
       well as by the compiled fills. A name holding an executable
       string runs the string, so a cell left executable would be
       scanned as program text the moment a device asked for its own
       screen -- which is what the rows a page holds no pixel of do. */
    s = xpost_object_cvlit(s);

    ret = xpost_dict_put(ctx, targetdic, namebdkey[BK_HTCELL], s);
    if (!ret)
        ret = xpost_dict_put(ctx, targetdic, namebdkey[BK_HTW],
                             xpost_int_cons((integer)w));
    if (!ret)
        ret = xpost_dict_put(ctx, targetdic, namebdkey[BK_HTH],
                             xpost_int_cons((integer)h));
    return ret;
}

/* The screen the raster paints its ground under.
 *
 * A device rendering a grey as a pattern of pixels lays the rows it
 * holds no pixel of under the screen it holds, and a band lays those
 * rows before anything is played into it -- so a raster about to stand
 * on its first band needs a screen already, which no entry has yet put
 * there.
 *
 * The screen given is the last the page set, because that is the one a
 * page held whole is put out under: a page's rows are laid before its
 * marks are played and read back after, so what a whole page shows
 * where nothing was painted is the ground under the screen the page
 * ended with. A band's rows carry the same, and a page painted in bands
 * comes to the page painted whole.
 *
 * A record whose target does not screen holds no screen, and this puts
 * nothing. */
static int _playscreen(Xpost_Context *ctx,
                       Xpost_Object recdic,
                       Xpost_Object targetdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    const unsigned char *cell;
    size_t n;
    int w = 0, h = 0;

    if (!_private_get(ctx, recdic, &privatestr, &private))
        return undefined;
    if (!private.rec)
        return 0;
    n = xpost_record_screen_count(private.rec);
    if (!n)
        return 0;
    cell = xpost_record_screen_get(private.rec, n - 1, &w, &h);
    if (!cell)
        return 0;
    return _install_screen(ctx, targetdic, cell, w, h);
}

/* What this device is told when the screen changes.
 *
 * A record's target may render a grey as a pattern of pixels, choosing
 * each pixel by the threshold under it. Which thresholds those are is
 * the screen in force, and it is state a marking call does not carry:
 * the device reads it from itself when it paints. A page played back
 * later would find whatever screen its target held by then, so the
 * screen is written down here, where the machinery maintaining it says
 * it has changed, and put back as a replay passes it.
 *
 * Being told costs a page one entry per screen it sets rather than one
 * per mark, because that machinery rebuilds the cell only where the
 * screen it is built from has changed. */
static int _recordscreen(Xpost_Context *ctx,
                         Xpost_Object devdic)
{
    Xpost_Object privatestr;
    PrivateData private;
    const unsigned char *cell;
    int w, h;

    if (!_private_get(ctx, devdic, &privatestr, &private))
        return undefined;

    /* a released record takes no screen, as it takes no mark */
    if (!private.rec)
        return 0;

    /* read by the rule the painting device reads it by, so that a
       screen written down is one a page could have been painted under */
    cell = xpost_dev_ht_cell(ctx, devdic, &w, &h);
    if (!cell)
    {
        XPOST_LOG_ERR("%d a screen change offers no cell to record",
                      typecheck);
        return typecheck;
    }
    if (!xpost_record_screen(private.rec, w, h, cell))
        return VMerror;
    return 0;
}

/* A polygon mark's coordinates as the array a device method takes.
 *
 * It is built in local memory whatever the run was allocating in: it
 * lives as long as the call it is made for, and global memory is not
 * collected. One array per mark, and one two-element array per vertex
 * inside it -- the shape the method's operand is, which is not a shape
 * a run of coordinates can be lent. It is not carried from one mark to
 * the next either: the length differs mark by mark, and a method is
 * given its operand to keep for as long as it wants it, so an array
 * handed to two calls is an array the first of them may still be
 * holding. */
static int _play_poly(Xpost_Context *ctx, const real *ops,
                      Xpost_Object *poly)
{
    unsigned int mode = ctx->vmmode;
    int n = (int)ops[0];
    int i;

    ctx->vmmode = LOCAL;
    *poly = xpost_array_cons(ctx, (unsigned int)n);
    if (xpost_object_get_type(*poly) != arraytype)
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
            ret = xpost_array_put(ctx, *poly, i, null);
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
        ret = xpost_array_put(ctx, pair, 0, xpost_real_cons(ops[1 + 2 * i]));
        if (!ret)
            ret = xpost_array_put(ctx, pair, 1,
                                  xpost_real_cons(ops[2 + 2 * i]));
        if (!ret)
            ret = xpost_array_put(ctx, *poly, i, pair);
        if (ret)
        {
            ctx->vmmode = mode;
            return ret;
        }
    }
    ctx->vmmode = mode;
    return 0;
}

/* Whether an object is the walk's own continuation, which is how the
   loop below tells an execution stack a call left alone from one it
   left work on. */
static int _is_replay_cont(Xpost_Object o)
{
    return xpost_object_get_type(o) == operatortype
        && o.mark_.padw == _replay_step_opcode;
}

/* How many marks one call plays before handing the interpreter back.
 *
 * The loop below is bounded rather than open, and what bounds it is
 * what returning is for. Between two evaluation steps the interpreter
 * takes the collection a run has asked for and the interrupt an outside
 * caller has raised; neither can be taken while this is running, so a
 * band played in one go is a band during which memory is not reclaimed
 * and a request to stop is not heard. A batch this size leaves the
 * round trip at a fifth of a per cent of the marks and holds both to
 * the length of one batch. A collection asked for inside a batch ends
 * it early besides, since that is the one of the two that a long band
 * can be made to need. */
#define XPOST_REPLAY_BATCH 512

/* Play the marks that reach the rows asked for, from the one at @p idx
 * on, into the device that paints.
 *
 * A mark is played by calling the target's method for its kind. Where
 * that method is a compiled operator the call is made here, in a loop:
 * the operands go on the operand stack, which is where an operator
 * takes them from and where its declared shape is checked, and the
 * operator is run without going round the interpreter for it. What that
 * saves is not the call but the journey -- a mark played by leaving the
 * call on the stacks and returning costs two evaluation steps, and with
 * them a fresh look at this device's state and at the target's method
 * table, for every mark on the page.
 *
 * Where the method is not an operator the loop cannot make the call: a
 * device method may be a procedure, and what runs a procedure is the
 * interpreter. Such a mark is played by leaving the call on the stacks
 * and returning, and the walk resumes here afterwards and goes back to
 * looping. The same applies to a method that is an operator and leaves
 * work behind it: an operator may put a continuation of its own on the
 * execution stack, and the marks after it must be played after that
 * work rather than before it. So the walk's own continuation goes on
 * the execution stack before each call and is taken off again after it
 * -- if it is still on top, nothing was left and the loop goes on; if
 * it is not, the interpreter has been given something to do and this
 * returns to let it, with the continuation already sitting under the
 * work in the right place.
 *
 * The walk's operands sit on the operand stack for the whole batch
 * rather than being pushed and dropped per mark. The method consumes
 * what it was given and leaves them where they were, so the only one
 * that changes is the place in the record, which is written over in
 * place as each mark is played.
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
    Xpost_Object caller = ctx->currentobject;
    Xpost_Object cont;
    /* the target's method for each of the five marking kinds, looked up
       as each kind is first met and kept for the batch: the table is the
       target's and does not change while its own methods are running */
    Xpost_Object method[5];
    char looked[5];
    real rlo, rhi;
    size_t from;
    int batch, k;
    int ret = 0;

    if (!_private_get(ctx, recdic, &privatestr, &private))
        return undefined;
    if (!private.rec)
        return 0;

    memset(looked, 0, sizeof looked);
    cont = xpost_operator_cons_opcode(_replay_step_opcode);
    rlo = (real)xpost_object_number(lo);
    rhi = (real)xpost_object_number(hi);
    from = (size_t)idx.int_.val;

    /* The walk's own operands, which every return below leaves in place
       for the continuation to be resumed with. They go on once for the
       batch rather than once for each mark, the marks being played over
       the top of them. */
    xpost_stack_push(ctx->lo, ctx->os, recdic);
    xpost_stack_push(ctx->lo, ctx->os, targetdic);
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons((integer)from));
    xpost_stack_push(ctx->lo, ctx->os, lo);
    xpost_stack_push(ctx->lo, ctx->os, hi);
    /* a push the stack would not take leaves the walk standing on
       operands that are not there; the run is told about it at the next
       evaluation step, and nothing is played in the meantime */
    if (ctx->lo->push_refused)
        return 0;

    for (batch = 0; batch < XPOST_REPLAY_BATCH; batch++)
    {
        Xpost_Record_Kind kind;
        const real *colour;
        const real *ops;
        Xpost_Object m;
        Xpost_Object poly = null;
        size_t at;
        int nops, i;

        /* The rows asked for choose which marks are played, and the
           marks between are stepped over rather than played and
           dropped: what makes a page affordable to paint a run of rows
           at a time is that playing it into a set of runs costs the
           marks each run meets rather than every mark once per run.

           Nothing further reaching those rows, or an entry that cannot
           be read: either way the walk is over and its operands go
           back. */
        if (!xpost_record_next(private.rec, from, rlo, rhi, &at))
            goto refused;
        if (!xpost_record_get(private.rec, at, &kind, &colour, &ops, &nops))
            goto refused;

        /* Every entry played moves the walk past itself, whether it was
           a mark, a picture or a screen. Resuming where it was looking
           from instead would find the same entry again for every mark
           the rows asked for had it step over.

           Written over the operand the continuation reads it from, the
           third of the five the walk stands on. There is no reaching
           for a place the stack does not have: the five went on above,
           the push that would not have taken them was answered there,
           and each turn of the loop leaves them where it found them --
           a method takes the operands its own shape states and this
           returns rather than looping past a method whose shape is not
           stated. */
        from = at + 1;
        XPOST_REFUSAL_IMPOSSIBLE(
            xpost_stack_topdown_replace(ctx->lo, ctx->os, 2,
                                        xpost_int_cons((integer)from)));

        /* An image is not one of the marking calls and is not made by
           calling one: its rows are written into the target's raster
           through the writer that wrote them the first time. Nothing is
           left on the stacks for a method to consume, so the loop goes
           straight on to the next entry rather than round the
           interpreter.

           The rows painted are the whole of the target's page. A replay
           into part of it hands that part down here instead, and the
           entry writes the rows meeting it -- which is where a band
           enters. */
        if (kind == XPOST_RECORD_IMAGE)
        {
            const Xpost_Record_Image *img;
            Xpost_Object dims;
            int nrows;

            img = xpost_record_image_get(private.rec,
                                         nops > 0 ? (size_t)ops[0]
                                                  : (size_t)-1);
            if (!img)
            {
                XPOST_LOG_ERR("%d a recorded image names no entry", undefined);
                ret = undefined;
                goto refused;
            }
            dims = xpost_dict_get(ctx, targetdic, namebdkey[BK_DIMENSIONS]);
            if (xpost_object_get_type(dims) != arraytype || dims.comp_.sz < 2)
            {
                ret = typecheck;
                goto refused;
            }
            /* an image is held to the rows asked for the same way a mark
               is: the replay chooses the sample rows that reach them and
               narrows the region it paints to them, so a run of rows
               takes only its own part of an image that crosses it */
            ret = _play_image(ctx, img, targetdic, rlo, rhi, &nrows);
            if (ret)
                goto refused;
            private.plays++;
            private.imgrows += (unsigned int)nrows;
            continue;
        }

        /* A screen is not a mark and paints nothing. It says what the
           marks after it were made under, and playing it is putting that
           back on the device about to paint them, so that each mark is
           screened by the screen it was made under rather than by
           whichever one the target happens to hold when the page is put
           out.

           Every run of rows passes through the same screens in the same
           order, a screen being met by every range, so the pixels a band
           paints are the pixels the whole page would have carried
           there. */
        if (kind == XPOST_RECORD_SCREEN)
        {
            const unsigned char *cell;
            int w = 0, h = 0;

            cell = xpost_record_screen_get(private.rec,
                                           nops > 0 ? (size_t)ops[0]
                                                    : (size_t)-1,
                                           &w, &h);
            if (!cell)
            {
                XPOST_LOG_ERR("%d a recorded screen names no entry",
                              undefined);
                ret = undefined;
                goto refused;
            }
            ret = _install_screen(ctx, targetdic, cell, w, h);
            if (ret)
                goto refused;
            continue;
        }

        if ((int)kind < 0 || (int)kind >= (int)(sizeof looked / sizeof *looked))
        {
            XPOST_LOG_ERR("%d a recorded mark is of no kind a record holds",
                          undefined);
            ret = undefined;
            goto refused;
        }
        if (!looked[(int)kind])
        {
            method[(int)kind] = xpost_dict_get(ctx, targetdic, _slot(kind));
            looked[(int)kind] = 1;
        }
        m = method[(int)kind];
        if (xpost_object_get_type(m) == invalidtype ||
            xpost_object_get_type(m) == nulltype)
        {
            /* the device being played into does not offer one of the
               five marking methods a record holds, so the mark cannot be
               made */
            XPOST_LOG_ERR("%d a recorded mark has no method to play it into",
                          undefined);
            ret = undefined;
            goto refused;
        }

        /* One more mark played, which is what .recordplayed answers.
           Counted here, where the call about to be made is known to be
           one the target offers, so what the count says is marks made
           and not marks looked at. */
        private.played++;

        if (kind == XPOST_RECORD_FILLPOLY)
        {
            ret = _play_poly(ctx, ops, &poly);
            if (ret)
                goto refused;
        }

        /* the colour it was made with, one value per component of the
           space it was made in, which is the space the device being
           played into declares and so the operands its method takes */
        for (i = 0; i < private.ncomp; i++)
            xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons(colour[i]));
        if (kind == XPOST_RECORD_FILLPOLY)
            xpost_stack_push(ctx->lo, ctx->os, poly);
        else
            for (i = 0; i < nops; i++)
                xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons(ops[i]));
        xpost_stack_push(ctx->lo, ctx->os, targetdic);

        /* the continuation goes on first, so that the call runs before
           it and anything the call leaves behind runs before it too */
        if (!xpost_stack_push(ctx->lo, ctx->es, cont))
        {
            ret = execstackoverflow;
            goto done;
        }

        if (xpost_object_get_type(m) != operatortype)
        {
            /* a method that is a procedure, which only the interpreter
               runs: the call is left on the stacks and this returns */
            xpost_stack_push(ctx->lo, ctx->os, m);
            if (!xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, exec)))
                ret = execstackoverflow;
            goto done;
        }

        /* The mark is made from here. The operator is named as the
           object being executed for the length of the call, so that an
           error raised inside it is reported against the method and
           unwinds onto the method's own operands, which is what an error
           from a mark played by the interpreter does. */
        ctx->currentobject = m;
        ret = xpost_operator_exec(ctx, m.mark_.padw);
        if (ret)
            goto done;  /* the method is left named, for the error */
        ctx->currentobject = caller;

        /* Whether the call left the interpreter something to do. The
           walk's continuation was put on before the call, so work left
           behind sits above it and this returns to let the interpreter
           reach both in that order; an untouched execution stack still
           has the continuation on top and the loop takes it back off and
           goes on to the next mark. */
        if (!_is_replay_cont(xpost_stack_topdown_fetch(ctx->lo, ctx->es, 0)))
            goto done;
        (void)xpost_stack_pop(ctx->lo, ctx->es);

        /* A collection the run has asked for is taken between evaluation
           steps and not inside this, so a batch that has been asked for
           one ends here and the interpreter takes it before the walk is
           resumed. */
        if ((ctx->lo && ctx->lo->garbage_collect_pending) ||
            (ctx->gl && ctx->gl->garbage_collect_pending))
            break;
    }

    /* the batch is full, or a collection is waiting: the walk goes back
       on to be resumed, standing on the operands it was left with */
    if (!xpost_stack_push(ctx->lo, ctx->es, cont))
        ret = execstackoverflow;

    goto done;

  refused:
    /* Nothing was played for this entry and nothing will be: the walk's
       operands come back off, so that a caller reached by the error
       finds the stack the entry point left rather than the walk's own
       working state. An error raised by a method that did run leaves
       them where they are, which is where a mark played by the
       interpreter leaves them. */
    for (k = 0; k < 5; k++)
        (void)xpost_stack_pop(ctx->lo, ctx->os);

  done:
    if (!xpost_dev_private_put(ctx, privatestr, &private, sizeof private))
        return ret ? ret : VMerror;
    return ret;
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
    Xpost_Object ncomp;
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

    /* The components a mark of this record carries, taken from the
       instance rather than from where the entry points were installed:
       the record is made to hold that many values per mark, and a
       colour it gave back at another count would be a colour nobody
       named. The class carries it, put there when the target this
       record plays into was settled, and the instance is a copy of the
       class. */
    ncomp = xpost_dict_get(ctx, devdic, namedotncomp);
    if (xpost_object_get_type(ncomp) != integertype
        || ncomp.int_.val < 1 || ncomp.int_.val > RECORD_MAXCOMP)
    {
        XPOST_LOG_ERR("%d a record device carries no component count a mark"
                      " can be held at", rangecheck);
        return rangecheck;
    }

    ret = xpost_handle_cons(ctx, devdic, namePrivate, &privatestr,
                            XPOST_HANDLE_DEVICE, sizeof(PrivateData));
    if (ret)
        return ret;

    private.width = width;
    private.height = height;
    private.ncomp = ncomp.int_.val;
    private.plays = 0;
    private.played = 0;
    private.imgrows = 0;
    private.rec = xpost_record_new(private.ncomp);
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

/* The mode selector this run's device selection carried, as the name a
   roster is keyed by, or a null where the run named none.

   The selector belongs to the device it was written beside: a run
   started as -d raster:bgra names a raster's pixel format, which says
   nothing about where a record's page goes, and a program that switches
   to a record afterwards has not chosen anything. So it is read only
   where this run's selection named this device, and a record the run did
   not select takes the target a selection naming no mode takes. */
static Xpost_Object _selected_mode(Xpost_Context *ctx, const char *device)
{
    Xpost_Object o;
    char name[16];

    o = xpost_context_host_setting(ctx, "StartDevice");
    if (xpost_object_get_type(o) != nametype
        || xpost_dict_compare_objects(ctx, o,
                                      xpost_name_cons(ctx, device)) != 0)
        return null;

    o = xpost_context_host_setting(ctx, "SUBDEVICE");
    if (xpost_object_get_type(o) != stringtype || o.comp_.sz < 1
        || o.comp_.sz >= sizeof name)
        return null;
    /* the bytes are as long as the string says and a name is as long as
       the NUL after it says, so the copy states the length twice */
    memcpy(name, xpost_string_get_pointer(ctx, o), o.comp_.sz);
    name[o.comp_.sz] = '\0';
    return xpost_name_cons(ctx, name);
}

/* The driver to bring in before this record is specialised, or a null
   where there is none to bring in.
 *
 * The class of the device a record plays into is what the record is
 * specialised from, and a class a C driver defines is not there until
 * the driver has been loaded. Which driver that is the class states
 * (data/recorddev.ps); it is asked for only where the class is not
 * already in the private dictionary, so a driver loaded by anything
 * else is not loaded twice.
 */
static Xpost_Object _play_loader(Xpost_Context *ctx, Xpost_Object classdic)
{
    Xpost_Object mode, targets, loaders, clsname, o;

    mode = _selected_mode(ctx, "record");
    if (xpost_object_get_type(mode) != nametype)
        return null;
    targets = xpost_dict_get(ctx, classdic, namedotplaytargets);
    loaders = xpost_dict_get(ctx, classdic, namedotplayloaders);
    if (xpost_object_get_type(targets) != dicttype
        || xpost_object_get_type(loaders) != dicttype)
        return null;
    clsname = xpost_dict_get(ctx, targets, mode);
    if (xpost_object_get_type(clsname) != nametype
        || xpost_object_get_type(xpost_dict_get(ctx, ctx->privatedict,
                                                clsname)) == dicttype)
        return null;
    o = xpost_dict_get(ctx, loaders, mode);
    if (xpost_object_get_type(o) != nametype)
        return null;
    /* the loaders are operators the drivers install in systemdict, and
       a build without the driver installed none: such a name answers
       nothing here and the target is refused further on, where every
       other unbuilt target is refused */
    o = xpost_dict_get(ctx, xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0), o);
    return xpost_object_get_type(o) == operatortype ? o : null;
}

/* Specialise the .xpost_RECORD class: load it, copy it, and continue
   below with the copy. */
static int loadrecorddevice(Xpost_Context *ctx)
{
    Xpost_Object classdic, loader;
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
    /* and, ahead of both, the driver of the device this record plays
       into: the copy above is made from this class and the continuation
       takes the target's colour space off the target's class, so the
       class has to be there by the time it looks */
    loader = _play_loader(ctx, classdic);
    if (xpost_object_get_type(loader) == operatortype
        && !xpost_stack_push(ctx->lo, ctx->es, loader))
        return execstackoverflow;
    return 0;
}

/* Settle which device paints this record's page, and take from it what
   this class must declare to be sent the marks that device would have
   been sent.

   The roster of the devices a record can be played into is the class's,
   with the reasons a device is on it (data/recorddev.ps); what this adds
   is the run's choice among them, and the refusal of anything else. The
   colour space and the component count come off the target because a
   mark carries one value per component of the space it was made in and
   is played by handing those values to a method whose operands the
   target's space decides; the glyph coverage comes off it because a
   target that thresholds a glyph's edge pixels to whole pixels would
   otherwise be played the coverage-weighted blends this device was sent
   in its place.

   Answers the component count the class is to be installed at. */
static int _play_target(Xpost_Context *ctx, Xpost_Object classdic,
                        int *ncomp)
{
    Xpost_Object mode, targets, clsname, cls, o;
    int ret;

    targets = xpost_dict_get(ctx, classdic, namedotplaytargets);
    if (xpost_object_get_type(targets) != dicttype)
    {
        XPOST_LOG_ERR("%d the recording class carries no roster of the"
                      " devices it can be played into", undefined);
        return undefined;
    }

    mode = _selected_mode(ctx, "record");
    if (xpost_object_get_type(mode) != nametype)
        mode = xpost_name_cons(ctx, "ppm");

    clsname = xpost_dict_get(ctx, targets, mode);
    if (xpost_object_get_type(clsname) != nametype)
    {
        XPOST_LOG_ERR("%d a record is asked to be played into a device that"
                      " is not one it can be played into", rangecheck);
        return rangecheck;
    }
    cls = xpost_dict_get(ctx, ctx->privatedict, clsname);
    if (xpost_object_get_type(cls) != dicttype)
    {
        /* a device the roster names and this build did not make */
        XPOST_LOG_ERR("%d the device a record is to be played into was not"
                      " loaded", undefined);
        return undefined;
    }

    o = xpost_dict_get(ctx, cls, namedotncomp);
    if (xpost_object_get_type(o) != integertype
        || o.int_.val < 1 || o.int_.val > RECORD_MAXCOMP)
    {
        XPOST_LOG_ERR("%d the device a record is to be played into takes a"
                      " colour no record holds", rangecheck);
        return rangecheck;
    }
    *ncomp = o.int_.val;
    ret = xpost_dict_put(ctx, classdic, namedotncomp, o);
    if (!ret)
        ret = xpost_dict_put(ctx, classdic, namenativecolorspace,
                             xpost_dict_get(ctx, cls, namenativecolorspace));
    if (!ret)
        ret = xpost_dict_put(ctx, classdic, nametextalphabits,
                             xpost_dict_get(ctx, cls, nametextalphabits));
    /* A device rendering a grey as a pattern of pixels declares
       ScreenPaint, and the painting machinery then keeps that screen's
       cell on it. A recorder standing in for such a device declares it
       as well -- not because it screens anything, but because what it
       writes down will be screened when it is played, and the screen a
       mark was made under is state it can only be told about while the
       page is being drawn. A recorder for a target that does not screen
       is never told, and holds no screen. */
    if (!ret)
    {
        Xpost_Object sp = xpost_name_cons(ctx, "ScreenPaint");

        if (xpost_object_get_type(sp) == invalidtype)
            return VMerror;
        if (xpost_dict_known_key(ctx, xpost_context_select_memory(ctx, cls),
                                 cls, sp))
            ret = xpost_dict_put(ctx, classdic, sp,
                                 xpost_dict_get(ctx, cls, sp));
    }
    if (!ret)
        ret = xpost_dict_put(ctx, classdic, namedotplayclass, clsname);
    return ret;
}

/* Fill the class's method slots with this device's operators and define
   the class and its maker in userdict. */
static int loadrecorddevicecont(Xpost_Context *ctx,
                                Xpost_Object classdic)
{
    /* This device's whole suite, for a target declaring DeviceRGB. It is
       the five marking methods a record holds and nothing else that
       marks: a method it did not bring is resolved above the device into
       these, and one it brought would be a call the record has no entry
       for. */
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

    /* ... and the same suite for a target declaring DeviceGray, whose
       marks carry one colour value where these carry three. Two tables
       and not one, because a method's operand count is fixed when it is
       installed and an entry point's signature is fixed when it is
       compiled. The read is in both: its operands do not follow the
       colour space, so there is one of it. */
    static const Xpost_Dev_Method greymethods[] =
    {
        { "Create", "recordCreate", (Xpost_Op_Func)_create, XPOST_DEV_M_CREATE },
        { "PutPix", "recordGrayPutPix", (Xpost_Op_Func)_putpix_g, XPOST_DEV_M_PUTPIX },
        { "GetPix", "recordGetPix", (Xpost_Op_Func)_getpix, XPOST_DEV_M_GETPIX },
        { "BlendPix", "recordGrayBlendPix", (Xpost_Op_Func)_blendpix_g, XPOST_DEV_M_BLEND },
        { "DrawLine", "recordGrayDrawLine", (Xpost_Op_Func)_drawline_g, XPOST_DEV_M_LINE },
        { "FillRect", "recordGrayFillRect", (Xpost_Op_Func)_fillrect_g, XPOST_DEV_M_RECT },
        { "FillPoly", "recordGrayFillPoly", (Xpost_Op_Func)_fillpoly_g, XPOST_DEV_M_POLY },
        { "Emit", "recordEmit", (Xpost_Op_Func)_emit, XPOST_DEV_M_PAGE },
        { "Destroy", "recordDestroy", (Xpost_Op_Func)_destroy, XPOST_DEV_M_PAGE }
    };

    Xpost_Object userdict;
    Xpost_Object op;
    int ncomp;
    int ret;

    /* which device paints this record's page, before anything is
       installed: it decides what every marking method here looks like */
    ret = _play_target(ctx, classdic, &ncomp);
    if (ret)
        return ret;

    op = xpost_operator_cons(ctx, "recordCreateCont",
                             (Xpost_Op_Func)_create_cont, 3,
                             integertype, integertype, dicttype);
    _create_cont_opcode = op.mark_.padw;

    ret = ncomp == 1
        ? xpost_dev_class_install(ctx, classdic, ncomp, 1, greymethods,
                                  XPOST_DEV_METHOD_COUNT(greymethods))
        : xpost_dev_class_install(ctx, classdic, ncomp, 1, methods,
                                  XPOST_DEV_METHOD_COUNT(methods));
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

    /* What the painting machinery calls where the screen changes, for a
       target that screens. Like the image entry it is not a device
       method and is not dispatched as one: a method here would be a
       marking call the record holds no entry for, which is the one
       thing this class must not declare. A recorder whose target does
       not screen declares no ScreenPaint, and nothing calls this. */
    op = xpost_operator_cons(ctx, "recordScreen",
                             (Xpost_Op_Func)_recordscreen, 1, dicttype);
    ret = xpost_dict_put(ctx, classdic,
                         xpost_name_cons(ctx, "ScreenChanged"), op);
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
    if (xpost_object_get_type((namedotground = xpost_name_cons(ctx, ".ground"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namenativecolorspace = xpost_name_cons(ctx, "nativecolorspace"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namedotncomp = xpost_name_cons(ctx, ".ncomp"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nametextalphabits = xpost_name_cons(ctx, "TextAlphaBits"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namedotplaytargets = xpost_name_cons(ctx, ".playtargets"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namedotplayloaders = xpost_name_cons(ctx, ".playloaders"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namedotplayclass = xpost_name_cons(ctx, ".playclass"))) == invalidtype)
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
    op = xpost_operator_cons(ctx, ".recordplays", (Xpost_Op_Func)_recordplays,
                             1, dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".recordplayed", (Xpost_Op_Func)_recordplayed,
                             1, dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".recordscreens",
                             (Xpost_Op_Func)_recordscreens, 1,
                             dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".playscreen", (Xpost_Op_Func)_playscreen,
                             2, dicttype, dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".recordimagerows",
                             (Xpost_Op_Func)_recordimagerows, 1,
                             dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".recordground", (Xpost_Op_Func)_recordground,
                             3, dicttype, numbertype, numbertype); INSTALL;

    return 0;
}
