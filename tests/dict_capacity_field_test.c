/* The capacity a dictionary reports, held to the field that carries it.
 *
 * A dictionary's header records two sizes in fields one word wide: the
 * table it was given, and the capacity maxlength answers with. The table
 * is over-allocated from the capacity asked for, so the two are clamped
 * separately, and only one of the two clamps is reachable from a
 * program: `dict` refuses a size the field cannot carry before any
 * dictionary is made, and growth doubles the table rather than the
 * capacity. What is left is a call from inside the library asking for
 * more than the field carries, which is what the capacity clamp is for.
 *
 * A capacity stored wider than its field reads back as a small number:
 * one over the field's limit reads back as zero, nine over as eight. The
 * dictionary then reports a capacity below the one it was made with, and
 * below the number of entries it takes before it grows.
 *
 * The report has to be read before anything is put in, because putting
 * entries in raises a capacity the dictionary has outgrown to the size
 * of its table -- which is clamped, and would cover a capacity that was
 * truncated. So the empty dictionary is asked first, and the same
 * dictionary again afterwards, which may only have risen.
 *
 * tests/op_dict_capacity_test.ps covers what a program can ask for. This
 * covers what it cannot. The sizes above the field's limit are not
 * expressible in every build -- where the field is as wide as the
 * argument nothing can be asked that exceeds it -- so the cases are
 * counted, and the count is held to the number put. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdio.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_dict.h"

#include "xpost_test.h"

/* the sizes a program can ask for, where capacity is the size itself */
static const unsigned int ordinary[] = { 0u, 1u, 5u, 8u, 10u, 100u, 1000u };
#define NORDINARY (sizeof ordinary / sizeof ordinary[0])

/* whether this build can name a capacity its header field will not hold */
static int field_is_narrower_than_the_ask(void)
{
    return (dword)UINT_MAX > XPOST_OBJECT_COMP_MAX_SZ;
}

int main(void)
{
    Xpost_Context *ctx;
    unsigned int i;
    unsigned int held = 0;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }
    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_RETURN, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return verdict();
    }

    for (i = 0; i < NORDINARY; i++)
    {
        Xpost_Object d = xpost_dict_cons(ctx, ordinary[i]);

        if (xpost_object_get_type(d) != dicttype)
        {
            report_failure("a dictionary of %u did not construct", ordinary[i]);
            continue;
        }
        if (xpost_dict_capacity_memory(ctx->lo, d) != ordinary[i])
            report_failure("a dictionary asked for %u reports capacity %u",
                           ordinary[i],
                           xpost_dict_capacity_memory(ctx->lo, d));
        held++;
    }
    check(held == NORDINARY,
          "every ordinary capacity was asked of a dictionary that was made");

    /* A capacity above what the header field carries. Where the argument
       is no wider than the field there is no such number to ask for, and
       the clamp stands over a case this build cannot reach. */
    if (field_is_narrower_than_the_ask())
    {
        /* the limit itself, and the three shapes a truncating store
           answers with: zero, a small number, and one below the limit */
        const unsigned int lim = (unsigned int)XPOST_OBJECT_COMP_MAX_SZ;
        unsigned int asked[4];
        unsigned int n;
        unsigned int put = 0;

        asked[0] = lim;
        asked[1] = lim + 1u;
        asked[2] = lim + 9u;
        asked[3] = lim * 2u;

        for (n = 0; n < 4; n++)
        {
            Xpost_Object d = xpost_dict_cons(ctx, asked[n]);
            unsigned int cap;
            unsigned int after;

            if (xpost_object_get_type(d) != dicttype)
            {
                report_failure("a dictionary asked for %u did not construct",
                               asked[n]);
                continue;
            }
            cap = xpost_dict_capacity_memory(ctx->lo, d);
            if (cap != lim)
                report_failure("a dictionary asked for %u, above the %u its "
                               "field carries, reports capacity %u",
                               asked[n], lim, cap);

            /* and the report is not a number the dictionary has already
               outgrown: one entry in, the capacity may only have risen */
            if (xpost_dict_put(ctx, d, xpost_int_cons(1), xpost_int_cons(2)))
            {
                report_failure("a dictionary asked for %u refused an entry",
                               asked[n]);
                continue;
            }
            after = xpost_dict_capacity_memory(ctx->lo, d);
            if (after < cap)
                report_failure("a dictionary asked for %u reported capacity "
                               "%u and then %u", asked[n], cap, after);
            if (xpost_dict_length_memory(ctx->lo, d) > after)
                report_failure("a dictionary asked for %u holds %u entries "
                               "and reports capacity %u", asked[n],
                               xpost_dict_length_memory(ctx->lo, d), after);
            put++;
        }
        check(put == 4,
              "every capacity above the field was asked of a dictionary that "
              "was made");
    }
    else
    {
        /* not a pass by omission: no argument this build accepts names a
           size its capacity field will not hold */
        check(XPOST_OBJECT_COMP_MAX_SZ >= (dword)UINT_MAX,
              "this build's capacity field carries every size it can be "
              "asked for");
    }

    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
