/* What a device can hold, and where in it a pixel sits.
 *
 * A device that keeps its page as one block of memory reaches a pixel by
 * its position within that block, and the block has to be one whose far
 * end this platform has an address for. Both halves of that are
 * arithmetic -- xpost_device_raster_bytes() in src/lib/xpost_dev_generic.c
 * settles what may be held, xpost_dev_raster_offset() in
 * src/lib/xpost_dev_driver.h settles where in it a pixel is -- and they
 * have to agree: every position the second can form for an extent the
 * first accepted must land inside the block the first priced.
 *
 * They are held here rather than through a device because holding them
 * through a device would mean allocating one. The interesting extents
 * are the ones at the edge of what a size expresses, and a page there
 * runs to gigabytes: a run that allocated one would answer for a single
 * point, on the one machine that had the memory that day, and would say
 * nothing at all on a machine with less. The arithmetic answers for the
 * whole range and answers the same on every machine, and it answers on
 * the platforms where the edge is low enough to reach -- a build whose
 * sizes are 32 bits refuses pages a 64-bit build holds, and the
 * boundaries below are computed from the platform's own size rather than
 * written down, so each build is held to its own.
 *
 * Two quantities are in play and they are not the same question. What
 * the page is comes from the page size the program asked for. What the
 * buffer is comes from the block that is resident and being indexed.
 * Every device here holds a whole page in one block and gives the two
 * the same numbers; what is checked below is the buffer's, because that
 * is what the memory has.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
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
#include "xpost_name.h"
#include "xpost_operator.h"
#include "xpost_dev_generic.h"
#include "xpost_dev_driver.h"

#include "xpost_test.h"

/* An extent the arithmetic must accept, together with what it must
   answer: the raster's own bytes plus whatever the caller keeps in front
   of it. The expected size is passed in rather than recomputed by the
   same expression the subject uses, so the check is a second opinion and
   not an echo. */
static void holds(const char *what,
                  int w, int h, size_t pixel, size_t reserve,
                  size_t expect)
{
    size_t bytes = 0;

    if (!xpost_device_raster_bytes(w, h, pixel, reserve, &bytes))
    {
        report_failure("%s: refused, and it is a buffer this platform holds",
                       what);
        return;
    }
    if (bytes != expect)
        report_failure("%s: comes to %lu bytes, not %lu",
                       what, (unsigned long)bytes, (unsigned long)expect);
}

/* An extent the arithmetic must refuse. The out-parameter is primed with
   a value the subject cannot produce, so a refusal that wrote to it
   anyway is caught: a caller reads the count only when the answer was
   yes, and a refusal that scribbles is a refusal one edit away from
   being read. */
static void refuses(const char *what, int w, int h, size_t pixel,
                    size_t reserve)
{
    size_t bytes = (size_t)-7;

    if (xpost_device_raster_bytes(w, h, pixel, reserve, &bytes))
    {
        report_failure("%s: accepted, and no block of that size can be"
                       " addressed here", what);
        return;
    }
    if (bytes != (size_t)-7)
        report_failure("%s: refused, and wrote a count anyway", what);
}

/* Every position the device can form for a buffer of this extent lies
   inside it, and the last of them is one short of the pixel count. The
   corners are what the walk reaches: a device's marking methods clip to
   the extent and then index, so the far corner is a position it forms
   whenever a program fills the page. */
static void corners(const char *what, int w, int h, size_t pixels)
{
    if (xpost_dev_raster_offset(0, 0, w) != 0)
        report_failure("%s: the first pixel is not at the start", what);
    if (xpost_dev_raster_offset(w - 1, 0, w) != (size_t)(w - 1))
        report_failure("%s: the end of the first row is not w-1 in", what);
    if (xpost_dev_raster_offset(0, h - 1, w) != pixels - (size_t)w)
        report_failure("%s: the start of the last row is not a row short"
                       " of the end", what);
    if (xpost_dev_raster_offset(w - 1, h - 1, w) != pixels - 1)
        report_failure("%s: the far corner is not the last pixel", what);
}

