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

#include <stdio.h> /* snprintf */
#include <stdlib.h> /* abs */
#include <stddef.h>

#include <assert.h>
#include <math.h>
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
#include "xpost_file.h" /* raster emission */

#include "xpost_operator.h" /* create operators */
#include "xpost_op_dict.h" /* call xpost_op_any_load operator for convenience */
#include "xpost_dev_generic.h" /* check prototypes */

struct point
{
    real x, y;
};

/* marks a subpath separator in a point list */
#define SUBPATH_BREAK ((real)-0x7ffffff)

/* FIXME: re-entrancy */
static Xpost_Context *localctx;

static Xpost_Object namewidth;
static Xpost_Object namenativecolorspace;
static Xpost_Object nameDeviceGray;
static Xpost_Object nameDeviceRGB;
static Xpost_Object nameroll;
static Xpost_Object nameDrawLine;
static Xpost_Object nameexec;
static Xpost_Object namerepeat;
static Xpost_Object namecvx;
static Xpost_Object nameRbracket;
static Xpost_Object nameImgData;
static Xpost_Object nameFillRect;

char *xpost_device_get_filename(Xpost_Context *ctx, Xpost_Object devdic)
{
    Xpost_Object filenamestr;
    char *filename;

    filenamestr = xpost_dict_get(ctx, devdic,
                                 xpost_name_cons(ctx, "OutputFileName"));
    /* a device dict without a string OutputFileName -- e.g. after a program
       switches devices with setpagedevice, which records the name in userdict
       rather than the device dict -- must not be read as a string */
    if (xpost_object_get_type(filenamestr) != stringtype)
        return NULL;
    filename = malloc(filenamestr.comp_.sz + 1);
    if (filename)
    {
        memcpy(filename, xpost_string_get_pointer(ctx, filenamestr), filenamestr.comp_.sz);
        filename[filenamestr.comp_.sz] = '\0';
    }

    return filename;
}

int xpost_device_set_filename(Xpost_Context *ctx, Xpost_Object devdic, char *filename)
{
    Xpost_Object filenamestr;
    int ret;

    filenamestr = xpost_string_cons(ctx, strlen(filename), filename);
    if ((ret = xpost_dict_put(ctx, devdic, xpost_name_cons(ctx, "OutputFileName"), filenamestr)))
        return ret;
    return 0;
}

static
int _yxcomp(const void *left, const void *right)
{
    const Xpost_Object *lt = left;
    const Xpost_Object *rt = right;
    Xpost_Object leftx, lefty, rightx, righty;
    integer ltx, lty, rtx, rty;

    leftx = xpost_array_get(localctx, *lt, 0);
    lefty = xpost_array_get(localctx, *lt, 1);
    rightx = xpost_array_get(localctx, *rt, 0);
    righty = xpost_array_get(localctx, *rt, 1);
    ltx = xpost_object_get_type(leftx) == realtype ?
        (integer)leftx.real_.val : leftx.int_.val;
    lty = xpost_object_get_type(lefty) == realtype ?
        (integer)lefty.real_.val : lefty.int_.val;
    rtx = xpost_object_get_type(rightx) == realtype ?
        (integer)rightx.real_.val : rightx.int_.val;
    rty = xpost_object_get_type(righty) == realtype ?
        (integer)righty.real_.val : righty.int_.val;
    if (lty == rty)
    {
        if (ltx < rtx)
        {
            return 1;
        }
        else if (ltx > rtx)
        {
            return -1;
        } else
        {
            return 0;
        }
    }
    else
    {
        if (lty < rty)
            return -1;
        else
            return 1;
    }
}

static
int _yxsort (Xpost_Context *ctx, Xpost_Object arr)
{
    unsigned char *arrcontents;
    unsigned int arradr;
    Xpost_Memory_File *mem;

    mem = xpost_context_select_memory(ctx, arr);
    if (!xpost_memory_table_get_addr(mem, xpost_object_get_ent(arr), &arradr))
        return VMerror;
    arrcontents = (mem->base + arradr);

    localctx = ctx;
    qsort(arrcontents, arr.comp_.sz, sizeof(arr), _yxcomp);
    localctx = NULL;

    return 0;
}

/* One boundary-chain passage through a pixel-row band: the x extent
   [lo, hi] the chain covers within the band (row b covers device
   b <= y < b+1) and the chain's y direction (+1 rising, -1 falling) */
struct band_span
{
    int band;
    int dirn;
    real lo, hi;
};

static
int _bandspancomp (const void *left, const void *right)
{
    const struct band_span *lt = left;
    const struct band_span *rt = right;

    if (lt->band != rt->band)
        return lt->band < rt->band ? -1 : 1;
    if (lt->lo != rt->lo)
        return lt->lo < rt->lo ? -1 : 1;
    if (lt->hi != rt->hi)
        return lt->hi < rt->hi ? -1 : 1;
    return lt->dirn - rt->dirn;
}

/* append a span, growing the array as needed; 0 on success */
static
int _span_push(struct band_span **spans, int *cap, int *n,
               int band, int dirn, real lo, real hi)
{
    if (*n == *cap)
    {
        struct band_span *tmp;
        int newcap = *cap ? *cap * 2 : 64;

        tmp = realloc(*spans, newcap * sizeof *tmp);
        if (!tmp)
            return VMerror;
        *spans = tmp;
        *cap = newcap;
    }
    (*spans)[*n].band = band;
    (*spans)[*n].dirn = dirn;
    (*spans)[*n].lo = lo;
    (*spans)[*n].hi = hi;
    ++*n;
    return 0;
}

/* a winding-resolved fill span: the x extent the region covers within
   one pixel-row band, still in real device coordinates */
struct rspan
{
    int band;
    real lo, hi;
};

static
int _rspan_push(struct rspan **rsp, int *cap, int *n,
                int band, real lo, real hi)
{
    if (*n == *cap)
    {
        struct rspan *tmp;
        int newcap = *cap ? *cap * 2 : 64;

        tmp = realloc(*rsp, newcap * sizeof *tmp);
        if (!tmp)
            return VMerror;
        *rsp = tmp;
        *cap = newcap;
    }
    (*rsp)[*n].band = band;
    (*rsp)[*n].lo = lo;
    (*rsp)[*n].hi = hi;
    ++*n;
    return 0;
}

/* Scan-convert a null-separated polygon array to winding-resolved
   band spans (the shared middle of the fill pipeline: vertices in,
   sorted boundary passages accumulated to filled extents out).
   evenodd selects the insideness rule: 0 accumulates winding numbers
   to zero (nonzero rule), 1 counts boundary passages by parity
   (even-odd rule). The caller owns the returned buffer.
   0 on success. */
