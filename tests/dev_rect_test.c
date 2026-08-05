/* The driver contract's device-space geometry: which pixels a marking
   method paints. src/lib/xpost_dev_driver.h states it once -- a device
   coordinate names the pixel containing it (floor), a rectangle's far
   corner is inclusive, a negative extent reflects through the origin,
   and a line paints the pixels whose centres it covers along its major
   axis, whichever end it is drawn from. Pin that here,
   with the operand folding, so every device routed through the header
   inherits exactly these semantics. Uses the internal headers
   deliberately: the helpers are below anything a PostScript-level test
   can observe on a windowed device. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

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
#include "xpost_dev_driver.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* a 100x50 device throughout */
static void rect(const char *what,
                 double x, double y, double w, double h,
                 int expect_nonempty,
                 int ex0, int ey0, int ex1, int ey1)
{
    int x0, y0, x1, y1, r;

    xpost_dev_rect_normalize(x, y, w, h, &x0, &y0, &x1, &y1);
    r = xpost_dev_rect_clip(&x0, &y0, &x1, &y1, 100, 50);

    if (!expect_nonempty)
    {
        check(r == 0, what);
        return;
    }
    check(r == 1 &&
          x0 == ex0 && y0 == ey0 && x1 == ex1 && y1 == ey1,
          what);
    if (r &&
        (x0 != ex0 || y0 != ey0 || x1 != ex1 || y1 != ey1))
        printf("  got [%d..%d]x[%d..%d]\n", x0, x1, y0, y1);
}

/* Walk a segment and render the pixels it paints as "x,y x,y ..." so an
   expectation reads as the picture it is. */
static void line(const char *what,
                 double x1, double y1, double x2, double y2,
                 const char *expect)
{
    Xpost_Dev_Line l;
    char got[512];
    int px, py;
    size_t n = 0;

    got[0] = '\0';
    xpost_dev_line_init(&l, x1, y1, x2, y2);
    while (xpost_dev_line_next(&l, &px, &py) && n < sizeof(got) - 32)
        n += (size_t)snprintf(got + n, sizeof(got) - n,
                              n ? " %d,%d" : "%d,%d", px, py);
    if (strcmp(got, expect) != 0)
    {
        printf("FAIL: %s\n  want %s\n  got  %s\n", what, expect, got);
        failures++;
    }
}

