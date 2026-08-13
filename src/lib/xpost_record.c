/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 * (BSD 3-clause; see COPYING)
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <math.h>
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
/* One threshold cell, kept whole because a screen is read by the pixel
   and there is no part of it a replay can do without. */
typedef struct
{
    int w, h;
    unsigned char *cell;
} _Screen;

/* One coverage mask, and a digest of it. The digest is kept because
   taking a mask up asks whether the record already holds it, and a page
   of text asks that once per glyph on it: comparing the bytes against
   every mask held would make a line of text cost the masks times the
   glyphs, where comparing digests makes it cost the glyphs. The bytes
   are still compared where the digests agree, so what is shared is
   masks that are equal and not masks that collide. */
typedef struct
{
    int w, h;
    unsigned long long digest;
    unsigned char *cov;
} _Cover;

struct _Xpost_Record
{
    int ncomp;
    int short_of_a_mark;        /* a mark was given and could not be held */
    Xpost_String_Buffer mark;   /* a run of _Mark */
    Xpost_String_Buffer val;    /* colour and operands, run together */
    Xpost_String_Buffer img;    /* a run of Xpost_Record_Image */
    size_t imgbytes;            /* what the copies they point at cost */
    Xpost_String_Buffer msk;    /* a run of _Cover */
    size_t mskbytes;            /* what the coverage they point at costs */
    Xpost_String_Buffer scr;    /* a run of _Screen */
    size_t scrbytes;            /* what the cells they point at cost */
    /* The screen the marks are being made under, kept apart from the
       run so that it outlives a page: a page boundary is not a screen
       change, so the page beginning is written the same screen the page
       ending was painted under. */
    int htw, hth;
    unsigned char *htcell;
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

static Xpost_Record_Image *_imgs(const Xpost_Record *rec)
{
    return (Xpost_Record_Image *)rec->img.s;
}

static size_t _nimg(const Xpost_Record *rec)
{
    return rec->img.len / sizeof(Xpost_Record_Image);
}

static _Cover *_msks(const Xpost_Record *rec)
{
    return (_Cover *)rec->msk.s;
}

static size_t _nmsk(const Xpost_Record *rec)
{
    return rec->msk.len / sizeof(_Cover);
}

static _Screen *_scrs(const Xpost_Record *rec)
{
    return (_Screen *)rec->scr.s;
}

static size_t _nscr(const Xpost_Record *rec)
{
    return rec->scr.len / sizeof(_Screen);
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
        xpost_strbuf_init(&rec->val, 0) ||
        xpost_strbuf_init(&rec->img, 0) ||
        xpost_strbuf_init(&rec->msk, 0) ||
        xpost_strbuf_init(&rec->scr, 0))
    {
        xpost_strbuf_free(&rec->mark);
        xpost_strbuf_free(&rec->val);
        xpost_strbuf_free(&rec->img);
        xpost_strbuf_free(&rec->msk);
        xpost_strbuf_free(&rec->scr);
        free(rec);
        return NULL;
    }
    rec->ncomp = ncomp;
    return rec;
}

/* Give up the copies one image entry was made from. Every pointer in it
   is the record's own, so there is nothing here a caller still holds. */
static void _image_free(Xpost_Record_Image *img)
{
    free((void *)img->samples);
    free((void *)img->lut);
    free((void *)img->dluts);
    free((void *)img->tlut);
    free((void *)img->tlutrgb);
    free((void *)img->mbits);
    free((void *)img->mranges);
    free((void *)img->cspans);
    memset(img, 0, sizeof *img);
}

/* Give up the cells a record's screens were copied into. */
static void _screens_free(Xpost_Record *rec)
{
    size_t i, n = _nscr(rec);

    for (i = 0; i < n; i++)
        free(_scrs(rec)[i].cell);
    rec->scr.len = 0;
    rec->scrbytes = 0;
}

/* and the coverage its masks were copied into */
static void _masks_free(Xpost_Record *rec)
{
    size_t i, n = _nmsk(rec);

    for (i = 0; i < n; i++)
        free(_msks(rec)[i].cov);
    rec->msk.len = 0;
    rec->mskbytes = 0;
}