static
int _poly_resolved_spans(Xpost_Context *ctx,
                         Xpost_Object poly,
                         struct rspan **out,
                         int *nout,
                         int evenodd)
{
    struct point *points;
    struct band_span *spans;
    int nspans, spancap;
    struct rspan *rsp;
    int nrsp, rspcap;
    int i;

    *out = NULL;
    *nout = 0;

    /* extract polygon vertices from ps array;
       null elements separate subpaths */
    points = malloc(poly.comp_.sz * sizeof *points);
    if (!points)
        return VMerror;
    for (i = 0; i < poly.comp_.sz; i++)
    {
        Xpost_Object pair, x, y;

        pair = xpost_array_get(ctx, poly, i);
        if (xpost_object_get_type(pair) != arraytype)
        {
            points[i].x = SUBPATH_BREAK;
            points[i].y = SUBPATH_BREAK;
            continue;
        }
        x = xpost_array_get(ctx, pair, 0);
        y = xpost_array_get(ctx, pair, 1);
        if (xpost_object_get_type(x) == integertype)
            x = xpost_real_cons((real)x.int_.val);
        if (xpost_object_get_type(y) == integertype)
            y = xpost_real_cons((real)y.int_.val);
        /* quantize to a 1/256 pixel device grid: geometry meant to lie
           on a pixel boundary arrives with accumulated float noise, and
           unsnapped it would classify to the wrong side of the boundary */
        points[i].x = (real)(floor(x.real_.val * 256.0 + 0.5) / 256.0);
        points[i].y = (real)(floor(y.real_.val * 256.0 + 0.5) / 256.0);
    }

    /* Scan-convert under the any-part-of-pixel rule (PLRM 7.5): a
       pixel is painted when the filled region meets its interior.
       Device space divides into unit pixel-row bands (row b covers
       b <= y < b+1). Each subpath boundary is cut into y-monotone
       chains -- walking from a least-y vertex, so a chain never wraps
       the start/end seam -- and each chain deposits, for every band it
       passes through, the x extent of its passage tagged with its y
       direction. Horizontal travel widens the open extent, except
       travel exactly on a band boundary, which meets no band interior
       (an integer-aligned bottom edge must not leak into the band
       below). Sorting each band's extents by left edge and
       accumulating winding numbers then yields the fill spans. */
    spans = NULL;
    nspans = 0;
    spancap = 0;
    i = 0;
    for (;;)
    {
        int s0, nv, base, k;
        int dirn, ib, code;
        real lo, hi, submin, submax;

        while (i < poly.comp_.sz && points[i].x == SUBPATH_BREAK)
            i++;
        if (i == poly.comp_.sz)
            break;
        s0 = i;
        while (i < poly.comp_.sz && points[i].x != SUBPATH_BREAK)
            i++;
        nv = i - s0;

        base = 0;
        for (k = 1; k < nv; k++)
            if (points[s0 + k].y < points[s0 + base].y)
                base = k;

        /* chain state: the open extent, its band, and its direction
           (0 until the first non-horizontal edge; starting at a
           least-y vertex the first direction can only be upward) */
        dirn = 0;
        ib = (int)floor(points[s0 + base].y);
        lo = hi = points[s0 + base].x;
        submin = submax = lo;
        code = 0;

        for (k = 0; k < nv && code == 0; k++)
        {
            struct point P = points[s0 + (base + k) % nv];
            struct point Q = points[s0 + (base + k + 1) % nv];
            int d, eb;

            if (Q.x < submin) submin = Q.x;
            if (Q.x > submax) submax = Q.x;

            if (P.y == Q.y)
            {
                if (P.y == (real)floor(P.y))
                {
                    /* on a band boundary: deposits nothing; until the
                       chain has a direction just track the position */
                    if (dirn == 0)
                        lo = hi = Q.x;
                }
                else
                {
                    if (Q.x < lo) lo = Q.x;
                    if (Q.x > hi) hi = Q.x;
                }
                continue;
            }

            d = Q.y > P.y ? 1 : -1;
            /* the band this edge starts in: a start exactly on a band
               boundary belongs to the band ahead of travel */
            eb = (int)floor(P.y);
            if (d < 0 && (real)eb == P.y)
                eb--;

            if (d != dirn)
            {
                /* direction reversal: the vertex row holds two passages */
                if (dirn != 0)
                {
                    code = _span_push(&spans, &spancap, &nspans, ib, dirn, lo, hi);
                    lo = hi = P.x;
                }
                dirn = d;
                ib = eb;
            }
            else if (eb != ib)
            {
                /* the previous edge ended exactly on our starting boundary */
                code = _span_push(&spans, &spancap, &nspans, ib, dirn, lo, hi);
                lo = hi = P.x;
                ib = eb;
            }

            /* walk the edge band to band, cutting at each boundary */
            while (code == 0)
            {
                real yb = (real)(d > 0 ? ib + 1 : ib);

                if (d > 0 ? Q.y > yb : Q.y < yb)
                {
                    real xb = P.x + (Q.x - P.x) * ((yb - P.y) / (Q.y - P.y));

                    if (xb < lo) lo = xb;
                    if (xb > hi) hi = xb;
                    code = _span_push(&spans, &spancap, &nspans, ib, dirn, lo, hi);
                    ib += d;
                    lo = hi = xb;
                }
                else
                {
                    if (Q.x < lo) lo = Q.x;
                    if (Q.x > hi) hi = Q.x;
                    break;
                }
            }
        }

        if (code == 0)
        {
            if (dirn != 0)
                code = _span_push(&spans, &spancap, &nspans, ib, dirn, lo, hi);
            else
            {
                /* no vertical travel at all: the subpath still meets its
                   row; deposit a balanced pair over its whole x extent */
                code = _span_push(&spans, &spancap, &nspans, ib, 1, submin, submax);
                if (code == 0)
                    code = _span_push(&spans, &spancap, &nspans, ib, -1, submin, submax);
            }
        }
        if (code)
        {
            free(points);
            free(spans);
            return code;
        }
    }
    free(points);

    /* nspans can be zero for a degenerate row, leaving spans NULL; passing a
       null pointer to qsort is undefined even for a zero count, and there is
       nothing to order below two spans anyway */
    if (nspans > 1)
        qsort(spans, nspans, sizeof *spans, _bandspancomp);

    /* Walk each band accumulating winding: a span opens at the first
       extent's left edge and closes where the winding count returns to
       zero (or the band runs out), covering the rightmost extent seen. */
    rsp = NULL;
    nrsp = 0;
    rspcap = 0;
    {
        int s = 0;

        while (s < nspans)
        {
            int b = spans[s].band;
            int wind = 0;
            real L = spans[s].lo, R = spans[s].hi;
            int code;

            do
            {
                if (spans[s].hi > R)
                    R = spans[s].hi;
                wind += spans[s].dirn;
                s++;
            } while ((evenodd ? (wind & 1) : wind) != 0
                     && s < nspans && spans[s].band == b);

            code = _rspan_push(&rsp, &rspcap, &nrsp, b, L, R);
            if (code)
            {
                free(spans);
                free(rsp);
                return code;
            }
        }
    }
    free(spans);

    *out = rsp;
    *nout = nrsp;
    return 0;
}

