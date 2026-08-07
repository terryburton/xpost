/* What a run is told when a collection gives up before its sweep.
 *
 * Marking walks the roots and refuses anything it cannot read as an
 * object. An entity number it will not mark is the shape the refusal
 * takes -- one the table never handed out, or one below the band the
 * collector owns -- whether it arrives on a root the interpreter placed
 * or in bytes a collection read as objects because an allocation claimed
 * more of its block than it filled. A collection that refuses returns
 * before its sweep, so it reclaims nothing, and the next one refuses in
 * the same place -- reclamation is over for the rest of the run.
 *
 * Nothing in the run says so unless the collection's answer is read.
 * The allocations go on being served out of fresh entity numbers until
 * the numbers run out, and what the program is finally told is that
 * whichever operator happened to be allocating at that moment exceeded
 * an implementation limit -- an operator that had nothing to do with
 * the fault, at a point arbitrarily far from it.
 *
 * So both halves are asserted here. The collector must return its
 * refusal, and the safe point that asked for the collection must turn
 * that refusal into an error the program sees: PLRM 8.2 gives VMerror
 * for an error in the virtual memory machinery, "an internal error in
 * the interpreter" among its causes, and that is what a mark that
 * cannot read the roots is.
 *
 * Each half is asked twice, once with the fault present and once
 * without it. A check that fires is worth nothing until the same check
 * has been shown to stay quiet over a sound run, which is the failure
 * mode this test is about.
 *
 * The fault is induced at a root rather than staged through a save and
 * a restore. The refusal is the same one either way -- the marker is
 * handed an entity number it will not mark -- and inducing it directly
 * keeps the test about what the interpreter does with a refusal rather
 * than about any one way of provoking one.
 *
 * The collection is asked for at the moment the program is suspended
 * between showpage and its continuation, so it happens where every
 * collection happens: at a safe point inside a running program, with
 * the error handling the run set up around itself still in place.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_name.h"
#include "xpost_array.h"
#include "xpost_dict.h"
#include "xpost_garbage.h"

#include "xpost_test.h"

/* Put an array object naming an entity outside the collector's domain
   where a collection reaches it. Everything about the object but the
   number is what the marker expects, so it reaches the arm that checks
   the number and is refused there.

   The number is one below the band, rather than one above the table's
   last slot, because the band's floor does not move: an entity number
   the table has not reached yet stops being a refusal as soon as
   something allocates, and what is wanted here is a root that no amount
   of allocation makes markable.

   It goes into a dictionary a collection walks rather than onto a stack
   the error report prints. The refusal is the same from either place --
   the marker is handed the object and will not mark it -- but an object
   on the operand stack is also read by the handler reporting the error
   it causes, which is a second fault on top of the one under test. */
static int plant_unmarkable(Xpost_Context *ctx)
{
    Xpost_Object systemdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);
    Xpost_Object userdict = xpost_dict_get(ctx, systemdict,
                                           xpost_name_cons(ctx, "userdict"));
    Xpost_Object o = xpost_array_cons(ctx, 1);

    if (xpost_object_get_type(userdict) != dicttype
        || xpost_object_get_type(o) != arraytype
        || ctx->lo->start < 2)
        return 0;
    o = xpost_object_set_ent(o, ctx->lo->start - 1);
    return xpost_dict_put(ctx, userdict,
                          xpost_name_cons(ctx, "unmarkable"), o) == 0;
}

/* The collector's own answer, with the fault and without it. */
static void collector_reports_refusal(void)
{
    Xpost_Context *ctx;

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return;
    }

    xpost_stack_clear(ctx->lo, ctx->hold);
    if (xpost_garbage_collect(ctx->lo, 1, 1) < 0)
        report_failure("a collection over sound roots refused");

    if (!plant_unmarkable(ctx))
    {
        report_failure("could not place the object among the roots");
        xpost_destroy(ctx);
        return;
    }
    xpost_stack_clear(ctx->lo, ctx->hold);
    if (xpost_garbage_collect(ctx->lo, 1, 1) >= 0)
        report_failure("a collection that could not mark its roots "
                       "answered as though it had swept");

    xpost_destroy(ctx);
}

/* What the program is told, with the fault and without it. */
static void safe_point_raises_vmerror(void)
{
    Xpost_Context *ctx;
    Xpost_Run_Status st;

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_RETURN, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return;
    }

    /* sound roots: the collection the safe point performs must leave
       the program none the wiser */
    st = xpost_run(ctx, XPOST_INPUT_STRING, "showpage", 0);
    if (st != XPOST_RUN_YIELDED)
    {
        report_failure("the program did not suspend at showpage (%d)", (int)st);
        xpost_destroy(ctx);
        return;
    }
    ctx->lo->garbage_collect_pending = 1;
    st = xpost_run(ctx, XPOST_INPUT_RESUME, "", 0);
    if (st != XPOST_RUN_COMPLETE)
        report_failure("a collection over sound roots ended the program "
                       "(%d, %s)", (int)st, xpost_error_name_get(ctx));

    /* the same collection, over roots one of which cannot be marked */
    st = xpost_run(ctx, XPOST_INPUT_STRING, "showpage", 0);
    if (st != XPOST_RUN_YIELDED)
    {
        report_failure("the program did not suspend at showpage (%d)", (int)st);
        xpost_destroy(ctx);
        return;
    }
    if (!plant_unmarkable(ctx))
    {
        report_failure("could not place the object among the roots");
        xpost_destroy(ctx);
        return;
    }
    ctx->lo->garbage_collect_pending = 1;
    st = xpost_run(ctx, XPOST_INPUT_RESUME, "", 0);
    if (st != XPOST_RUN_ERRORED)
        report_failure("a collection that reclaimed nothing left the program "
                       "running as though it had (%d)", (int)st);
    else if (strcmp(xpost_error_name_get(ctx), "VMerror") != 0)
        report_failure("the failed collection was reported as %s",
                       xpost_error_name_get(ctx));

    xpost_destroy(ctx);
}

int main(void)
{
    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    collector_reports_refusal();
    safe_point_raises_vmerror();

    xpost_quit();

    return verdict();
}