void xpost_record_free(Xpost_Record *rec)
{
    size_t i, n;

    if (!rec)
        return;
    n = _nimg(rec);
    for (i = 0; i < n; i++)
        _image_free(&_imgs(rec)[i]);
    _masks_free(rec);
    _screens_free(rec);
    free(rec->htcell);
    xpost_strbuf_free(&rec->mark);
    xpost_strbuf_free(&rec->val);
    xpost_strbuf_free(&rec->img);
    xpost_strbuf_free(&rec->msk);
    xpost_strbuf_free(&rec->scr);
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
        /* a polygon states its own length, and none of an image, a
           screen and a glyph is written down through xpost_record_mark
           at all */
        case XPOST_RECORD_FILLPOLY:
        case XPOST_RECORD_IMAGE:
        case XPOST_RECORD_SCREEN:
        case XPOST_RECORD_GLYPH:    return -1;
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
            /* A segment's ends are put on the 1/256 grid before it is
               walked (xpost_dev_line_quantize), and that can carry an
               end sitting a fraction below a row boundary over it: the
               row the segment ends on is then one past the row its own
               coordinates fall in. It cannot go the other way -- a
               whole row is itself a point of that grid, so a coordinate
               at or above one rounds to no less than it -- so the reach
               is taken one grid step further down and no further up. */
            if (a < b) b += 1.0 / 256.0; else a += 1.0 / 256.0;
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
        case XPOST_RECORD_IMAGE:
            /* an image's reach follows the transform that places it and
               is taken where it is written down, not from the one
               operand -- which names the entry rather than describing
               it */
            *lo = *hi = 0.0;
            return;
        case XPOST_RECORD_SCREEN:
            /* a screen reaches no row and every run of rows: it paints
               nothing, and governs whatever is painted after it wherever
               that lands, which is settled in _meets rather than here */
            *lo = *hi = 0.0;
            return;
        case XPOST_RECORD_GLYPH:
            /* a glyph's reach is its mask's height at the row it is put
               on, and is taken where it is written down: the operands
               name the mask rather than describing it */
            *lo = *hi = 0.0;
            return;
    }
    *lo = a < b ? a : b;
    *hi = a < b ? b : a;
}

/* The rows an image can be written into: the box its transform puts it
   in, met with the region its rows are written through.

   The low end is taken down to the row the first write lands in. A
   write starts at the row the box's edge falls inside, so a reach
   stated from the edge itself would leave a range meeting that row
   judging the image not to reach it -- and a mark judged not to reach a
   band is simply absent from the page, which is wrong output rather
   than slow output. Erring the other way costs a visit. */
static void _image_extent(const Xpost_Record_Image *img, real *lo, real *hi)
{
    real a = img->yoff;
    real b = img->yoff + (real)img->height * img->yscale;
    real t;

    if (a > b) { t = a; a = b; b = t; }
    if (a < img->cy0) a = img->cy0;
    if (b > img->cy1) b = img->cy1;
    *lo = (real)floor((double)a);
    *hi = b < *lo ? *lo : b;
}

/* Put one entry into the two runs: its values first, so that a mark is
   only written once there is somewhere for it to point at. */