static
int _fillpoly(Xpost_Context *ctx,
              Xpost_Object poly,
              Xpost_Object devdic)
{
    Xpost_Object colorspace;
    int ncomp;
    Xpost_Object comp1, comp2, comp3;
    int numlines;
    /* Xpost_Object x1, y1, x2, y2; */
    Xpost_Object drawline;
    Xpost_Object fillrect;
    int usefillrect;
    struct rspan *rsp;
    int nrsp;
    int i;
    //int width;

    //printf("_fillpoly\n");

    //width = xpost_dict_get(ctx, devdic, namewidth).int_.val;
    colorspace = xpost_dict_get(ctx, devdic, namenativecolorspace);
    if (xpost_dict_compare_objects(ctx, colorspace, nameDeviceGray) == 0)
    {
        ncomp = 1;
        comp1 = xpost_stack_pop(ctx->lo, ctx->os);
    }
    else if (xpost_dict_compare_objects(ctx, colorspace, nameDeviceRGB) == 0)
    {
        ncomp = 3;
        comp3 = xpost_stack_pop(ctx->lo, ctx->os);
        comp2 = xpost_stack_pop(ctx->lo, ctx->os);
        comp1 = xpost_stack_pop(ctx->lo, ctx->os);
    }
    else
    {
        XPOST_LOG_ERR("unimplemented device color space");
        return unregistered;
    }

    {
        int code = _poly_resolved_spans(ctx, poly, &rsp, &nrsp, 0);

        if (code)
            return code;
    }

    /* A fill scanline is a horizontal span. When the device provides a
       compiled FillRect, render each span through it (the per-pixel plotting
       then happens in C rather than a PostScript DrawLine/PutPix loop);
       otherwise fall back to DrawLine unchanged. Both take the same colour
       components plus four numbers, so the loop body and colour roll below are
       identical either way. */
    fillrect = xpost_dict_get(ctx, devdic, nameFillRect);
    usefillrect = xpost_object_get_type(fillrect) == operatortype;

    /* Paint columns [floor(lo), ceil(hi)): every pixel whose interior
       the span reaches, and exactly the geometry when the span lies on
       pixel boundaries. FillRect fills the inclusive box [x, x+w] on
       row y (a fill span is height 0); DrawLine plots from its first
       point (included) toward its second (excluded); both therefore
       cover [xlo, xhi-1]. */
    numlines = 0;
    for (i = 0; i < nrsp; i++)
    {
        integer xlo = (integer)floor(rsp[i].lo);
        integer xhi = (integer)ceil(rsp[i].hi);
        int b = rsp[i].band;

        if (xhi <= xlo)
            continue;
        if (usefillrect)
        {
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(xlo));
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(b));
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(xhi - xlo - 1));
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(0)); /* h */
        }
        else
        {
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(xlo));
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(b));
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(xhi));
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(b));
        }
        numlines++;
    }

    /*call the device's DrawLine generically with continuations.
      each call to DrawLine looks like this

         comp1 (comp2 comp3)? x1 y1 x2 y2 DEVICE >-- DrawLine

     So what we'll do is push all the points on the stack */

    /*for each line: */
    /*
        xpost_stack_push(ctx->lo, ctx->os, x1);
        xpost_stack_push(ctx->lo, ctx->os, y1);
        xpost_stack_push(ctx->lo, ctx->os, x2);
        xpost_stack_push(ctx->lo, ctx->os, y2);
    */

    /*the loop body and continuation are built from operator objects,
     not executable names, so a user definition of /roll or /repeat on
     the dict stack cannot capture them mid-fill */
    /*then we'll use a repeat loop to call DrawLine
     on each set of 4 numbers. But in order to treat the color space
     generically, we construct the loop body dynamically. */

    /*first push the number of elements
     remember we're using a repeat loop which looks like:
         count proc  -repeat-
     so this line places the `count` parameter on the stack
    */
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(numlines));

    /*then push a mark object to begin array construction
     this array is our loop body */
    xpost_stack_push(ctx->lo, ctx->os, mark);

    /*the loop body finds the 4 coordinate numbers on the stack
     and must roll the color values beneath these numbers on the stack  */

    switch (ncomp)
    {
        case 1:
            xpost_stack_push(ctx->lo, ctx->os, comp1);
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(5)); /* total elements to roll */
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(1)); /* color components to move */
            break;
        case 3:
            xpost_stack_push(ctx->lo, ctx->os, comp1);
            xpost_stack_push(ctx->lo, ctx->os, comp2);
            xpost_stack_push(ctx->lo, ctx->os, comp3);
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(7)); /* total elements to roll */
            xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(3)); /* color components to move */
            break;
    }
    xpost_stack_push(ctx->lo, ctx->os, xpost_operator_cons(ctx, "roll", NULL, 0, 0));

      /*at this point (in constructing the (color-space-generic) loop-body) we have the desired stack picture:

             comp1 (comp2 comp3)? x1 y1 x2 y2

        (with possibly more pairs deeper on the stack, waiting for the next iteration),
        just need to push the devdic (ie. the DEVICE object, in OO-speak) and DrawLine,
        then cinch-off the loop-body procedure (array), make it executable, and call
        the `repeat` operator.
       */

    xpost_stack_push(ctx->lo, ctx->os, devdic);
    if (usefillrect)
    {
        xpost_stack_push(ctx->lo, ctx->os, fillrect);
    }
    else
    {
        drawline = xpost_dict_get(ctx, devdic, nameDrawLine);
        xpost_stack_push(ctx->lo, ctx->os, drawline);

        /*if drawline is a procedure, we also need to call exec */
        if (xpost_object_get_type(drawline) == arraytype)
            xpost_stack_push(ctx->lo, ctx->os, xpost_operator_cons(ctx, "exec", NULL, 0, 0));
    }

    /*--the rest of the code here calls-back to postscript (by "continuation")
        by pushing executable names on the execution-stack, and then returns.
        The (color-space-) generic loop-body is called with the
        `repeat` looping-operator.-------------------------------------------*/

    /*Then construct the loop-body procedure array. Just showing you the line here.
      Read the whole story-line of comments for why we're not just executing it here. */
       //xpost_stack_push(ctx->lo, ctx->es, xpost_object_cvx(xpost_name_cons(ctx, "]")));

    /*Then, after the loop-body array is constructed, we need to call cvx on it. */
       //xpost_stack_push(ctx->lo, ctx->es, xpost_object_cvx(xpost_name_cons(ctx, "cvx")));
    /*"after" means this line, which pushes on the stack, goes *before* the xpost_name_cons("]") line.
     I'll summarize this part again. */

    /*After this, we call `repeat` and we're done. */
        //xpost_stack_push(ctx->lo, ctx->es, xpost_object_cvx(xpost_name_cons(ctx, "repeat")));

    /*Again since these are scheduled on a stack, we need to push them in reverse order
      from the order in which we desire them to execute.
      What we're doing is:

      opstack> xyxy xyxy xyxy ... xyxy numlines [ comp1 5 1 roll DEVICE DrawLine (exec)?
      -or for rgb color values-:
                                   ... numlines [ comp1 comp2 comp3 7 3 roll DEVICE DrawLine (exec)?
      execstack> repeat cvx ]
                            ^ construct array
                         ^ make executable
                   ^ call the loop operator

      So the sequence in C is:
     */

    xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons(ctx, "repeat", NULL, 0, 0));
    xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons(ctx, "cvx", NULL, 0, 0));
    xpost_stack_push(ctx->lo, ctx->es, xpost_operator_cons(ctx, "]", NULL, 0, 0));

    /*performance could be increased by factoring-out calls to xpost_name_cons()  ... DONE!
      or using opcode shortcuts for Rbracket & cvx (or just the arrtomark() function) and repeat.
     */
    free(rsp);
    return 0;
}

/* Build a null-separated polygon array of pixel-band rectangles, one
   per resolved span, in the FillPoly argument format: winding-uniform
   output any consumer may treat by either insideness rule. Consumes
   nothing; pushes the array on the operand stack. 0 on success. */
static
int _rspans_to_poly(Xpost_Context *ctx,
                    struct rspan *out,
                    int nout)
{
    Xpost_Object result;
    int i;

    if (5 * (long)nout > 65535)
        /* too many spans for a single backing array (the object size
           field is 16 bits) */
        return limitcheck;

    result = xpost_array_cons(ctx, 5 * nout);
    if (xpost_object_get_type(result) == invalidtype)
        return VMerror;
    for (i = 0; i < nout; i++)
    {
        static const int xsel[4] = { 0, 1, 1, 0 };  /* lo hi hi lo */
        static const int ysel[4] = { 0, 0, 1, 1 };  /* b  b  b+1 b+1 */
        int k;

        for (k = 0; k < 4; k++)
        {
            Xpost_Object pair = xpost_array_cons(ctx, 2);

            if (xpost_object_get_type(pair) == invalidtype)
                return VMerror;
            xpost_array_put(ctx, pair, 0,
                xpost_real_cons(xsel[k] ? out[i].hi : out[i].lo));
            xpost_array_put(ctx, pair, 1,
                xpost_real_cons((real)(out[i].band + ysel[k])));
            xpost_array_put(ctx, result, 5 * i + k, pair);
        }
        xpost_array_put(ctx, result, 5 * i + 4, null);
    }

    xpost_stack_push(ctx->lo, ctx->os, xpost_object_cvlit(result));
    return 0;
}

