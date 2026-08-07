/* What an operator does with an operand the stack would not take.
 *
 * A stack grows in segments. A push into a full segment links a fresh one
 * first, and that link is an allocation, which a memory file with nothing
 * left declines. The push then puts the object nowhere and answers that it
 * did not, which is the only account of it there is: the operand stack is
 * one shorter than the operator that pushed believes, and nothing about
 * the stack says why.
 *
 * An operator that answers noerror over such a push reports work it did
 * not do. What follows reads whatever lies beneath the absent operand --
 * an operand belonging to the caller, or none at all -- so the failure
 * surfaces somewhere other than where it happened, against an operator
 * that did nothing wrong. PLRM 8.2 gives VMerror for an error in the
 * virtual memory machinery, and a stack segment with nowhere to go is one.
 *
 * currentglobal is the operator under test because it is the whole of the
 * shape and none of anything else: it takes no operands, pushes one
 * boolean, and has nothing else that can fail. What is being tested is the
 * answer to a refused push, not an operator's own work.
 *
 * The stack is brought to a segment boundary it has not been past before,
 * so that the next push is the one that needs a segment that does not yet
 * exist. Being at a boundary is not enough on its own: a stack that has
 * been deeper still holds the segment it used then, and pushing into that
 * allocates nothing.
 *
 * Both the refusal and a sound push are asked for. An operator that always
 * answered VMerror would satisfy the first alone; the second is what says
 * it still pushes its result, and answers noerror, when the stack takes it.
 *
 * The refusal is induced by putting the memory file at the far end of the
 * address range its offsets are unsigned ints of, which is where its
 * growth is declined. Nothing is written on that path, so the two fields
 * are the whole of the change and putting them back is the whole of
 * undoing it.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_operator.h"
#include "xpost_error.h"

#include "xpost_test.h"

static unsigned int held_used;
static unsigned int held_max;

/* Decline every allocation in mem until released. */
static void refuse_allocation(Xpost_Memory_File *mem)
{
    held_used = mem->used;
    held_max = mem->max;
    mem->used = 0xfffffff8u;
    mem->max = 0xfffffff8u;
}

static void allow_allocation(Xpost_Memory_File *mem)
{
    mem->used = held_used;
    mem->max = held_max;
}

/* Fill the operand stack to a segment boundary it has not been past
   before, so the next push is the one that needs a new segment. Answers
   the depth reached, or -1. */
static int fill_to_fresh_boundary(Xpost_Context *ctx)
{
    int pushed = 0;
    int ct;

    /* past any depth the startup left behind, so the boundary settled on
       below is one no segment has been linked for yet */
    while (pushed < XPOST_STACK_SEGMENT_SIZE)
    {
        if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(pushed)))
            return -1;
        pushed++;
    }
    while ((ct = xpost_stack_count(ctx->lo, ctx->os)) % XPOST_STACK_SEGMENT_SIZE)
    {
        if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(pushed)))
            return -1;
        pushed++;
    }
    return ct;
}

static void push_result(int refused)
{
    Xpost_Context *ctx;
    Xpost_Object op;
    int before;
    int after;
    int ret;

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return;
    }

    op = xpost_operator_cons(ctx, "currentglobal", NULL, 0);
    if (xpost_object_get_type(op) != operatortype)
    {
        report_failure("currentglobal is not an operator");
        xpost_destroy(ctx);
        return;
    }

    before = fill_to_fresh_boundary(ctx);
    if (before < 0)
    {
        report_failure("could not bring the operand stack to a boundary");
        xpost_destroy(ctx);
        return;
    }

    if (refused)
        refuse_allocation(ctx->lo);
    ret = xpost_operator_exec(ctx, op.mark_.padw);
    if (refused)
        allow_allocation(ctx->lo);

    after = xpost_stack_count(ctx->lo, ctx->os);

    if (refused)
    {
        if (after == before && ret == 0)
            report_failure("an operator whose result the stack would not "
                           "take answered noerror");
        else if (after == before && ret != VMerror)
            report_failure("an operator whose result the stack would not "
                           "take answered %s", errorname[ret]);
    }
    else
    {
        if (ret != 0)
            report_failure("a sound push was answered %s", errorname[ret]);
        if (after != before + 1)
            report_failure("a sound push left the operand stack at %d, "
                           "not %d", after, before + 1);
    }

    xpost_destroy(ctx);
}

int main(void)
{
    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    push_result(0);
    push_result(1);

    xpost_quit();

    return verdict();
}
