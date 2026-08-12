/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 * (BSD 3-clause; see COPYING)
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "xpost_object.h"
#include "xpost_error.h"
#include "xpost_op_path.h"  /* XPOST_PATH_BREAK: what a subpath break is */
#include "xpost_strbuf.h"

#include "xpost_record.h"

/* A mark's place in the run of values, and the rows it reaches.
   Keeping the extent beside the mark rather than working it out at
   replay is what makes a replay's cost the marks it plays: a polygon's
   reach is a walk of its vertices, and a page is replayed once per band
   rather than once. */
typedef struct
{
    Xpost_Record_Kind kind;
    size_t at;                  /* where its values begin */
    int nops;                   /* how many operands follow the colour */
    real lo, hi;                /* the rows it reaches */
} _Mark;

/* Both runs are kept in the buffer the tree grows buffers with, so
   that a record's storage is grown the one way everything else is. The
   marks and the values are each a run of one kind of thing rather than
   text, and are read back through a pointer of that kind: what a
   buffer holds is the caller's to say, and its alignment is the
   allocator's, which suits anything. */
struct _Xpost_Record
{
    int ncomp;
    int short_of_a_mark;        /* a mark was given and could not be held */
    Xpost_String_Buffer mark;   /* a run of _Mark */
    Xpost_String_Buffer val;    /* colour and operands, run together */
};

static _Mark *_marks(const Xpost_Record *rec)
{
    return (_Mark *)rec->mark.s;
}

static real *_vals(const Xpost_Record *rec)
{
    return (real *)rec->val.s;
}

static size_t _nmark(const Xpost_Record *rec)
{
    return rec->mark.len / sizeof(_Mark);
}

Xpost_Record *xpost_record_new(int ncomp)
{
    Xpost_Record *rec;

    if (ncomp < 1)
        return NULL;
    rec = calloc(1, sizeof *rec);
    if (!rec)
        return NULL;
    if (xpost_strbuf_init(&rec->mark, 0) ||
        xpost_strbuf_init(&rec->val, 0))
    {
        xpost_strbuf_free(&rec->mark);
        xpost_strbuf_free(&rec->val);
        free(rec);
        return NULL;
    }
    rec->ncomp = ncomp;
    return rec;
}

void xpost_record_free(Xpost_Record *rec)
{
    if (!rec)
        return;
    xpost_strbuf_free(&rec->mark);
    xpost_strbuf_free(&rec->val);
    free(rec);
}

/* How many operands a kind carries after its colour, or -1 for one
   whose length is its own to state. */
static int _fixed_nops(Xpost_Record_Kind kind)
{
    switch (kind)
    {
        case XPOST_RECORD_PUTPIX:   return 2;
        case XPOST_RECORD_BLENDPIX: return 3;
        case XPOST_RECORD_DRAWLINE: return 4;
        case XPOST_RECORD_FILLRECT: return 4;
        case XPOST_RECORD_FILLPOLY: return -1;
    }
    return -1;
}

/* The rows a mark reaches, from its own operands. A rectangle's height
   may be negative -- the contract reflects such a rectangle through its
   origin rather than refusing it -- so both ends are taken. */
static void _extent(Xpost_Record_Kind kind, const real *ops, int nops,
                    real *lo, real *hi)
{
    real a, b;
    int i, any;

    switch (kind)
    {
        case XPOST_RECORD_PUTPIX:
            *lo = *hi = ops[1];
            return;
        case XPOST_RECORD_BLENDPIX:
            *lo = *hi = ops[2];
            return;
        case XPOST_RECORD_DRAWLINE:
            a = ops[1]; b = ops[3];
            break;
        case XPOST_RECORD_FILLRECT:
            a = ops[1]; b = ops[1] + ops[3];
            break;
        case XPOST_RECORD_FILLPOLY:
            /* n, then n pairs: the vertices are walked, which is the
               only kind whose reach is not read off two values. A pair
               marking a subpath break is a separator and not a point,
               and reaches no row: taken as one it would put the reach
               at the sentinel's own value and have the polygon met by
               every range there is. */
            *lo = *hi = 0.0;
            for (i = 0, any = 0; i * 2 + 2 < nops; i++)
            {
                real y;

                if (ops[i * 2 + 1] == XPOST_PATH_BREAK)
                    continue;
                y = ops[i * 2 + 2];
                if (!any) { *lo = *hi = y; any = 1; }
                else if (y < *lo) *lo = y;
                else if (y > *hi) *hi = y;
            }
            return;
    }
    *lo = a < b ? a : b;
    *hi = a < b ? b : a;
}