/* subjectpoly clippoly  .clipfillpoly  spanpoly
   Intersect two filled regions, each a null-separated polygon array in
   the FillPoly argument format, under the nonzero winding rule, and
   return the intersection as one such array of pixel-band rectangles.
   This is the exact boolean the clip machinery needs for regions the
   half-plane clipper cannot express -- many disjoint windows, concave
   boundaries, counters -- resolved span-by-span at device resolution:
   both operands scan-convert to winding-resolved band extents, and
   each band contributes the pairwise overlaps of its extents. */
static
int _clipfillpoly(Xpost_Context *ctx,
                  Xpost_Object subj,
                  Xpost_Object clip)
{
    struct rspan *S = NULL, *C = NULL, *out = NULL;
    int nS, nC, nout, outcap;
    int si, ci;
    int code;

    code = _poly_resolved_spans(ctx, subj, &S, &nS, 0);
    if (code)
        return code;
    code = _poly_resolved_spans(ctx, clip, &C, &nC, 0);
    if (code)
    {
        free(S);
        return code;
    }

    nout = 0;
    outcap = 0;
    si = ci = 0;
    while (si < nS && ci < nC)
    {
        if (S[si].band < C[ci].band)
            si++;
        else if (C[ci].band < S[si].band)
            ci++;
        else
        {
            /* one shared band: both extent runs are disjoint and
               ascending, so a linear merge finds every overlap */
            int b = S[si].band;
            int i2 = si, j2 = ci;

            while (i2 < nS && S[i2].band == b && j2 < nC && C[j2].band == b)
            {
                real L = S[i2].lo > C[j2].lo ? S[i2].lo : C[j2].lo;
                real R = S[i2].hi < C[j2].hi ? S[i2].hi : C[j2].hi;

                if (L < R)
                {
                    code = _rspan_push(&out, &outcap, &nout, b, L, R);
                    if (code)
                    {
                        free(S);
                        free(C);
                        free(out);
                        return code;
                    }
                }
                if (S[i2].hi < C[j2].hi)
                    i2++;
                else
                    j2++;
            }
            while (si < nS && S[si].band == b)
                si++;
            while (ci < nC && C[ci].band == b)
                ci++;
        }
    }
    free(S);
    free(C);

    code = _rspans_to_poly(ctx, out, nout);
    free(out);
    return code;
}

/* poly  .eospanpoly  spanpoly
   The even-odd interior of a filled region, returned as pixel-band
   rectangles in the FillPoly argument format. The rectangles are
   winding-uniform, so downstream nonzero machinery (the span
   intersection, the device fill) treats them exactly: this is how
   eofill and eoclip obtain the rule the nonzero pipeline lacks. */
static
int _eospanpoly(Xpost_Context *ctx,
                Xpost_Object poly)
{
    struct rspan *rsp = NULL;
    int nrsp;
    int code;

    code = _poly_resolved_spans(ctx, poly, &rsp, &nrsp, 1);
    if (code)
        return code;

    code = _rspans_to_poly(ctx, rsp, nrsp);
    free(rsp);
    return code;
}

/* A colour component scaled to a 0..max channel value. The component is
   clamped to [0,1] first: the colour pipeline can hand a device an
   out-of-range component, and unclamped it would wrap the byte or shift
   sign bits across the packed pixel. */
static double
_channel(Xpost_Object v, double max)
{
    double d = xpost_object_get_type(v) == realtype
             ? v.real_.val : (double)v.int_.val;
    if (d < 0.0) d = 0.0;
    if (d > 1.0) d = 1.0;
    return d * max;
}

/* Fast FillRect for grayscale (DeviceGray) array-of-strings devices such as
   PGMIMAGE. Writes the ImgData row strings directly rather than looping over
   PutPix in PostScript; erasepage clears the whole page through FillRect, so
   the per-pixel interpreter overhead otherwise dominates page emission.
   Mirrors PGMIMAGE's FillRect/PutPix handling exactly: value scaled by 255 and
   truncated to a byte, coordinates floored, negative extents normalised,
   inclusive end coordinates, and bounds clipping (rows via ImgData length,
   columns via each row string's length). */
static
int _fillrectgray(Xpost_Context *ctx,
                  Xpost_Object val,
                  Xpost_Object x,
                  Xpost_Object y,
                  Xpost_Object w,
                  Xpost_Object h,
                  Xpost_Object devdic)
{
    Xpost_Object imgdata, row;
    double dx, dy, dw, dh;
    int height, iy, iy0, iy1, ix0, ix1;
    unsigned char b;

    imgdata = xpost_dict_get(ctx, devdic, nameImgData);
    if (xpost_object_get_type(imgdata) != arraytype)
        return undefined;
    height = imgdata.comp_.sz;

    /* value -> byte, matching PGMIMAGE PutPix "255 mul cvi put" */
    b = (unsigned char)(int)_channel(val, 255.0);

    dx = xpost_object_get_type(x) == realtype ? x.real_.val : (double)x.int_.val;
    dy = xpost_object_get_type(y) == realtype ? y.real_.val : (double)y.int_.val;
    dw = xpost_object_get_type(w) == realtype ? w.real_.val : (double)w.int_.val;
    dh = xpost_object_get_type(h) == realtype ? h.real_.val : (double)h.int_.val;

    /* normalise negative extents, then form inclusive end coords */
    if (dw < 0) { dw = -dw; dx -= dw; }
    if (dh < 0) { dh = -dh; dy -= dh; }
    ix0 = (int)floor(dx);
    iy0 = (int)floor(dy);
    ix1 = (int)floor(dx + dw);
    iy1 = (int)floor(dy + dh);

    /* clip rows to the device */
    if (iy0 < 0) iy0 = 0;
    if (iy1 > height - 1) iy1 = height - 1;

    for (iy = iy0; iy <= iy1; iy++)
    {
        int width, cx0, cx1;
        row = xpost_array_get(ctx, imgdata, iy);
        width = row.comp_.sz;
        cx0 = ix0 < 0 ? 0 : ix0;
        cx1 = ix1 > width - 1 ? width - 1 : ix1;
        if (cx0 <= cx1)
            memset(xpost_string_get_pointer(ctx, row) + cx0, b,
                   (size_t)(cx1 - cx0 + 1));
    }

    return 0;
}

/* Fill a rectangle of a packed-integer rgb device (each row an array
   of r<<16|g<<8|b). Mirrors PPMIMAGE PutPix handling: each channel
   scaled by 255 and truncated, coordinates floored, negative extents
   normalised, inclusive end coordinates, and bounds clipping. The rgb
   devices render continuous tone, so no halftone cell applies. */
static
int _fillrectrgb(Xpost_Context *ctx,
                 Xpost_Object r,
                 Xpost_Object g,
                 Xpost_Object b,
                 Xpost_Object x,
                 Xpost_Object y,
                 Xpost_Object w,
                 Xpost_Object h,
                 Xpost_Object devdic)
{
    Xpost_Object imgdata, row;
    double dx, dy, dw, dh;
    int height, iy, ix, iy0, iy1, ix0, ix1;
    int packed;

    imgdata = xpost_dict_get(ctx, devdic, nameImgData);
    if (xpost_object_get_type(imgdata) != arraytype)
        return undefined;
    height = imgdata.comp_.sz;

    packed = ((int)_channel(r, 255.0) << 16)
           | ((int)_channel(g, 255.0) << 8)
           |  (int)_channel(b, 255.0);

    dx = xpost_object_get_type(x) == realtype ? x.real_.val : (double)x.int_.val;
    dy = xpost_object_get_type(y) == realtype ? y.real_.val : (double)y.int_.val;
    dw = xpost_object_get_type(w) == realtype ? w.real_.val : (double)w.int_.val;
    dh = xpost_object_get_type(h) == realtype ? h.real_.val : (double)h.int_.val;

    /* normalise negative extents, then form inclusive end coords */
    if (dw < 0) { dw = -dw; dx -= dw; }
    if (dh < 0) { dh = -dh; dy -= dh; }
    ix0 = (int)floor(dx);
    iy0 = (int)floor(dy);
    ix1 = (int)floor(dx + dw);
    iy1 = (int)floor(dy + dh);

    /* clip rows to the device */
    if (iy0 < 0) iy0 = 0;
    if (iy1 > height - 1) iy1 = height - 1;

    for (iy = iy0; iy <= iy1; iy++)
    {
        int width, cx0, cx1;
        row = xpost_array_get(ctx, imgdata, iy);
        if (xpost_object_get_type(row) != arraytype)
            return undefined;
        width = row.comp_.sz;
        cx0 = ix0 < 0 ? 0 : ix0;
        cx1 = ix1 > width - 1 ? width - 1 : ix1;
        for (ix = cx0; ix <= cx1; ix++)
            xpost_array_put(ctx, row, ix, xpost_int_cons(packed));
    }

    return 0;
}