int main(void)
{
    /* the inclusive span: w+1 columns, h+1 rows */
    rect("unit rect paints 2x2 inclusive", 5, 5, 1, 1, 1, 5, 5, 6, 6);
    rect("zero-extent rect paints one pixel", 5, 5, 0, 0, 1, 5, 5, 5, 5);

    /* negative extents reflect through the origin */
    rect("negative extent spans x-|w|..x", 80, 40, -30, -30, 1, 50, 10, 80, 40);

    /* clipping to [0, width-1] x [0, height-1] */
    rect("clip at far corner", 90, 40, 20, 20, 1, 90, 40, 99, 49);
    rect("clip at origin", -20, -20, 30, 30, 1, 0, 0, 10, 10);
    rect("full-page erase", 0, 0, 100, 50, 1, 0, 0, 99, 49);

    /* wholly outside paints nothing (a rectangle left of the device
       must not translate onto it) */
    rect("wholly off left", -20, 10, 10, 5, 0, 0, 0, 0, 0);
    rect("wholly off bottom", 10, 60, 5, 5, 0, 0, 0, 0, 0);
    rect("wholly off right", 500, 10, 50, 5, 0, 0, 0, 0, 0);

    /* a real coordinate names the pixel containing it: floor, so the
       fractional part never moves the near edge, and the far edge is
       the pixel the far corner lands in */
    rect("fractional origin floors", 5.5, 5.25, 3, 2, 1, 5, 5, 8, 7);
    rect("fractional extent reaches the far pixel",
         5.5, 5.25, 3.75, 2.5, 1, 5, 5, 9, 7);
    /* truncation would put both these on pixel 0 and leave -1 unpainted */
    rect("a coordinate below zero floors", -0.5, -0.5, 0, 0, 0, 0, 0, 0, 0);
    rect("a rectangle crossing the origin keeps its left column",
         -1.5, 0, 2, 0, 1, 0, 0, 0, 0);

    /* the span clipper on its own: the clip source a device supplies */
    {
        int lo = 3, hi = 12;
        check(xpost_dev_span_clip(&lo, &hi, 8) && lo == 3 && hi == 7,
              "a span clips to the extent it is given");
        lo = 20; hi = 30;
        check(xpost_dev_span_clip(&lo, &hi, 8) == 0,
              "a span wholly past the extent survives nothing");
    }

    /* DrawLine: the pixels whose centres the segment covers along its
       major axis */
    line("a horizontal run stops short of its far endpoint",
         2, 5, 6, 5, "2,5 3,5 4,5 5,5");
    line("a vertical run walks the minor axis",
         5, 2, 5, 6, "5,2 5,3 5,4 5,5");
    /* reversing the operands reverses the order and nothing else: an
       implementation that walks from one endpoint would paint 6 and
       drop 2, so the same wire drawn back would land elsewhere */
    line("a reversed run covers the same pixels backwards",
         6, 5, 2, 5, "5,5 4,5 3,5 2,5");
    line("a diagonal advances both",
         0, 0, 3, 3, "0,0 1,1 2,2");
    line("a shallow diagonal steps in x",
         0, 0, 4, 2, "0,0 1,0 2,1 3,1");
    line("a degenerate segment marks its own pixel",
         5, 5, 5, 5, "5,5");
    line("a segment too short to reach a centre marks its midpoint",
         5.1, 5.0, 5.2, 5.0, "5,5");
    /* the joint: 2..6 then 6..9 covers 2..8 once each, no gap and no
       double-painted pixel at 6 */
    line("the first half of a joint", 2, 5, 6, 5, "2,5 3,5 4,5 5,5");
    line("the second half of a joint", 6, 5, 9, 5, "6,5 7,5 8,5");
    /* an endpoint meant to sit on a pixel boundary arrives carrying
       accumulated float noise; quantising to the 1/256 device grid the
       fill pipeline works on puts it back where it was meant to be */
    line("a noisy endpoint paints what the exact one would",
         2.0, 5.0, 5.9999999, 5.0, "2,5 3,5 4,5 5,5");

    /* operand folding truncates toward zero; colours scale then truncate */
    check(xpost_dev_num_to_int(xpost_real_cons((real)3.7)) == 3,
          "real coordinate truncates");
    check(xpost_dev_num_to_int(xpost_int_cons(-4)) == -4,
          "integer coordinate passes through");
    check(xpost_dev_num_to_byte(xpost_real_cons((real)1.0)) == 255,
          "unit colour scales to full channel");
    check(xpost_dev_num_to_byte(xpost_int_cons(1)) == 255,
          "integer unit colour scales to full channel");
    check(xpost_dev_num_to_byte(xpost_real_cons((real)0.5)) == 127,
          "half colour truncates");
    check(xpost_dev_num_to_scaled(xpost_real_cons((real)1.0), 65535.0) == 65535,
          "unit colour scales to a 16-bit channel");

    /* a component outside [0,1] is clamped, not folded: a tint
       transform is the program's own procedure and returns whatever it
       computes, and an unclamped scale wraps the stored channel */
    check(xpost_dev_num_to_byte(xpost_real_cons((real)1.7)) == 255,
          "a colour above the range clamps to full scale");
    check(xpost_dev_num_to_byte(xpost_real_cons((real)-0.7)) == 0,
          "a colour below the range clamps to zero");
    check(xpost_dev_num_to_scaled(xpost_real_cons((real)1.7), 65535.0) == 65535,
          "a colour above the range clamps on a 16-bit channel");
    check(xpost_dev_num_to_component(xpost_real_cons((real)0.25)) == 0.25,
          "a colour inside the range passes through");

    if (failures)
    {
        printf("FAILURES: %d\n", failures);
        return 1;
    }
    printf("SUCCESS\n");
    return 0;
}
