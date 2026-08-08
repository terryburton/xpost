/*
 * Embedding contract: a context that did not validate is not a yield.
 *
 * A run under the returning semantics ends in one of two ways that the
 * caller has to tell apart. It yields -- the program reached a page
 * boundary, the caller may look at the page and resume -- or it is over.
 * The interpreter's central loop answers both, and it answers a third
 * thing as well: that the context it was handed did not validate, which
 * is the loop refusing to run at all.
 *
 * Those three answers travel as one integer, so no two of them may be
 * the same number. Reported as a yield, a refusal tells the caller to
 * resume a context that cannot run; the next call validates no better
 * and answers the same way, and a caller that drives a run to its end
 * never reaches one. What that costs is not a wrong answer but no
 * answer at all -- an embedder looping on a context that will never
 * finish, with nothing on its output to say why.
 *
 * The refusal is induced rather than waited for. Validation is what the
 * loop does with the context's two memory files before it runs anything,
 * so emptying the global one of its base address is a context that
 * cannot validate; nothing is read through it on the way to the check,
 * and putting the address back is the whole of undoing it. It is done
 * between the halves of a job that yielded, because that is the one
 * place a caller re-enters the loop with no other work in front of it:
 * resuming touches the context's error record and nothing else before
 * the loop is asked to run.
 *
 * The yield is taken first and checked, so that the refusal is being
 * compared against a yield this build really produces rather than
 * against a number written down here.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_context.h"

#include "xpost_test.h"

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Run_Status yielded;
    Xpost_Run_Status refused;
    unsigned char *held;

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

    /* the answer a real yield gives on this build */
    yielded = xpost_run(ctx, XPOST_INPUT_STRING, "showpage /a 1 def", 0);
    check(yielded == XPOST_RUN_YIELDED,
          "a page boundary under the returning semantics yields");

    /* and the answer for a context the loop will not run */
    held = ctx->gl->base;
    ctx->gl->base = NULL;
    refused = xpost_run(ctx, XPOST_INPUT_RESUME, "", 0);
    ctx->gl->base = held;

    check(refused != XPOST_RUN_YIELDED,
          "a context that did not validate is not reported as a yield");
    check(refused == XPOST_RUN_FAILED,
          "a context that did not validate is reported as a failed run");

    /* the sabotage was the two assignments above and nothing else: with
       the address back, the context finishes the job it was in the
       middle of */
    check(xpost_run(ctx, XPOST_INPUT_RESUME, "", 0) == XPOST_RUN_COMPLETE,
          "the context resumes once its memory is whole again");

    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