/* Set every pixel of a packed-integer raster (an array of row arrays)
   to integer zero. Devices call this once from Create; initialising
   each element from PostScript costs an interpreter loop per pixel. */
static
int _zerorows(Xpost_Context *ctx, Xpost_Object imgdata)
{
    int iy, ix;

    for (iy = 0; iy < imgdata.comp_.sz; iy++)
    {
        Xpost_Object row = xpost_array_get(ctx, imgdata, iy);
        if (xpost_object_get_type(row) != arraytype)
            return typecheck;
        for (ix = 0; ix < row.comp_.sz; ix++)
            xpost_array_put(ctx, row, ix, xpost_int_cons(0));
    }
    return 0;
}

/* Write bytes to an emission target, routing through the registered
   stdout/stderr handler when one has claimed the stream, as the
   writestring operator does. */
static
int _emit_write(Xpost_Context *ctx, Xpost_File *f,
                const unsigned char *buf, size_t len)
{
    FILE *stream = xpost_file_stdio_stream_get(f);

    if (stream == stdout && ctx->stdout_fn)
        return ctx->stdout_fn(ctx->stdout_user, (const char *)buf, len) == len ? 0 : -1;
    if (stream == stderr && ctx->stderr_fn)
        return ctx->stderr_fn(ctx->stderr_user, (const char *)buf, len) == len ? 0 : -1;
    return xpost_file_write((const char *)buf, 1, (int)len, f) == (int)len ? 0 : -1;
}

/* Emit a grayscale array-of-strings raster as a binary P4 PBM:
   header, then each row's bytes thresholded at half coverage (black
   below 128) and packed most significant bit first. */
static
int _writepbmrows(Xpost_Context *ctx,
                  Xpost_Object imgdata,
                  Xpost_Object F)
{
    Xpost_File *f;
    Xpost_Object row;
    unsigned char *buf;
    char head[32];
    int width, height, rb, iy, ix, hn;

    if (!xpost_file_get_status(ctx->lo, F))
        return ioerror;
    if (!xpost_object_is_writeable(ctx, F))
        return invalidaccess;
    f = xpost_file_get_file_pointer(ctx->lo, F);

    height = imgdata.comp_.sz;
    if (height == 0)
        return rangecheck;
    row = xpost_array_get(ctx, imgdata, 0);
    if (xpost_object_get_type(row) != stringtype)
        return typecheck;
    width = row.comp_.sz;
    rb = (width + 7) / 8;

    hn = snprintf(head, sizeof head, "P4\n%d %d\n", width, height);
    if (_emit_write(ctx, f, (unsigned char *)head, (size_t)hn) < 0)
        return ioerror;

    buf = malloc((size_t)rb);
    if (!buf)
        return VMerror;
    for (iy = 0; iy < height; iy++)
    {
        const unsigned char *p;

        row = xpost_array_get(ctx, imgdata, iy);
        if (xpost_object_get_type(row) != stringtype
            || row.comp_.sz != width)
        {
            free(buf);
            return typecheck;
        }
        p = (unsigned char *)xpost_string_get_pointer(ctx, row);
        memset(buf, 0, (size_t)rb);
        for (ix = 0; ix < width; ix++)
            if (p[ix] < 128)
                buf[ix / 8] |= 0x80 >> (ix % 8);
        if (_emit_write(ctx, f, buf, (size_t)rb) < 0)
        {
            free(buf);
            return ioerror;
        }
    }
    free(buf);
    return 0;
}

/* Emit a packed-integer rgb raster as a binary P6 PPM: header, then
   three bytes per pixel unpacked from each row's r<<16|g<<8|b
   integers. Emitting from PostScript costs several string operations
   per pixel, which dominates page output time. */
static
int _writeppmrows(Xpost_Context *ctx,
                  Xpost_Object imgdata,
                  Xpost_Object F)
{
    Xpost_File *f;
    Xpost_Object row;
    unsigned char *buf;
    char head[32];
    int width, height, iy, ix, hn;

    if (!xpost_file_get_status(ctx->lo, F))
        return ioerror;
    if (!xpost_object_is_writeable(ctx, F))
        return invalidaccess;
    f = xpost_file_get_file_pointer(ctx->lo, F);

    height = imgdata.comp_.sz;
    if (height == 0)
        return rangecheck;
    row = xpost_array_get(ctx, imgdata, 0);
    if (xpost_object_get_type(row) != arraytype)
        return typecheck;
    width = row.comp_.sz;

    hn = snprintf(head, sizeof head, "P6\n%d %d\n255\n", width, height);
    if (_emit_write(ctx, f, (unsigned char *)head, (size_t)hn) < 0)
        return ioerror;

    buf = malloc((size_t)width * 3);
    if (!buf)
        return VMerror;
    for (iy = 0; iy < height; iy++)
    {
        row = xpost_array_get(ctx, imgdata, iy);
        if (xpost_object_get_type(row) != arraytype
            || row.comp_.sz != width)
        {
            free(buf);
            return typecheck;
        }
        for (ix = 0; ix < width; ix++)
        {
            Xpost_Object pix = xpost_array_get(ctx, row, ix);
            int packed = xpost_object_get_type(pix) == integertype
                       ? pix.int_.val : 0;

            buf[ix * 3]     = (unsigned char)((packed >> 16) & 0xff);
            buf[ix * 3 + 1] = (unsigned char)((packed >> 8) & 0xff);
            buf[ix * 3 + 2] = (unsigned char)(packed & 0xff);
        }
        if (_emit_write(ctx, f, buf, (size_t)width * 3) < 0)
        {
            free(buf);
            return ioerror;
        }
    }
    free(buf);
    return 0;
}


/* decode one interleaved normalized sample row to native colour,
   one r,g,b triple per pixel (grey rides in all three), through the
   same tables the direct path uses */
static void
_blit_decode_row(const unsigned char *src, unsigned char *const *planes,
                 int w, int ncomp,
                 const unsigned char *lut, unsigned char *const dlut[4],
                 const unsigned char *tlut, int cmyk, int nat, int *out)
{
    int x, c;

#define DECSAMP(x, c) (planes ? planes[c][x] : src[(x) * ncomp + (c)])
    for (x = 0; x < w; x++)
    {
        int r = 0, g = 0, b = 0;

        if (lut)
        {
            const unsigned char *e = lut + DECSAMP(x, 0) * nat;

            if (nat == 3) { r = e[0]; g = e[1]; b = e[2]; }
            else r = g = b = e[0];
        }
        else
        {
            int v[4];

            for (c = 0; c < ncomp; c++)
                v[c] = dlut[c][DECSAMP(x, c)];
            if (cmyk)
            {
                r = 255 - (v[0] + v[3] > 255 ? 255 : v[0] + v[3]);
                g = 255 - (v[1] + v[3] > 255 ? 255 : v[1] + v[3]);
                b = 255 - (v[2] + v[3] > 255 ? 255 : v[2] + v[3]);
            }
            else
            {
                r = v[0];
                g = ncomp > 1 ? v[1] : v[0];
                b = ncomp > 2 ? v[2] : v[0];
            }
            if (nat == 3)
            {
                r = tlut[r]; g = tlut[g]; b = tlut[b];
            }
            else
                r = g = b = tlut[(r * 30 + g * 59 + b * 11) / 100];
        }
        out[x * 3] = r;
        out[x * 3 + 1] = g;
        out[x * 3 + 2] = b;
    }
#undef DECSAMP
}

