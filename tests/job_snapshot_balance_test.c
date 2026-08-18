/* What a job does with the global save level it could not take.
 *
 * A context set to snapshot its jobs takes a save level over each memory
 * file at the start of a run and rewinds to it at the end, so that what
 * one job did to virtual memory is not what the next job starts from. A
 * save level is the substack its records are pushed onto, and that
 * substack is an allocation: a memory file with no room for it answers
 * the request with a null and pushes nothing.
 *
 * The rewind is the half that matters. It pops the save stack and plays
 * the records it finds back over VM. A run that pushed no level and
 * rewinds regardless pops whichever level is there -- one belonging to
 * whatever put it there, not to this job -- and reverts that owner's
 * objects to contents they had before it began, out from under it. The
 * local snapshot is read back before its rewind; the global one is the
 * one this holds to doing the same.
 *
 * Both halves are asserted, and each with the refusal and without it.
 * The refusal has to be shown to be a refusal -- a snapshot that
 * succeeded would make the second half pass while testing nothing -- and
 * the balance has to be shown to hold over a job that took its level
 * normally, since a run that never rewound would also leave the stack
 * where it found it.
 *
 * The refusal is induced by putting the memory file at the far end of
 * the address range its offsets are unsigned ints of, which is where its
 * growth is declined. Nothing is written on that path, so the two fields
 * are the whole of the change and putting them back is the whole of
 * undoing it. It is a refusal that does not move: every allocation in
 * that file is declined for as long as it stands, rather than the next
 * one happening to fit.
 *
 * The bracket is one call's, and a job is not always one call. Under
 * XPOST_SHOWPAGE_RETURN the job hands control back at each showpage and
 * ends on a later call, so no bracket is taken over it: a level pushed
 * by the first call is one no call of that job rewinds, and a context
 * serving job after job would gather one per job. Both halves of that
 * are held here -- the stacks stay where they were, and each job starts
 * from the virtual memory the one before it left, which is where the
 * graphics the first job loaded are.
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
#include "xpost_save.h"

#include "xpost_test.h"

static unsigned int held_used;
static unsigned int held_max;

/* Decline every allocation in mem until released. */
static void refuse_allocation(Xpost_Memory_File *mem)
{
    held_used = mem->high_water;
    held_max = mem->max;
    mem->high_water = 0xfffffff8u;
    mem->max = 0xfffffff8u;
}

static void allow_allocation(Xpost_Memory_File *mem)
{
    mem->high_water = held_used;
    mem->max = held_max;
}

static int global_save_depth(Xpost_Context *ctx)
{
    return xpost_stack_count(ctx->gl, xpost_memory_save_stack_ent(ctx->gl));
}

static int local_save_depth(Xpost_Context *ctx)
{
    return xpost_stack_count(ctx->lo, xpost_memory_save_stack_ent(ctx->lo));
}

static char out_buf[64];
static size_t out_len;

static size_t out_sink(void *user, const char *buf, size_t len)
{
    (void)user;
    if (out_len + len < sizeof out_buf)
    {
        memcpy(out_buf + out_len, buf, len);
        out_len += len;
    }
    return len;
}

/* Jobs on a context whose showpage returns to the caller. */
static void returning_jobs_leave_the_save_stacks_alone(void)
{
    Xpost_Context *ctx;
    int gdepth;
    int ldepth;
    int i;

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_RETURN, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return;
    }
    xpost_stdout_handler_set(ctx, out_sink, NULL);

    gdepth = global_save_depth(ctx);
    ldepth = local_save_depth(ctx);

    for (i = 0; i < 3; i++)
    {
        out_len = 0;
        (void) xpost_run(ctx, XPOST_INPUT_STRING,
                         "0 0 moveto 10 10 lineto stroke (drew) print flush", 0);
        out_buf[out_len] = '\0';
        /* the graphics this job needs were loaded by the first of them:
           a job whose virtual memory was rewound out from under it comes
           back with them gone */
        check(strcmp(out_buf, "drew") == 0,
              "every job of a returning context runs against the graphics");
    }

    if (global_save_depth(ctx) != gdepth)
        report_failure("jobs that return at showpage left the global save "
                       "stack at %d, not %d",
                       global_save_depth(ctx), gdepth);
    if (local_save_depth(ctx) != ldepth)
        report_failure("jobs that return at showpage left the local save "
                       "stack at %d, not %d",
                       local_save_depth(ctx), ldepth);

    xpost_stdout_handler_set(ctx, NULL, NULL);
    xpost_destroy(ctx);
}

/* The snapshot's own answer, with the refusal and without it. */
static void snapshot_reports_refusal(void)
{
    Xpost_Context *ctx;
    Xpost_Object v;
    int depth;

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return;
    }

    depth = global_save_depth(ctx);
    v = xpost_save_create_snapshot_object(ctx->gl);
    if (xpost_object_get_type(v) != savetype)
        report_failure("a snapshot over sound memory was refused");
    else if (global_save_depth(ctx) != depth + 1)
        report_failure("a snapshot that was taken did not reach the save stack");

    depth = global_save_depth(ctx);
    refuse_allocation(ctx->gl);
    v = xpost_save_create_snapshot_object(ctx->gl);
    allow_allocation(ctx->gl);
    if (xpost_object_get_type(v) == savetype)
        report_failure("a snapshot with no room for its records answered "
                       "as though it had been taken");
    if (global_save_depth(ctx) != depth)
        report_failure("a refused snapshot moved the save stack");

    xpost_destroy(ctx);
}

/* What the run does with the level, with the refusal and without it.
   A context per job: the level placed below is one the job must leave
   alone, and a job that ran before it would have taken and rewound one
   of its own over the same global VM. */
static void a_job_rewinds_only_its_own_level(int refused)
{
    Xpost_Context *ctx;
    int depth;

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return;
    }

    /* a level over global VM that is not this job's, which is what a job
       suspended between showpage and its continuation leaves behind */
    if (xpost_object_get_type(xpost_save_create_snapshot_object(ctx->gl))
        != savetype)
    {
        report_failure("could not place a save level for the job to find");
        xpost_destroy(ctx);
        return;
    }
    depth = global_save_depth(ctx);

    if (refused)
        refuse_allocation(ctx->gl);
    (void) xpost_run(ctx, XPOST_INPUT_STRING, "quit", 0);
    if (refused)
        allow_allocation(ctx->gl);

    if (global_save_depth(ctx) != depth)
        report_failure(refused
                       ? "a job that could not take a global snapshot rewound "
                         "a save level it never took: stack at %d, not %d"
                       : "a job that took a global snapshot did not leave the "
                         "save stack at %d, but at %d",
                       refused ? global_save_depth(ctx) : depth,
                       refused ? depth : global_save_depth(ctx));

    xpost_destroy(ctx);
}

int main(void)
{
    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    snapshot_reports_refusal();
    a_job_rewinds_only_its_own_level(0);
    a_job_rewinds_only_its_own_level(1);
    returning_jobs_leave_the_save_stacks_alone();

    xpost_quit();

    return verdict();
}
