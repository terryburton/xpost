/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 * (BSD 3-clause; see COPYING)
 */

/* What a record gives back, and to which rows.
 *
 * A record exists so that a page can be painted into a raster smaller
 * than the page, by being played once per run of rows. Two things have
 * to hold for that to be worth anything:
 *
 *   Everything comes back. Playing the whole extent gives every mark,
 *   in the order it was made, with the values it was made with. The
 *   order is not incidental: marks overpaint, so the order they are
 *   played in is the order they were painted in.
 *
 *   Only what reaches comes back. Playing a run of rows gives the marks
 *   that reach those rows and no others -- that is the whole of why a
 *   large page is affordable, and a record that played everything every
 *   time would be correct and useless.
 *
 * The second is the one a test has to be careful about, because a
 * record that ignored the range entirely would pass any check that only
 * asked whether the right marks were present.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "xpost_object.h"
#include "xpost_record.h"

#include "xpost_test.h"

/* what a replay saw */
typedef struct
{
    int n;
    Xpost_Record_Kind kind[64];
    real first[64];   /* the first colour value, to tell marks apart */
    real op0[64];     /* and the first operand */
} Seen;

static int _see(void *data, Xpost_Record_Kind kind, const real *colour,
                const real *ops, int nops)
{
    Seen *s = data;

    if (s->n >= 64)
        return 1;
    s->kind[s->n] = kind;
    s->first[s->n] = colour[0];
    s->op0[s->n] = nops > 0 ? ops[0] : 0.0;
    s->n++;
    return 0;
}

static Seen _play(Xpost_Record *rec, real lo, real hi)
{
    Seen s;

    memset(&s, 0, sizeof s);
    xpost_record_replay(rec, lo, hi, _see, &s);
    return s;
}

int main(void)
{
    Xpost_Record *rec;
    real grey[1];
    real ops[16];
    real lo, hi;
    Seen s;

    rec = xpost_record_new(1);
    if (!rec)
    {
        report_failure("a record for one colour component");
        return verdict();
    }

    if (xpost_record_count(rec) != 0)
        report_failure("a new record holds no mark");
    if (xpost_record_extent(rec, &lo, &hi))
        report_failure("a record holding no mark reaches no row");

    /* four marks at known rows: a pixel at 10, a rectangle over 20..30,
       a line over 40..50 (given the other way round, so that the ends
       are taken rather than assumed ordered), and a triangle over
       60..80. The colour tells them apart in what comes back. */
    grey[0] = 1.0; ops[0] = 5.0; ops[1] = 10.0;
    if (!xpost_record_mark(rec, XPOST_RECORD_PUTPIX, grey, ops, 2))
        report_failure("a pixel is written down");

    grey[0] = 2.0; ops[0] = 0.0; ops[1] = 20.0; ops[2] = 8.0; ops[3] = 10.0;
    if (!xpost_record_mark(rec, XPOST_RECORD_FILLRECT, grey, ops, 4))
        report_failure("a rectangle is written down");

    grey[0] = 3.0; ops[0] = 0.0; ops[1] = 50.0; ops[2] = 9.0; ops[3] = 40.0;
    if (!xpost_record_mark(rec, XPOST_RECORD_DRAWLINE, grey, ops, 4))
        report_failure("a line is written down");

    grey[0] = 4.0;
    ops[0] = 3.0;
    ops[1] = 0.0;  ops[2] = 60.0;
    ops[3] = 10.0; ops[4] = 80.0;
    ops[5] = 20.0; ops[6] = 70.0;
    if (!xpost_record_mark(rec, XPOST_RECORD_FILLPOLY, grey, ops, 7))
        report_failure("a polygon is written down");

    if (xpost_record_count(rec) != 4)
        report_failure("the record holds the four marks made");

    if (!xpost_record_extent(rec, &lo, &hi) || lo != 10.0 || hi != 80.0)
        report_failure("the record reaches from the first row marked to"
                       " the last");

    /* everything, in the order it was made */
    s = _play(rec, -1000.0, 1000.0);
    if (s.n != 4)
        report_failure("playing every row gives every mark: %d of 4", s.n);
    else if (s.first[0] != 1.0 || s.first[1] != 2.0 ||
             s.first[2] != 3.0 || s.first[3] != 4.0)
        report_failure("the marks come back in the order they were made");
    else if (s.kind[1] != XPOST_RECORD_FILLRECT ||
             s.kind[3] != XPOST_RECORD_FILLPOLY)
        report_failure("each mark comes back as the kind it was made");
    else if (s.op0[0] != 5.0)
        report_failure("a mark comes back with the operands it was made"
                       " with");

    /* one row, met by one mark */
    s = _play(rec, 10.0, 10.0);
    if (s.n != 1 || s.first[0] != 1.0)
        report_failure("a row met by one mark gives that mark alone:"
                       " %d mark(s)", s.n);

    /* a run met by none: between the pixel and the rectangle */
    s = _play(rec, 12.0, 18.0);
    if (s.n != 0)
        report_failure("a run of rows no mark reaches gives nothing:"
                       " %d mark(s)", s.n);

    /* a rectangle is met anywhere across its height, ends included */
    s = _play(rec, 25.0, 25.0);
    if (s.n != 1 || s.first[0] != 2.0)
        report_failure("a rectangle is met by a row inside it");
    s = _play(rec, 30.0, 30.0);
    if (s.n != 1 || s.first[0] != 2.0)
        report_failure("a rectangle is met by the last row it covers");
    s = _play(rec, 31.0, 39.0);
    if (s.n != 0)
        report_failure("a rectangle is not met past the row it ends on");

    /* a line given from its far end still reaches the rows between */
    s = _play(rec, 45.0, 45.0);
    if (s.n != 1 || s.first[0] != 3.0)
        report_failure("a line reaches the rows between its ends however"
                       " the ends were given");

    /* a polygon reaches the rows its vertices span, and is met by any
       of them -- its reach is a walk of the vertices rather than a pair
       of values, so it is asked at a row only an inner vertex reaches */
    s = _play(rec, 79.0, 79.0);
    if (s.n != 1 || s.first[0] != 4.0)
        report_failure("a polygon is met by a row inside the vertices it"
                       " spans");
    s = _play(rec, 81.0, 90.0);
    if (s.n != 0)
        report_failure("a polygon is not met past its furthest vertex");

    /* a run meeting two marks gives both, still in order */
    s = _play(rec, 10.0, 25.0);
    if (s.n != 2 || s.first[0] != 1.0 || s.first[1] != 2.0)
        report_failure("a run meeting two marks gives both in order:"
                       " %d mark(s)", s.n);

    /* a record refuses a mark whose operands do not fit its kind, so
       that a walk of what was written down stays inside it */
    grey[0] = 9.0; ops[0] = 1.0; ops[1] = 2.0;
    if (xpost_record_mark(rec, XPOST_RECORD_FILLRECT, grey, ops, 2))
        report_failure("a rectangle needs the four operands a rectangle"
                       " has");
    ops[0] = 5.0;   /* says five vertices and gives one */
    ops[1] = 0.0; ops[2] = 0.0;
    if (xpost_record_mark(rec, XPOST_RECORD_FILLPOLY, grey, ops, 3))
        report_failure("a polygon needs as many vertices as it says it"
                       " has");
    if (xpost_record_count(rec) != 4)
        report_failure("a refused mark is not written down");

    xpost_record_free(rec);
    xpost_record_free(NULL);   /* nothing is not something to give up */

    return verdict();
}