static int _put(Xpost_Record *rec, Xpost_Record_Kind kind,
                const real *colour, const real *ops, int nops,
                real lo, real hi)
{
    _Mark m2;

    m2.kind = kind;
    m2.at = rec->val.len / sizeof(real);
    m2.nops = nops;
    m2.lo = lo;
    m2.hi = hi;

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

int xpost_record_mark(Xpost_Record *rec, Xpost_Record_Kind kind,
                      const real *colour, const real *ops, int nops)
{
    int fixed;
    real lo, hi;

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

    _extent(kind, ops, nops, &lo, &hi);
    return _put(rec, kind, colour, ops, nops, lo, hi);
}

/* Copy what a caller owns into memory the record owns, counting what it
   cost. Nothing asked for and nothing available both answer NULL, which
   a caller tells apart by what it asked for. */
static void *_take(const void *p, size_t n, size_t *cost)
{
    void *q;

    if (!p || !n)
        return NULL;
    q = malloc(n);
    if (!q)
        return NULL;
    memcpy(q, p, n);
    *cost += n;
    return q;
}

/* Copy the sample rows into one block the record owns. They arrive as a
   run pointer apiece because that is how the painter holds them -- a
   buffer it refills per row -- and they are laid end to end here so
   that the entry holds one thing rather than a list of them. */
static unsigned char *_take_rows(const unsigned char *const *rows, int nrows,
                                 size_t each, size_t *cost)
{
    unsigned char *block;
    int i;

    if (!rows || nrows < 1 || !each)
        return NULL;
    block = malloc((size_t)nrows * each);
    if (!block)
        return NULL;
    for (i = 0; i < nrows; i++)
    {
        if (!rows[i])
        {
            free(block);
            return NULL;
        }
        memcpy(block + (size_t)i * each, rows[i], each);
    }
    *cost += (size_t)nrows * each;
    return block;
}

int xpost_record_image(Xpost_Record *rec, const Xpost_Record_Image *src,
                       const unsigned char *const *rows, int nrows)
{
    Xpost_Record_Image img;
    real *colour;
    real idx;
    real lo, hi;
    size_t cost = 0;
    int ok;

    if (!rec || !src || rec->short_of_a_mark)
        return 0;
    /* what the row writer indexes with is bounded here, once, rather
       than on the way past every sample */
    if (src->width < 1 || src->height < 1
     || src->ncomp < 1 || src->ncomp > 4
     || src->nat < 1 || src->nat > 3
     || (src->mbits && src->mrowb < 1)
     || src->nranges < 0 || src->nranges > 8
     || (src->nranges && !src->mranges)
     || src->nspan < 0 || (src->nspan && !src->cspans))
        return 0;
    if (nrows != src->height * (src->planar ? src->ncomp : 1))
        return 0;

    img = *src;
    img.samples = _take_rows(rows, nrows,
                             (size_t)src->width
                             * (src->planar ? 1u : (size_t)src->ncomp),
                             &cost);
    img.lut = _take(src->lut, 256u * (size_t)src->nat, &cost);
    img.dluts = _take(src->dluts, 256u * (size_t)src->ncomp, &cost);
    img.tlut = _take(src->tlut, 256u, &cost);
    img.tlutrgb = _take(src->tlutrgb, 3u * 256u, &cost);
    img.mbits = _take(src->mbits,
                      (size_t)src->mrowb * (size_t)src->height, &cost);
    img.mranges = _take(src->mranges,
                        (size_t)src->nranges * sizeof *src->mranges, &cost);
    img.cspans = _take(src->cspans,
                       4u * (size_t)src->nspan * sizeof *src->cspans, &cost);

    ok = img.samples != NULL
      && (!src->lut || img.lut)
      && (!src->dluts || img.dluts)
      && (!src->tlut || img.tlut)
      && (!src->tlutrgb || img.tlutrgb)
      && (!src->mbits || img.mbits)
      && (!src->nranges || img.mranges)
      && (!src->nspan || img.cspans);
    if (!ok)
    {
        _image_free(&img);
        rec->short_of_a_mark = 1;
        return 0;
    }

    /* the colour a mark carries is one value per component of the
       device's space; an image carries its colours in its samples, so
       the place is filled with zeros and every mark's values stay laid
       out the same way */
    colour = calloc((size_t)rec->ncomp, sizeof *colour);
    if (!colour)
    {
        _image_free(&img);
        rec->short_of_a_mark = 1;
        return 0;
    }

    idx = (real)_nimg(rec);
    if (xpost_strbuf_append(&rec->img, &img, sizeof img))
    {
        free(colour);
        _image_free(&img);
        rec->short_of_a_mark = 1;
        return 0;
    }
    /* the run holds the entry now, so what it points at is the
       record's to give up and no longer this call's */
    rec->imgbytes += cost;

    _image_extent(&img, &lo, &hi);
    ok = _put(rec, XPOST_RECORD_IMAGE, colour, &idx, 1, lo, hi);
    free(colour);
    return ok;
}

size_t xpost_record_image_count(const Xpost_Record *rec)
{
    return rec ? _nimg(rec) : 0;
}

const Xpost_Record_Image *xpost_record_image_get(const Xpost_Record *rec,
                                                 size_t i)
{
    if (!rec || i >= _nimg(rec))
        return NULL;
    return &_imgs(rec)[i];
}

int xpost_record_image_rows(const Xpost_Record_Image *img,
                            real lo, real hi, int *y0, int *y1)
{
    double s, a, b, t;
    int first, last;

    if (!img || !y0 || !y1)
        return 0;
    s = (double)img->yscale;
    /* a transform putting every row in the same place writes nothing;
       answering the whole image there costs a pass and cannot lose one */
    if (s > -1e-9 && s < 1e-9)
    {
        *y0 = 0;
        *y1 = img->height;
        return img->height > 0;
    }
    /* where the range's two edges fall in the image's own rows. A row
       is a whole one either side of that, since a row magnified with
       its neighbours blended reaches half a row beyond its own band at
       each end and the last row reaches a whole one. */
    a = ((double)lo - (double)img->yoff) / s;
    b = ((double)hi + 1.0 - (double)img->yoff) / s;
    if (a > b) { t = a; a = b; b = t; }
    a -= 2.0;
    b += 2.0;
    *y0 = *y1 = 0;
    if (b < 0.0 || a > (double)img->height)
        return 0;
    /* brought inside the image before it is counted in rows, so that a
       range far off the page does not name a row number no int holds */
    if (a < 0.0) a = 0.0;
    if (b > (double)img->height) b = (double)img->height;
    first = (int)floor(a);
    last = (int)ceil(b);
    *y0 = first;
    *y1 = last;
    return first < last;
}

/* A digest of a mask's bytes, and of the extents that say how to read
   them. It answers whether two masks might be equal; the bytes answer
   whether they are. */
static unsigned long long _digest(const unsigned char *p, size_t n,
                                  int w, int h)
{
    unsigned long long d = 1469598103934665603ULL;
    size_t i;

    d = (d ^ (unsigned long long)(unsigned)w) * 1099511628211ULL;
    d = (d ^ (unsigned long long)(unsigned)h) * 1099511628211ULL;
    for (i = 0; i < n; i++)
        d = (d ^ p[i]) * 1099511628211ULL;
    return d;
}

int xpost_record_mask(Xpost_Record *rec, const unsigned char *cov,
                      int w, int h, size_t *at)
{
    _Cover m;
    _Cover *held;
    size_t n, i, count;

    if (!rec || !cov || !at || w < 1 || h < 1)
        return 0;
    /* a record already short of a mark describes a page it cannot
       reproduce, and adding to it would only make the gap harder to
       see */
    if (rec->short_of_a_mark)
        return 0;
    /* what the two extents multiply to has to be a count this can walk,
       which is asked here rather than on the way past every byte */
    if (h > INT_MAX / w)
        return 0;
    n = (size_t)w * (size_t)h;

    /* the one the record already holds, if it holds it. Backwards,
       because a page's text repeats the glyph it last used far more
       often than the one it used first. */
    m.digest = _digest(cov, n, w, h);
    held = _msks(rec);
    count = _nmsk(rec);
    for (i = count; i--; )
    {
        if (held[i].digest != m.digest
            || held[i].w != w || held[i].h != h)
            continue;
        if (memcmp(held[i].cov, cov, n) != 0)
            continue;
        *at = i;
        return 1;
    }

    m.w = w;
    m.h = h;
    m.cov = malloc(n);
    if (!m.cov)
    {
        rec->short_of_a_mark = 1;
        return 0;
    }
    memcpy(m.cov, cov, n);
    if (xpost_strbuf_append(&rec->msk, &m, sizeof m))
    {
        free(m.cov);
        rec->short_of_a_mark = 1;
        return 0;
    }
    /* the run holds the entry now, so the coverage is the record's to
       give up and no longer this call's */
    rec->mskbytes += n;
    *at = count;
    return 1;
}

int xpost_record_glyph(Xpost_Record *rec, const real *colour,
                       size_t at, real x, real y)
{
    const _Cover *m;
    real ops[3];

    if (!rec || !colour)
        return 0;
    if (rec->short_of_a_mark)
        return 0;
    /* a placement naming a mask the record does not hold would replay
       as nothing, which is a page missing a glyph: it is refused on the
       terms a mark that cannot be held is refused, so that the page is
       refused rather than put out short */
    if (at >= _nmsk(rec))
    {
        rec->short_of_a_mark = 1;
        return 0;
    }
    m = &_msks(rec)[at];

    ops[0] = (real)at;
    ops[1] = x;
    ops[2] = y;
    /* the rows the mask covers from where it was put, which is the one
       kind whose reach is read off the entry it names rather than off
       its own operands */
    return _put(rec, XPOST_RECORD_GLYPH, colour, ops, 3,
                y, y + (real)(m->h - 1));
}

size_t xpost_record_mask_count(const Xpost_Record *rec)
{
    return rec ? _nmsk(rec) : 0;
}

size_t xpost_record_mask_bytes(const Xpost_Record *rec)
{
    return rec ? rec->mskbytes : 0;
}

const unsigned char *xpost_record_mask_get(const Xpost_Record *rec, size_t i,
                                           int *w, int *h)
{
    const _Cover *m;

    /* a record short of a mark gives none of what it holds back, on the
       same terms as a replay of one */
    if (!rec || rec->short_of_a_mark || i >= _nmsk(rec))
        return NULL;
    m = &_msks(rec)[i];
    if (w) *w = m->w;
    if (h) *h = m->h;
    return m->cov;
}

/* Write one screen entry down, its cell copied into the record's own
   memory. Shared by a screen the painting announced and by the one a
   page boundary opens the page after with. */
static int _screen_put(Xpost_Record *rec, int w, int h,
                       const unsigned char *cell)
{
    _Screen s;
    real *colour;
    real idx;
    size_t n = (size_t)w * (size_t)h;
    int ok;

    s.w = w;
    s.h = h;
    s.cell = malloc(n);
    if (!s.cell)
    {
        rec->short_of_a_mark = 1;
        return 0;
    }
    memcpy(s.cell, cell, n);

    /* a screen paints nothing, so the place a mark's colour takes is
       filled with zeros and every entry's values stay laid out the one
       way */
    colour = calloc((size_t)rec->ncomp, sizeof *colour);
    if (!colour)
    {
        free(s.cell);
        rec->short_of_a_mark = 1;
        return 0;
    }

    idx = (real)_nscr(rec);
    if (xpost_strbuf_append(&rec->scr, &s, sizeof s))
    {
        free(colour);
        free(s.cell);
        rec->short_of_a_mark = 1;
        return 0;
    }
    /* the run holds the entry now, so the cell is the record's to give
       up and no longer this call's */
    rec->scrbytes += n;

    ok = _put(rec, XPOST_RECORD_SCREEN, colour, &idx, 1, 0.0, 0.0);
    free(colour);
    return ok;
}

int xpost_record_screen(Xpost_Record *rec, int w, int h,
                        const unsigned char *cell)
{
    unsigned char *keep;
    size_t n;

    if (!rec || !cell || w < 1 || h < 1 || h > INT_MAX / w)
        return 0;
    /* a record already short of a mark describes a page it cannot
       reproduce, and adding to it would only make the gap harder to
       see */
    if (rec->short_of_a_mark)
        return 0;
    n = (size_t)w * (size_t)h;

    keep = malloc(n);
    if (!keep)
    {
        rec->short_of_a_mark = 1;
        return 0;
    }
    memcpy(keep, cell, n);
    if (!_screen_put(rec, w, h, cell))
    {
        free(keep);
        return 0;
    }
    /* what the page after a boundary is opened under, kept only once
       the run has taken this one */
    free(rec->htcell);
    rec->htcell = keep;
    rec->htw = w;
    rec->hth = h;
    return 1;
}

size_t xpost_record_screen_count(const Xpost_Record *rec)
{
    return rec ? _nscr(rec) : 0;
}

const unsigned char *xpost_record_screen_get(const Xpost_Record *rec,
                                             size_t i, int *w, int *h)
{
    const _Screen *s;

    /* a record short of a mark gives none of what it holds back, on the
       same terms as a replay of one */
    if (!rec || rec->short_of_a_mark || i >= _nscr(rec))
        return NULL;
    s = &_scrs(rec)[i];
    if (w) *w = s->w;
    if (h) *h = s->h;
    return s->cell;
}

void xpost_record_clear(Xpost_Record *rec)
{
    size_t i, n;

    if (!rec)
        return;
    /* a record short of a mark answers every replay with the refusal,
       and the refusal is the one thing here that is not a mark of the
       page in hand */
    if (rec->short_of_a_mark)
        return;
    n = _nimg(rec);
    for (i = 0; i < n; i++)
        _image_free(&_imgs(rec)[i]);
    _masks_free(rec);
    _screens_free(rec);
    /* the runs keep what they took: a record is filled again by the page
       after, and what it costs is then the largest page rather than the
       sum of them */
    rec->mark.len = 0;
    rec->val.len = 0;
    rec->img.len = 0;
    rec->imgbytes = 0;
    /* The screen the page ending was painted under opens the page
       beginning, because a page boundary is not a screen change: the
       machinery that announces one rebuilds the cell only where the
       screen it is built from has changed, so nothing would announce
       this one again and the page after would replay under whatever
       screen its target happened to hold. */
    if (rec->htcell)
        _screen_put(rec, rec->htw, rec->hth, rec->htcell);
}

size_t xpost_record_count(const Xpost_Record *rec)
{
    return rec ? _nmark(rec) : 0;
}

size_t xpost_record_bytes(const Xpost_Record *rec)
{
    if (!rec)
        return 0;
    /* what the runs have taken rather than what they have filled: a
       record is compared against a raster it would save holding, and
       what a raster costs is what was allocated for it */
    return rec->mark.cap + rec->val.cap + rec->img.cap + rec->imgbytes
         + rec->msk.cap + rec->mskbytes
         + rec->scr.cap + rec->scrbytes;
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
    int any = 0;

    if (!rec)
        return 0;
    m = _marks(rec);
    n = _nmark(rec);
    for (i = 0; i < n; i++)
    {
        /* a screen paints nothing and so reaches no row: what is being
           asked is where the ink is, and a record holding screens and
           no mark reaches nothing */
        if (m[i].kind == XPOST_RECORD_SCREEN)
            continue;
        if (!any)
        {
            *lo = m[i].lo;
            *hi = m[i].hi;
            any = 1;
            continue;
        }
        if (m[i].lo < *lo) *lo = m[i].lo;
        if (m[i].hi > *hi) *hi = m[i].hi;
    }
    return any;
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
    /* A screen is met by every run of rows. It paints nothing, and what
       it says governs whatever is painted after it wherever on the page
       that lands -- so a replay of any run has to pass through the same
       screens in the same order as a replay of the whole page, or the
       rows it paints are not the rows the whole page would have had. */
    if (m->kind == XPOST_RECORD_SCREEN)
        return 1;

    /* A mark's reach is in the coordinates it was made with and a run of
       rows is in whole rows, so the reach is taken out to the rows it
       falls in before the two are compared. A shape reaching from
       halfway down one row to halfway down another inks both of them --
       a stroke of any width has ends at a half row, being a rectangle
       around a segment -- and a run ending at the first of those would
       judge the shape not to reach it and leave the page short of a
       mark. Every kind puts a coordinate on a row by dropping the
       fraction, so that is what taking it out to whole rows is.

       Erring outward here costs a visit to a mark that then paints
       nothing in the run; erring inward loses the mark from the page,
       which is wrong output rather than slow output. */
    return !(floor((double)m->hi) < (double)lo
          || floor((double)m->lo) > (double)hi);
}

int xpost_record_last(const Xpost_Record *rec, real lo, real hi, size_t *at)
{
    const _Mark *marks;
    size_t n;

    /* a record short of a mark gives none of them back, on the same
       terms as a replay of one */
    if (!rec || !at || rec->short_of_a_mark)
        return 0;
    marks = _marks(rec);
    n = _nmark(rec);
    /* backwards, stopping at the first one found: what is being asked
       is which mark had the last word over the run, and a run with
       anything in it is answered from near the end of the record rather
       than from a pass over the whole of it.

       A screen is stepped over. It is met by every run, so a caller
       asking whether a run comes to nothing but the colour the page was
       cleared to would be told no by the mere presence of one -- and
       every band of a screening device's page would then be painted,
       which is the cost this question exists to avoid. */
    while (n--)
        if (marks[n].kind != XPOST_RECORD_SCREEN
            && _meets(&marks[n], lo, hi))
        {
            *at = n;
            return 1;
        }
    return 0;
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