/* collect the resolved clip spans overlapping one device row as
   x-intervals; shared by the stepped and interpolated writers */
static int
_blit_row_spans(Xpost_Context *ctx, Xpost_Object cspans, int ncspans,
                int dy, double ivl[512][2], int *nivl)
{
    int q;
    double t;

    *nivl = 0;
    for (q = 0; q < ncspans && *nivl < 512; q++)
    {
        double qx0, qy0, qx1, qy1;
        Xpost_Object e;
#define QGET(i, into) do { \
        e = xpost_array_get(ctx, cspans, q * 4 + (i)); \
        if (xpost_object_get_type(e) == realtype) into = e.real_.val; \
        else if (xpost_object_get_type(e) == integertype) into = e.int_.val; \
        else return typecheck; \
    } while (0)
        QGET(0, qx0); QGET(1, qy0); QGET(2, qx1); QGET(3, qy1);
#undef QGET
        if (qy0 > qy1) { t = qy0; qy0 = qy1; qy1 = t; }
        if (qx0 > qx1) { t = qx0; qx0 = qx1; qx1 = t; }
        if (qy0 < dy + 1 && qy1 > dy)
        {
            ivl[*nivl][0] = qx0;
            ivl[*nivl][1] = qx1;
            (*nivl)++;
        }
    }
    return 0;
}

/* blitdict  .blitrow  -
   write one image row straight into a raster device's page buffer.
   The dictionary carries the device raster (rows: the ImgData array,
   packed 24-bit integer pixels or grey bytes per the packed flag),
   the axis-aligned image-to-device mapping (xoff xscale yoff yscale),
   the clip rectangle (cx0 cy0 cx1 cy1), the normalized sample row
   (buf, one byte per sample, ncomp samples per pixel, row index y of
   w pixels), the colour tables -- lut: a full 256-entry table of
   native bytes for one-component spaces with everything baked in;
   else dluts: per-component decode tables and tlut: the transfer,
   applied after conversion (cmyk converts by additive complement) --
   and the masks: mbits, one bit per pixel high-order first in rows
   of mrowb bytes (set = leave unpainted), and mranges, raw min,max
   pairs (a pixel inside every range is left unpainted). Pixels cover
   device pixels by the any-part-of-pixel rule, the high edge
   exclusive, matching the rectangle fills this replaces. */
static
int _blitrow(Xpost_Context *ctx,
             Xpost_Object dict)
{
    Xpost_Object rows, bufo, luto, dlutso, tluto, mbitso, mrangeso;
    Xpost_Object cspans;
    unsigned char *plane[4] = { NULL, NULL, NULL, NULL };
    int ncspans = 0, have_cspans = 0, have_planes = 0;
    double ivl[512][2];
    int nivl = 0;
    unsigned char *buf, *lut = NULL, *tlut = NULL, *mbits = NULL;
    unsigned char *dlut[4] = { NULL, NULL, NULL, NULL };
    int mranges[8];
    int devw, devh, nat, packed, w, ncomp, y, cmyk, mrowb = 0;
    double xoff, xscale, yoff, yscale, cx0, cy0, cx1, cy1;
    double ya, yb, t;
    int dy, x, c, nranges = 0;

#define GETI(name) do { \
        Xpost_Object o_ = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, #name)); \
        if (xpost_object_get_type(o_) != integertype) return typecheck; \
        name = o_.int_.val; \
    } while (0)
#define GETR(name) do { \
        Xpost_Object o_ = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, #name)); \
        if (xpost_object_get_type(o_) == realtype) name = o_.real_.val; \
        else if (xpost_object_get_type(o_) == integertype) name = o_.int_.val; \
        else return typecheck; \
    } while (0)
#define GETB(name) do { \
        Xpost_Object o_ = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, #name)); \
        if (xpost_object_get_type(o_) != booleantype) return typecheck; \
        name = o_.int_.val; \
    } while (0)

    GETI(devw); GETI(devh); GETI(nat); GETI(w); GETI(ncomp); GETI(y);
    GETB(packed); GETB(cmyk);
    GETR(xoff); GETR(xscale); GETR(yoff); GETR(yscale);
    GETR(cx0); GETR(cy0); GETR(cx1); GETR(cy1);
#undef GETI
#undef GETR
#undef GETB

    rows = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "rows"));
    if (xpost_object_get_type(rows) != arraytype)
        return typecheck;
    /* planar sources deliver one row buffer per component */
    {
        Xpost_Object bufso = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "bufs"));
        if (xpost_object_get_type(bufso) == arraytype)
        {
            if (bufso.comp_.sz < (unsigned int)ncomp || ncomp > 4)
                return rangecheck;
            for (c = 0; c < ncomp; c++)
            {
                Xpost_Object b = xpost_array_get(ctx, bufso, c);
                if (xpost_object_get_type(b) != stringtype
                 || b.comp_.sz < (unsigned int)w)
                    return rangecheck;
                plane[c] = (unsigned char *)xpost_string_get_pointer(ctx, b);
            }
            have_planes = 1;
            buf = NULL;
        }
    }
    if (!have_planes)
    {
        bufo = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "buf"));
        if (xpost_object_get_type(bufo) != stringtype
         || bufo.comp_.sz < (unsigned int)(w * ncomp))
            return rangecheck;
        buf = (unsigned char *)xpost_string_get_pointer(ctx, bufo);
    }
