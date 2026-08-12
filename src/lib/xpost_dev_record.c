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

    return 0;
}
