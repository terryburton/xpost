/* The one definition of the rectangle FillRect paints: the driver
   contract (src/lib/xpost_dev_driver.h) says a negative extent reflects
   through the origin and the painted span is inclusive of (x+w, y+h),
   clipped to the device. Pin xpost_dev_rect_normalize() to that
   definition, and the operand folding helpers to truncation, so every
   device routed through them inherits the same semantics. Uses the
   internal headers deliberately: the helpers are below anything a
   PostScript-level test can observe on a windowed device. */

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
#include "xpost_dict.h"
#include "xpost_string.h"
#include "xpost_name.h"
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

static void rect(const char *what,
                 int x, int y, int w, int h,
                 int expect_nonempty,
                 int ex0, int ey0, int ex1, int ey1)
{
    /* a 100x50 device throughout */
    int x0, y0, x1, y1;
    int r = xpost_dev_rect_normalize(x, y, w, h, 100, 50,
                                     &x0, &y0, &x1, &y1);

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