#define SAMPLE(x, c) (have_planes ? plane[c][x] : buf[(x) * ncomp + (c)])

    luto = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "lut"));
    if (xpost_object_get_type(luto) == stringtype)
    {
        if (luto.comp_.sz < (unsigned int)(256 * nat))
            return rangecheck;
        lut = (unsigned char *)xpost_string_get_pointer(ctx, luto);
    }
    else
    {
        dlutso = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "dluts"));
        tluto = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "tlut"));
        if (xpost_object_get_type(dlutso) != arraytype
         || dlutso.comp_.sz < (unsigned int)ncomp
         || xpost_object_get_type(tluto) != stringtype
         || tluto.comp_.sz < 256)
            return typecheck;
        for (c = 0; c < ncomp && c < 4; c++)
        {
            Xpost_Object d = xpost_array_get(ctx, dlutso, c);
            if (xpost_object_get_type(d) != stringtype || d.comp_.sz < 256)
                return typecheck;
            dlut[c] = (unsigned char *)xpost_string_get_pointer(ctx, d);
        }
        tlut = (unsigned char *)xpost_string_get_pointer(ctx, tluto);
    }

    mbitso = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "mbits"));
    if (xpost_object_get_type(mbitso) == stringtype)
    {
        Xpost_Object o = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "mrowb"));
        if (xpost_object_get_type(o) != integertype)
            return typecheck;
        mrowb = o.int_.val;
        if (mbitso.comp_.sz < (unsigned int)((y + 1) * mrowb))
            return rangecheck;
        mbits = (unsigned char *)xpost_string_get_pointer(ctx, mbitso);
    }
    /* an optional clip region: flat quads x0 y0 x1 y1 in device
       space, the resolved rectangle spans of a non-rectangular clip;
       column runs intersect the quads overlapping the device row */
    {
        Xpost_Object cs = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "cspans"));
        if (xpost_object_get_type(cs) == arraytype)
        {
            cspans = cs;
            ncspans = cs.comp_.sz / 4;
            have_cspans = 1;
        }
    }

    mrangeso = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "mranges"));
    if (xpost_object_get_type(mrangeso) == arraytype)
    {
        nranges = mrangeso.comp_.sz;
        if (nranges > 8)
            return rangecheck;
        for (c = 0; c < nranges; c++)
        {
            Xpost_Object o = xpost_array_get(ctx, mrangeso, c);
            if (xpost_object_get_type(o) != integertype)
                return typecheck;
            mranges[c] = o.int_.val;
        }
    }

    /* Interpolate: between the previous sample row and this one, each
       device pixel takes the bilinear blend of the four surrounding
       decoded colours, so a magnified image ramps between its samples
       instead of stepping with them. The band between the two row
       centres belongs to this call; the first row also owns the band
       from its outer edge, the last also the band to its own. The
       masks decide per device pixel from its nearest sample -- the
       stepped rule -- while the colour still blends, and the resolved
       clip spans clamp writes as they do on the stepped path;
       reductions keep the stepped path. */
    {
        Xpost_Object io = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "interp"));

        if (xpost_object_get_type(io) == booleantype && io.int_.val
         && fabs(xscale) >= 1.0 && fabs(yscale) >= 1.0 && w > 0)
        {
            Xpost_Object po = xpost_dict_get(ctx, dict,
                xpost_name_cons(ctx, have_planes ? "prevs" : "prev"));
            Xpost_Object lasto = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "last"));
            int lastrow = xpost_object_get_type(lasto) == booleantype && lasto.int_.val;
            unsigned char *prevsamp = NULL;
            unsigned char *prevplane[4] = { NULL, NULL, NULL, NULL };
            int prevok = 0;

            if (have_planes)
            {
                if (xpost_object_get_type(po) == arraytype
                 && po.comp_.sz >= (unsigned int)ncomp)
                {
                    prevok = 1;
                    for (c = 0; c < ncomp; c++)
                    {
                        Xpost_Object b = xpost_array_get(ctx, po, c);
                        if (xpost_object_get_type(b) != stringtype
                         || b.comp_.sz < (unsigned int)w)
                            { prevok = 0; break; }
                        prevplane[c] = (unsigned char *)
                            xpost_string_get_pointer(ctx, b);
                    }
                }
            }
            else if (xpost_object_get_type(po) == stringtype
                  && po.comp_.sz >= (unsigned int)(w * ncomp))
            {
                prevsamp = (unsigned char *)xpost_string_get_pointer(ctx, po);
                prevok = 1;
            }

            if (prevok)
            {
                int *cols = malloc(sizeof(int) * (size_t)w * 6);
                int *pc, *cc;
                double xe0, xe1, bandlo[2], bandhi[2];
                int band, nband;

                if (!cols)
                    return VMerror;
                pc = cols;
                cc = cols + w * 3;
                _blit_decode_row(prevsamp, have_planes ? prevplane : NULL,
                                 w, ncomp, lut, dlut, tlut, cmyk, nat, pc);
                _blit_decode_row(buf, have_planes ? plane : NULL,
                                 w, ncomp, lut, dlut, tlut, cmyk, nat, cc);

                bandlo[0] = y == 0 ? yoff : yoff + (y - 0.5) * yscale;
                bandhi[0] = yoff + (y + 0.5) * yscale;
                nband = 1;
                if (lastrow)
                {
                    bandlo[1] = bandhi[0];
                    bandhi[1] = yoff + (y + 1) * yscale;
                    nband = 2;
                }

                xe0 = xoff; xe1 = xoff + w * xscale;
                if (xe0 > xe1) { t = xe0; xe0 = xe1; xe1 = t; }
                if (xe0 < cx0) xe0 = cx0;
                if (xe1 > cx1) xe1 = cx1;
                if (xe0 < 0) xe0 = 0;
                if (xe1 > devw) xe1 = devw;

                for (band = 0; band < nband; band++)
                {
                    double blo = bandlo[band], bhi = bandhi[band];
                    double lo = blo < bhi ? blo : bhi;
                    double hi = blo < bhi ? bhi : blo;
                    int *rowa = band ? cc : pc;
                    int *rowb = cc;

                    for (dy = (int)floor(lo); dy < hi; dy++)
                    {
                        Xpost_Object row;
                        unsigned char *rowp = NULL;
                        double v;
                        int dx;

                        if (dy < 0 || dy >= devh)
                            continue;
                        if (dy + 0.5 < cy0 || dy + 0.5 >= cy1)
                            continue;
                        v = bhi != blo ? (dy + 0.5 - blo) / (bhi - blo) : 0.0;
                        if (v < 0.0 || v >= 1.0)
                            continue;
                        if (have_cspans)
                        {
                            int ret = _blit_row_spans(ctx, cspans, ncspans,
                                                      dy, ivl, &nivl);
                            if (ret)
                                { free(cols); return ret; }
                            if (nivl == 0)
                                continue;
                        }
                        row = xpost_array_get(ctx, rows, dy);
                        if (packed)
                        {
                            if (xpost_object_get_type(row) != arraytype
                             || row.comp_.sz < (unsigned int)devw)
                                { free(cols); return rangecheck; }
                        }
                        else
                        {
                            if (xpost_object_get_type(row) != stringtype
                             || row.comp_.sz < (unsigned int)devw)
                                { free(cols); return rangecheck; }
                            rowp = (unsigned char *)xpost_string_get_pointer(ctx, row);
                        }
                        {
                        double sxstep = 1.0 / xscale;
                        double sxv = ((int)floor(xe0) + 0.5 - xoff) / xscale - 0.5;

                        for (dx = (int)floor(xe0); dx < xe1;
                             dx++, sxv += sxstep)
                        {
                            double f;
                            int i0, i1, k, px[3];

                            if (dx < 0 || dx >= devw)
                                continue;
                            if (have_cspans)
                            {
                                int q, hit = 0;
                                for (q = 0; q < nivl; q++)
                                    if (dx + 1 > ivl[q][0] && dx < ivl[q][1])
                                        { hit = 1; break; }
                                if (!hit)
                                    continue;
                            }
                            if (mbits || nranges)
                            {
                                /* the nearest sample decides, as the
                                   stepped rule would paint it */
                                int msy = band == 0 && v < 0.5 && y > 0
                                        ? y - 1 : y;
                                int xm = (int)floor((dx + 0.5 - xoff) / xscale);

                                if (xm < 0) xm = 0;
                                if (xm > w - 1) xm = w - 1;
                                if (mbits
                                 && (mbits[msy * mrowb + (xm >> 3)]
                                     >> (7 - (xm & 7)) & 1))
                                    continue;
                                if (nranges)
                                {
                                    int inside = 1;

                                    for (k = 0; k < ncomp; k++)
                                    {
                                        int sv = msy == y
                                            ? (int)SAMPLE(xm, k)
                                            : (int)(have_planes
                                                ? prevplane[k][xm]
                                                : prevsamp[xm * ncomp + k]);
                                        if (sv < mranges[2 * k]
                                         || sv > mranges[2 * k + 1])
                                            { inside = 0; break; }
                                    }
                                    if (inside)
                                        continue;
                                }
                            }
                            i0 = (int)floor(sxv);
                            f = sxv - i0;
                            if (i0 < 0) { i0 = 0; f = 0.0; }
                            if (i0 > w - 1) { i0 = w - 1; f = 0.0; }
                            i1 = i0 + 1 > w - 1 ? w - 1 : i0 + 1;
                            for (k = 0; k < 3; k++)
                            {
                                double a = rowa[i0 * 3 + k] * (1.0 - f)
                                         + rowa[i1 * 3 + k] * f;
                                double bl = rowb[i0 * 3 + k] * (1.0 - f)
                                          + rowb[i1 * 3 + k] * f;
                                double m_ = a * (1.0 - v) + bl * v;

                                px[k] = (int)(m_ + 0.5);
                                if (px[k] < 0) px[k] = 0;
                                if (px[k] > 255) px[k] = 255;
                            }
                            if (packed)
                                xpost_array_put(ctx, row, dx,
                                    xpost_int_cons(px[0] << 16 | px[1] << 8 | px[2]));
                            else
                                rowp[dx] = (unsigned char)px[0];
                        }
                        }
                    }
                }
                free(cols);
                return 0;
            }
        }
    }

    ya = yoff + y * yscale;
    yb = yoff + (y + 1) * yscale;
    if (ya > yb) { t = ya; ya = yb; yb = t; }
    if (ya < cy0) ya = cy0;
    if (yb > cy1) yb = cy1;
    if (ya < 0) ya = 0;
    if (yb > devh) yb = devh;

    for (dy = (int)floor(ya); dy < yb; dy++)
    {
        Xpost_Object row;
        unsigned char *rowp = NULL;

        if (dy < 0)
            continue;
        if (have_cspans)
        {
            int ret = _blit_row_spans(ctx, cspans, ncspans, dy, ivl, &nivl);
            if (ret)
                return ret;
            if (nivl == 0)
                continue;
        }
        row = xpost_array_get(ctx, rows, dy);
        if (packed)
        {
            if (xpost_object_get_type(row) != arraytype
             || row.comp_.sz < (unsigned int)devw)
                return rangecheck;
        }
        else
        {
            if (xpost_object_get_type(row) != stringtype
             || row.comp_.sz < (unsigned int)devw)
                return rangecheck;
            rowp = (unsigned char *)xpost_string_get_pointer(ctx, row);
        }

        for (x = 0; x < w; x++)
        {
            double xa, xb;
            int dx, r = 0, g = 0, b = 0, gray = 0;

            if (mbits)
            {
                int bit = mbits[y * mrowb + (x >> 3)] >> (7 - (x & 7)) & 1;
                if (bit)
                    continue;
            }
            if (nranges)
            {
                int inside = 1;
                for (c = 0; c < ncomp; c++)
                {
                    int v = SAMPLE(x, c);
                    if (v < mranges[2 * c] || v > mranges[2 * c + 1])
                    {
                        inside = 0;
                        break;
                    }
                }
                if (inside)
                    continue;
            }

            if (lut)
            {
                const unsigned char *e = lut + SAMPLE(x, 0) * nat;
                if (nat == 3) { r = e[0]; g = e[1]; b = e[2]; }
                else gray = e[0];
            }
            else
            {
                int v[4] = {0};
                for (c = 0; c < ncomp; c++)
                    v[c] = dlut[c][SAMPLE(x, c)];
                if (cmyk)
                {
                    r = 255 - (v[0] + v[3] > 255 ? 255 : v[0] + v[3]);
                    g = 255 - (v[1] + v[3] > 255 ? 255 : v[1] + v[3]);
                    b = 255 - (v[2] + v[3] > 255 ? 255 : v[2] + v[3]);
                }
                else
                {
                    r = v[0];
                    g = ncomp > 1 ? v[1] : v[0];
                    b = ncomp > 2 ? v[2] : v[0];
                }
                if (nat == 3)
                {
                    r = tlut[r]; g = tlut[g]; b = tlut[b];
                }
                else
                    gray = tlut[(r * 30 + g * 59 + b * 11) / 100];
            }

            xa = xoff + x * xscale;
            xb = xoff + (x + 1) * xscale;
            if (xa > xb) { t = xa; xa = xb; xb = t; }
            if (xa < cx0) xa = cx0;
            if (xb > cx1) xb = cx1;
            if (xa < 0) xa = 0;
            if (xb > devw) xb = devw;
            {
                int iv, niv = have_cspans ? nivl : 1;

                for (iv = 0; iv < niv; iv++)
                {
                    double sa = xa, sb = xb;

                    if (have_cspans)
                    {
                        if (ivl[iv][0] > sa) sa = ivl[iv][0];
                        if (ivl[iv][1] < sb) sb = ivl[iv][1];
                    }
                    for (dx = (int)floor(sa); dx < sb; dx++)
                    {
                        if (dx < 0)
                            continue;
                        if (packed)
                            xpost_array_put(ctx, row, dx,
                                            xpost_int_cons(r << 16 | g << 8 | b));
                        else
                            rowp[dx] = (unsigned char)gray;
                    }
                }
            }
        }
    }
    return 0;
}
#undef SAMPLE

