/* The job boundary is leak-free: serving job after job on one context
 * returns both memory banks to the baseline, rather than accumulating.
 *
 * This is a security property, not only a performance one. The boundary
 * reverts the whole context to a fixed baseline captured once, when the
 * first run has loaded the language and graphics. It reverts by putting
 * the baseline image of each bank back and moving the bank's cursor to
 * where it stood, which discards everything the job allocated in one
 * stroke -- no save level is taken per job and no garbage is left pinned.
 * A boundary that accumulated a level or leaked a job's allocations would
 * be one an operator serving many requests would have to turn off, and a
 * boundary that is off is no isolation at all. So the leak-freedom is what
 * lets isolation be left on, which is what makes it a boundary.
 *
 * A job here touches both banks: it allocates in global VM (an array put
 * in globaldict), writes a pre-existing global slot, and allocates and
 * mutates in local VM. After the boundary, the value store and the entity
 * table of each bank must read exactly as they did at the baseline -- the
 * same used cursor and the same next-entity cursor -- for every one of a
 * long series of jobs, and the save stacks must stand where they started.
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

#include "xpost_test.h"

#define JOBS 500

static size_t out_len;
static size_t out_sink(void *user, const char *buf, size_t len)
{
    (void)user; (void)buf;
    out_len += len;
    return len;
}

static Xpost_Run_Status run_job(Xpost_Context *ctx, const char *prog)
{
    Xpost_Run_Status st;
    int resumes = 0;

    out_len = 0;
    st = xpost_run(ctx, XPOST_INPUT_STRING, prog, 0);
    while (st == XPOST_RUN_YIELDED && resumes++ < 16)
        st = xpost_run(ctx, XPOST_INPUT_RESUME, "", 0);
    return st;
}

static int gsave_depth(Xpost_Context *ctx)
{
    return xpost_stack_count(ctx->gl, xpost_memory_save_stack_ent(ctx->gl));
}
static int lsave_depth(Xpost_Context *ctx)
{
    return xpost_stack_count(ctx->lo, xpost_memory_save_stack_ent(ctx->lo));
}

static void balance(Xpost_Showpage_Semantics semantics, const char *what)
{
    Xpost_Context *ctx;
    unsigned int lo_used, gl_used, lo_ent, gl_ent;
    int gsav, lsav;
    int i;
    /* a job that reaches both banks: a fresh global array anchored in
       globaldict, a write to a pre-existing global slot, and local work */
    const char *job =
        "true setglobal globaldict /K [1 2 3 4 5] put "
        "globaldict /K2 42 put false setglobal "
        "/L 12 dict def L /a 1 put L /b (str) put 3 4 add pop";

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL, semantics,
                       XPOST_OUTPUT_MESSAGE_QUIET, XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("%s: xpost_create", what);
        return;
    }
    xpost_job_snapshots_set(ctx, 1);
    xpost_stdout_handler_set(ctx, out_sink, NULL);

    /* the first run establishes the baseline; measure against what it left */
    (void)run_job(ctx, "0 0 moveto 5 5 lineto stroke");
    lo_used = ctx->lo->high_water;
    gl_used = ctx->gl->high_water;
    lo_ent = ctx->lo->table.nextent;
    gl_ent = ctx->gl->table.nextent;
    gsav = gsave_depth(ctx);
    lsav = lsave_depth(ctx);

    for (i = 0; i < JOBS; i++)
    {
        if (run_job(ctx, job) != XPOST_RUN_COMPLETE)
        {
            report_failure("%s: job %d did not complete", what, i);
            break;
        }
        if (ctx->lo->high_water != lo_used || ctx->lo->table.nextent != lo_ent)
        {
            report_failure("%s: local bank grew by job %d "
                           "(used %u->%u, nextent %u->%u): the boundary leaks",
                           what, i, lo_used, ctx->lo->high_water,
                           lo_ent, ctx->lo->table.nextent);
            break;
        }
        if (ctx->gl->high_water != gl_used || ctx->gl->table.nextent != gl_ent)
        {
            report_failure("%s: global bank grew by job %d "
                           "(used %u->%u, nextent %u->%u): the boundary leaks",
                           what, i, gl_used, ctx->gl->high_water,
                           gl_ent, ctx->gl->table.nextent);
            break;
        }
        if (gsave_depth(ctx) != gsav || lsave_depth(ctx) != lsav)
        {
            report_failure("%s: job %d left a save level (global %d, local %d)",
                           what, i, gsave_depth(ctx), lsave_depth(ctx));
            break;
        }
    }

    xpost_stdout_handler_set(ctx, NULL, NULL);
    xpost_destroy(ctx);
}

int main(void)
{
    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    balance(XPOST_SHOWPAGE_NOPAUSE, "nopause");
    balance(XPOST_SHOWPAGE_RETURN, "returning");

    xpost_quit();
    return verdict();
}
