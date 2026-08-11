/*
 * The key a wrapped call's saved-operand array is kept under, in every
 * context a process brings up.
 *
 * A PostScript-implemented operator copies the operands its call runs
 * on into an array held in the interpreter's private dictionary under
 * .wrapsave. Both the dictionary and the key are the language's: a
 * program reaches the array by that name, and the two tests that hold
 * the copy to its bounds -- wrapped_save and wrapsave-bank -- reach it
 * by that name to put something else there.
 *
 * A name is an index into the name stack of the context it was interned
 * in, and means nothing in another context: the same index there names
 * whatever that context's own boot happened to intern at it, or nothing
 * at all. So the name has to be interned in the context whose array it
 * names, once per context rather than once per process.
 *
 * That is what this measures, and it takes a process with more than one
 * context to measure it: the first context of a process is right either
 * way. Contexts are created and destroyed in turn, and each is asked to
 * drop the entry and then make one wrapped call, which rebuilds it. Two
 * things are required of what comes back. It is under .wrapsave, which
 * is what says a program can reach it. And it is the one entry the call
 * added, which is what says the rebuild did not go somewhere else and
 * leave privatedict a key the language does not name -- a key that, on
 * a later intern, comes to alias whatever real name lands at its index.
 *
 * The wrapped call is .gscratch, a wrapped operator the language always
 * has: its procedure takes no operands and yields the local scratch
 * dictionary, so the call is a call and nothing else. A call copies the
 * operands standing when it is made and copies nothing where there are
 * none, so the probe leaves one standing for it to copy.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "xpost.h"

#include "xpost_test.h"

/* Three rather than two: the second context is where a name held over
   from the first is first read, and the third is where one held over
   from the second would be. */
#define CONTEXTS 3

/* Drop the entry, make one wrapped call to rebuild it, and report the
   two things required of what came back. */
static const char *const probe =
    ".privatedict /.wrapsave undef "
    "/n0 .privatedict length def "
    "0 .gscratch pop pop "
    ".privatedict /.wrapsave known { (key) }{ (nokey) } ifelse print "
    ".privatedict length n0 1 add eq { (+1) }{ (+other) } ifelse print "
    "flush";

#define WANTED "key+1"

static char out_buf[256];
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

int main(void)
{
    int i;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    for (i = 0; i < CONTEXTS; i++)
    {
        Xpost_Context *ctx;
        Xpost_Run_Status st;

        ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                           XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                           XPOST_USE_SIZE, 100, 100);
        if (!ctx)
        {
            report_failure("context %d of %d was not created", i + 1, CONTEXTS);
            break;
        }
        xpost_job_snapshots_set(ctx, 0);
        xpost_stdout_handler_set(ctx, out_sink, NULL);

        out_len = 0;
        st = xpost_run(ctx, XPOST_INPUT_STRING, probe, 0);
        out_buf[out_len < sizeof out_buf ? out_len : sizeof out_buf - 1] = '\0';

        if (st != XPOST_RUN_COMPLETE)
            report_failure("context %d of %d did not run the probe to "
                           "completion", i + 1, CONTEXTS);
        else if (strcmp(out_buf, WANTED) != 0)
            report_failure("context %d of %d rebuilt the saved-operand array "
                           "as `%s', where a rebuild under .wrapsave and "
                           "nowhere else reads `%s'",
                           i + 1, CONTEXTS, out_buf, WANTED);

        xpost_stdout_handler_set(ctx, NULL, NULL);
        xpost_destroy(ctx);
    }

    xpost_quit();

    return verdict();
}