int xpost_record_mark(Xpost_Record *rec, Xpost_Record_Kind kind,
                      const real *colour, const real *ops, int nops)
{
    int fixed;
    size_t need;
    _Mark m2;

    if (!rec || !colour || (nops > 0 && !ops))
        return 0;
    /* a record already short of a mark describes a page it cannot
       reproduce, and adding to it would only make the gap harder to
       see */
    if (rec->short_of_a_mark)
        return 0;
    fixed = _fixed_nops(kind);
    if (fixed >= 0)
    {
        if (nops != fixed)
            return 0;
    }
    else if (kind == XPOST_RECORD_FILLPOLY)
    {
        /* the count and its pairs have to agree, so that a walk of the
           vertices stays inside what was written down */
        if (nops < 1 || ops[0] < 0 || nops != 1 + 2 * (int)ops[0])
            return 0;
    }
    else
        return 0;

    /* the values go down first, so that a mark is only written once
       there is somewhere for it to point at */
    need = rec->val.len / sizeof(real);
    m2.kind = kind;
    m2.at = need;
    m2.nops = nops;
    _extent(kind, ops, nops, &m2.lo, &m2.hi);

    if (xpost_strbuf_append(&rec->val, colour,
                            (size_t)rec->ncomp * sizeof *colour) ||
        (nops > 0 &&
         xpost_strbuf_append(&rec->val, ops, (size_t)nops * sizeof *ops)) ||
        xpost_strbuf_append(&rec->mark, &m2, sizeof m2))
    {
        rec->short_of_a_mark = 1;
        return 0;
    }
    return 1;
}

size_t xpost_record_count(const Xpost_Record *rec)
{
    return rec ? _nmark(rec) : 0;
}

int xpost_record_failed(const Xpost_Record *rec)
{
    return rec ? rec->short_of_a_mark : 0;
}

int xpost_record_get(const Xpost_Record *rec, size_t i,
                     Xpost_Record_Kind *kind, const real **colour,
                     const real **ops, int *nops)
{
    const _Mark *m;

    /* a record short of a mark describes a page it cannot reproduce, so
       it gives none of them back: what a caller would build from what
       is left is a page missing something, which looks like a page */
    if (!rec || rec->short_of_a_mark || i >= _nmark(rec))
        return 0;
    m = &_marks(rec)[i];
    *kind = m->kind;
    *colour = _vals(rec) + m->at;
    *ops = m->nops ? _vals(rec) + m->at + rec->ncomp : NULL;
    *nops = m->nops;
    return 1;
}

int xpost_record_extent(const Xpost_Record *rec, real *lo, real *hi)
{
    const _Mark *m;
    size_t i, n;

    if (!rec || !_nmark(rec))
        return 0;
    m = _marks(rec);
    n = _nmark(rec);
    *lo = m[0].lo;
    *hi = m[0].hi;
    for (i = 1; i < n; i++)
    {
        if (m[i].lo < *lo) *lo = m[i].lo;
        if (m[i].hi > *hi) *hi = m[i].hi;
    }
    return 1;
}

/* Whether a mark reaches a run of rows. A mark meeting the range at all
   is played whole: a shape has to be converted whole to be right about
   any part of it, so the range says which marks are played and never
   trims one. Stated once, because a replay reaches the marks two ways --
   as the loop below, and as the step a replay that returns to its caller
   between marks resumes with -- and the two picking different marks for
   the same rows would paint the same page differently depending on which
   asked. */
static int _meets(const _Mark *m, real lo, real hi)
{
    return !(m->hi < lo || m->lo > hi);
}

int xpost_record_next(const Xpost_Record *rec, size_t from, real lo, real hi,
                      size_t *at)
{
    const _Mark *marks;
    size_t i, n;

    /* a record short of a mark gives none of them back, on the same
       terms as a replay of one */
    if (!rec || !at || rec->short_of_a_mark)
        return 0;
    marks = _marks(rec);
    n = _nmark(rec);
    for (i = from; i < n; i++)
    {
        if (_meets(&marks[i], lo, hi))
        {
            *at = i;
            return 1;
        }
    }
    return 0;
}

int xpost_record_replay(const Xpost_Record *rec, real lo, real hi,
                        Xpost_Record_Player player, void *data)
{
    const _Mark *marks;
    const real *vals;
    size_t i, n;

    if (!rec || !player)
        return 0;
    /* what is played back has to be the whole of what was recorded: a
       record missing a mark would paint a page missing one, and a page
       missing a mark looks like a page */
    if (rec->short_of_a_mark)
        return VMerror;
    marks = _marks(rec);
    vals = _vals(rec);
    n = _nmark(rec);
    for (i = 0; i < n; i++)
    {
        const _Mark *m = &marks[i];
        int ret;

        if (!_meets(m, lo, hi))
            continue;
        ret = player(data, m->kind, vals + m->at,
                     m->nops ? vals + m->at + rec->ncomp : NULL,
                     m->nops);
        if (ret)
            return ret;
    }
    return 0;
}