int xpost_oper_init_generic_device_ops(Xpost_Context *ctx,
                                       Xpost_Object sd)
{
    unsigned int optadr;
    Xpost_Operator *optab;
    Xpost_Object n,op;

    xpost_memory_table_get_addr(ctx->gl,
                                XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE,
                                &optadr);
    optab = (Xpost_Operator *)(ctx->gl->base + optadr);

    op = xpost_operator_cons(ctx, ".yxsort", (Xpost_Op_Func)_yxsort, 0, 1, arraytype); INSTALL;
    op = xpost_operator_cons(ctx, ".fillpoly", (Xpost_Op_Func)_fillpoly, 0, 2, arraytype, dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".clipfillpoly", (Xpost_Op_Func)_clipfillpoly, 1, 2, arraytype, arraytype); INSTALL;
    op = xpost_operator_cons(ctx, ".eospanpoly", (Xpost_Op_Func)_eospanpoly, 1, 1, arraytype); INSTALL;
    op = xpost_operator_cons(ctx, ".blitrow", (Xpost_Op_Func)_blitrow, 0, 1, dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".fillrectgray", (Xpost_Op_Func)_fillrectgray, 0, 6,
            numbertype, numbertype, numbertype, numbertype, numbertype, dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".fillrectrgb", (Xpost_Op_Func)_fillrectrgb, 0, 8,
                             numbertype, numbertype, numbertype, numbertype,
                             numbertype, numbertype, numbertype, dicttype); INSTALL;
    op = xpost_operator_cons(ctx, ".zerorows", (Xpost_Op_Func)_zerorows, 0, 1, arraytype); INSTALL;
    op = xpost_operator_cons(ctx, ".writeppmrows", (Xpost_Op_Func)_writeppmrows, 0, 2,
                             arraytype, filetype); INSTALL;
    op = xpost_operator_cons(ctx, ".writepbmrows", (Xpost_Op_Func)_writepbmrows, 0, 2,
                             arraytype, filetype); INSTALL;
    if (xpost_object_get_type((nameImgData = xpost_name_cons(ctx, "ImgData"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameFillRect = xpost_name_cons(ctx, "FillRect"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namewidth = xpost_name_cons(ctx, "width"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namenativecolorspace = xpost_name_cons(ctx, "nativecolorspace"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameDeviceGray = xpost_name_cons(ctx, "DeviceGray"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameDeviceRGB = xpost_name_cons(ctx, "DeviceRGB"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameroll = xpost_name_cons(ctx, "roll"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameDrawLine = xpost_name_cons(ctx, "DrawLine"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameexec = xpost_name_cons(ctx, "exec"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namerepeat = xpost_name_cons(ctx, "repeat"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((namecvx = xpost_name_cons(ctx, "cvx"))) == invalidtype)
        return VMerror;
    if (xpost_object_get_type((nameRbracket = xpost_name_cons(ctx, "]"))) == invalidtype)
        return VMerror;

    return 0;
}
