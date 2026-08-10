/* What a graphics load that stops leaves behind, and what it says.
 *
 * The language is read into systemdict, which is opened for writing for
 * as long as the reading lasts and closed on a one-shot when it ends. A
 * page the device cannot provide stops the reading part way, and the two
 * things that follow from that are held here.
 *
 * The name. The report a caller gets is the whole of what it is told --
 * the run that knew the limit is over -- so it has to be the limit the
 * device reached and not a name raised by the interpreter on its way out
 * of the load. $error holds one error, the most recent, so any error
 * raised after the first displaces it.
 *
 * The window. It must be shut when the load ends, however the load
 * ended, and shut for good: a program runs against systemdict as the
 * load left it, and one that could reopen it could redefine the
 * language. Both halves are read off the context, because a run that
 * reaches a program is exactly the run this cannot arrange.
 *
 * The dictionary stack goes with the window. The language is read with
 * systemdict open and current, and the two are one act: a load that shut
 * the window and left the dictionary current would leave every later
 * definition aimed at a dictionary that has just been closed. So the
 * depth the load began at is the depth it must end at.
 *
 * Two devices, for the two ways the refusal is raised. A device whose
 * raster is one block of pixels outside virtual memory is refused by the
 * C that would allocate it, and that refusal reaches the interpreter's
 * own error handler. A device whose raster is virtual memory is refused
 * in PostScript, which never passes through that handler at all. A check
 * put to one of them says nothing about the other.
 *
 * Asked twice, because the load is attempted once per run and the first
 * attempt is the one that meets the device: an interpreter that answers
 * the second attempt with something of its own is the failure this is
 * looking for.
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

/* A side small enough to look like a page whose area is past every
   machine's memory, so no build allocates for it and the refusal is the
   same wherever this runs. */
#define STOPPING_SIDE 2000000000
#define ORDINARY_SIDE 200

static char out_buf[4096];
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

static const char *collect_run(Xpost_Context *ctx, const char *prog,
                               Xpost_Run_Status *st)
{
    out_len = 0;
    *st = xpost_run(ctx, XPOST_INPUT_STRING, prog, 0);
    out_buf[out_len] = '\0';
    return out_buf;
}

/* systemdict is the bottom of the dictionary stack. */
static int systemdict_is_writeable(Xpost_Context *ctx)
{
    return xpost_object_is_writeable(ctx,
               xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0));
}

/* The window is shut, the one-shot that opens it is spent, and the
   dictionary the load made current is closed again. */
static void the_load_left_no_window(Xpost_Context *ctx, const char *dev,
                                    const char *when, int entry_depth)
{
    if (systemdict_is_writeable(ctx))
        report_failure("%s: systemdict is still writeable %s", dev, when);
    if (ctx->sysdict_unlocked)
        report_failure("%s: the writeable window on systemdict is still "
                       "open %s", dev, when);
    if (!ctx->sysdict_load_done)
        report_failure("%s: the one-shot that opens systemdict is unspent "
                       "%s, so it can be opened again", dev, when);
    if (xpost_stack_count(ctx->lo, ctx->ds) != entry_depth)
        report_failure("%s: the dictionary stack is %d deep %s, not the %d "
                       "it began at", dev,
                       xpost_stack_count(ctx->lo, ctx->ds), when, entry_depth);
}

/* A page the device provides: the control that the device starts at all,
   and that a load which ran to its end leaves the same closed window. */
static void an_ordinary_page(const char *dev)
{
    Xpost_Context *ctx;
    Xpost_Run_Status st;
    const char *out;
    int entry_depth;

    ctx = xpost_create(dev, XPOST_OUTPUT_FILENAME, "/dev/null",
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, ORDINARY_SIDE, ORDINARY_SIDE);
    if (!ctx)
    {
        report_failure("%s: no context on a page it provides", dev);
        return;
    }
    entry_depth = xpost_stack_count(ctx->lo, ctx->ds);
    xpost_stdout_handler_set(ctx, out_sink, NULL);
    out = collect_run(ctx, "0 0 moveto 10 10 lineto stroke (drew) print flush",
                      &st);
    if (st != XPOST_RUN_COMPLETE)
        report_failure("%s: a page it provides did not run: %s", dev, out);
    else
        check(strstr(out, "drew") != NULL,
              "a run on a page the device provides reaches the program");
    the_load_left_no_window(ctx, dev, "after a load that ran to its end",
                            entry_depth);
    xpost_stdout_handler_set(ctx, NULL, NULL);
    xpost_destroy(ctx);
}

/* A page the device cannot provide. */
static void a_page_it_cannot_provide(const char *dev)
{
    Xpost_Context *ctx;
    Xpost_Run_Status st;
    const char *out;
    int attempt;
    int entry_depth;

    ctx = xpost_create(dev, XPOST_OUTPUT_FILENAME, "/dev/null",
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, STOPPING_SIDE, STOPPING_SIDE);
    if (!ctx)
    {
        report_failure("%s: no context on a page it cannot provide", dev);
        return;
    }
    entry_depth = xpost_stack_count(ctx->lo, ctx->ds);
    xpost_stdout_handler_set(ctx, out_sink, NULL);

    for (attempt = 1; attempt <= 2; attempt++)
    {
        out = collect_run(ctx, "showpage", &st);

        if (st == XPOST_RUN_COMPLETE)
            report_failure("%s: attempt %d provided a page it cannot hold",
                           dev, attempt);
        if (strstr(out, "unable to load graphics") == NULL)
            report_failure("%s: attempt %d said nothing about the load it "
                           "could not finish: %s", dev, attempt, out);
        /* PLRM 8.2 gives limitcheck for a limit of the implementation and
           VMerror for virtual memory exhausted; which of the two this page
           reaches is the platform's answer, and either is the device's own
           limit. Any other name is one the interpreter raised after it. */
        else if (strstr(out, "limitcheck") == NULL &&
                 strstr(out, "VMerror") == NULL)
            report_failure("%s: attempt %d names no limit the device "
                           "reached: %s", dev, attempt, out);

        the_load_left_no_window(ctx, dev, "after a load that stopped",
                                entry_depth);
    }

    xpost_stdout_handler_set(ctx, NULL, NULL);
    xpost_destroy(ctx);
}

int main(void)
{
    /* raster keeps its pixels outside virtual memory and is refused by
       the C that allocates them; pgm keeps its raster in virtual memory
       and is refused in PostScript, without the interpreter's own error
       handler seeing it. Both are built into every configuration. */
    static const char *const devices[] = { "raster", "pgm" };
    size_t i;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    for (i = 0; i < sizeof devices / sizeof devices[0]; i++)
    {
        an_ordinary_page(devices[i]);
        a_page_it_cannot_provide(devices[i]);
    }

    xpost_quit();

    return verdict();
}