int main(void)
{
    size_t pixel_max, slack;

    /* ---- the shape of an extent ---- */

    /* A buffer of no extent holds no raster, so there is nothing but the
       caller's own reserve to allocate. It is reported rather than
       refused: what a device makes of an empty page it makes on its own
       terms, and a refusal here would take that decision away from it. */
    holds("a buffer of no extent", 0, 0, 4, 32, 32);
    holds("a buffer with no columns", 0, 500, 4, 32, 32);
    holds("a buffer with no rows", 500, 0, 4, 32, 32);
    holds("a buffer of no extent and no reserve", 0, 0, 4, 0, 0);

    /* A negative extent is not an extent, and a position within one is
       not a number a raster is indexed by. */
    refuses("a buffer of negative width", -1, 500, 4, 0);
    refuses("a buffer of negative height", 500, -1, 4, 0);
    refuses("a buffer negative both ways", -1, -1, 4, 0);

    /* ---- the plain answer ---- */

    holds("an ordinary page", 100, 50, 4, 0, 20000);
    holds("an ordinary page with a header in front of it",
          100, 50, 4, 24, 20024);
    holds("a page one pixel across", 1, 1, 3, 0, 3);

    /* ---- what settles the ceiling ---- */

    /* The ceiling is what this platform expresses the size of a block of
       memory in, not what it counts a pixel coordinate in. Those differ,
       and an int is the smaller: 46341 squared is 2,147,488,281 pixels,
       which is past what an int counts and well inside what a size
       expresses on every platform this builds for. A buffer of that many
       one-byte pixels is one a device can be given, so the arithmetic
       has to price it rather than refuse it. */
    holds("a buffer of more pixels than an int counts",
          46341, 46341, 1, 0, (size_t)2147488281UL);

    /* ...and the position of its far pixel is a number the device can
       form. This is the half a widened price would be worthless
       without: an accepted buffer whose far end the index cannot reach
       is not a refusal, it is a write past the end. */
    corners("a buffer of more pixels than an int counts",
            46341, 46341, (size_t)2147488281UL);

    /* ---- the ceiling itself, computed from the platform's own size ---- */

    /* The largest buffer of a million pixels is the one whose pixels are
       as large as a size will divide into. Taken from SIZE_MAX at run
       time rather than written down, so a build whose sizes are 32 bits
       is held to its own boundary and not to another build's. */
    pixel_max = SIZE_MAX / 1000000u;
    holds("the largest raster a size expresses",
          1000, 1000, pixel_max, 0, 1000000u * pixel_max);
    refuses("a raster one pixel's worth past what a size expresses",
            1000, 1000, pixel_max + 1, 0);

    /* The reserve is part of the same question. A raster that fits on
       its own and not with its header in front of it is one whose
       allocation size the caller would form by wrapping, and a wrapped
       size is a small allocation followed by writes across the whole
       page. */
    slack = SIZE_MAX - 1000000u * pixel_max;
    holds("a raster filling the size with its reserve",
          1000, 1000, pixel_max, slack, SIZE_MAX);
    refuses("a raster whose reserve is one byte past the size",
            1000, 1000, pixel_max, slack + 1);

    /* The pixel count is checked before the byte count, and on a build
       whose sizes are narrow enough the count is what binds. The
       boundary is again the platform's: the widest buffer of 46341 rows
       whose pixels a size can count, and the one column past it. Where a
       size counts further than a coordinate can reach, the widest such
       buffer is the widest an int names and there is no column past it
       to ask about. */
    {
        size_t rows = 46341;
        size_t wmax = SIZE_MAX / rows;

        if (wmax >= (size_t)INT_MAX)
        {
            holds("the widest buffer a coordinate names",
                  INT_MAX, (int)rows, 1, 0, (size_t)INT_MAX * rows);
        }
        else
        {
            holds("the widest buffer a size counts the pixels of",
                  (int)wmax, (int)rows, 1, 0, wmax * rows);
            refuses("one column past what a size counts the pixels of",
                    (int)wmax + 1, (int)rows, 1, 0);
        }
    }

    /* ---- where a pixel sits ---- */

    corners("an ordinary page", 100, 50, 5000);
    corners("a single-column page", 1, 4000, 4000);
    corners("a single-row page", 4000, 1, 4000);

    /* A position is a count of pixels from the buffer's first, so it
       steps by the buffer's row width and not by the page's. The two
       carry the same number in every device here; they are separate
       quantities, and this is the one the memory is laid out in. */
    if (xpost_dev_raster_offset(3, 7, 100) != 703)
        report_failure("a pixel's position does not step by the row width");
    if (xpost_dev_raster_offset(3, 7, 1000) != 7003)
        report_failure("a pixel's position ignores the row width it was"
                       " given");

    /* Where a size reaches past what four bytes hold, a position does
       too: a page of more pixels than a 32-bit count holds is one a
       64-bit build can be given, and the row arithmetic has to carry it.
       Nothing is allocated to ask -- the question is what number comes
       out, not what memory is behind it. */
#if SIZE_MAX > 0xffffffffu
    {
        size_t w = 100000, h = 100000;

        if (xpost_dev_raster_offset(99999, 99999, 100000)
            != w * h - 1)
            report_failure("the far corner of a buffer of more pixels than"
                           " four bytes count is not the last pixel");
        if (xpost_dev_raster_offset(0, 99999, 100000) != w * (h - 1))
            report_failure("the last row of a buffer of more pixels than"
                           " four bytes count does not start a row short"
                           " of the end");
    }
#endif

    return verdict();
}
