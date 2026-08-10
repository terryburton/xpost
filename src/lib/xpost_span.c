/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 * (BSD 3-clause; see COPYING)
 */

/** \file xpost_span.c
   scan conversion: a boundary in, spans out
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <math.h>
#include <stdlib.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_object.h"
#include "xpost_error.h"
#include "xpost_op_path.h" /* XPOST_PATH_BREAK */
#include "xpost_span.h"

/* marks a subpath separator in a vertex list */
#define SUBPATH_BREAK XPOST_PATH_BREAK

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

/* Scan-convert a run of vertices to winding-resolved spans, stating
   each one to the consumer (the shared first half of the painting
   pipeline: vertices in, sorted boundary passages accumulated to filled
   extents out). A break entry ends one subpath and begins the next.

   evenodd selects the insideness rule of PLRM 4.5.2: 0 accumulates
   winding numbers to zero (the nonzero winding number rule, which fill
   and clip use), 1 counts boundary passages by parity (the even-odd
   rule, which eofill and eoclip use).

   rows, when given, is the inclusive band range to state spans for; the
   whole boundary is converted either way, since an insideness rule can
   only answer about part of a shape by counting all of it.

   The vertices are consumed -- the buffer is freed here whichever way
   the walk leaves. 0 on success; a consumer's refusal is returned
   unchanged and no further span is stated. */
int xpost_span_scanconvert(Xpost_Span_Vertex *points,
                           integer npoints,
                           int evenodd,
                           const Xpost_Span_Rows *rows,
                           Xpost_Span_Consumer *consumer)
{
    struct band_span *spans;
    int nspans, spancap;
    integer i;

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
        integer s0, nv, base, k;
        int dirn, ib, code;
        real lo, hi, submin, submax;

        while (i < npoints && points[i].x == SUBPATH_BREAK)
            i++;
        if (i == npoints)
            break;
        s0 = i;
        while (i < npoints && points[i].x != SUBPATH_BREAK)
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
            Xpost_Span_Vertex P = points[s0 + (base + k) % nv];
            Xpost_Span_Vertex Q = points[s0 + (base + k + 1) % nv];
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
       zero (or the band runs out), covering the rightmost extent seen.
       Every span the walk settles on passes through the consumer, and
       nothing here knows what becomes of it. */
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

            if (rows && (b < rows->lo || b > rows->hi))
                continue;

            code = consumer->take(consumer, b, L, R);
            if (code)
            {
                free(spans);
                return code;
            }
        }
    }
    free(spans);

    return 0;
}
