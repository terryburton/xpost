/* A loader that fails after it has opened a face gives the face back.
 *
 * The four font loaders each open a face over a font program the
 * dictionary carries, and then write what the face says into that same
 * dictionary. Between those two acts the face is held by nothing: the
 * block that carries its release is what the handover is about to make,
 * so a collection reaches nothing and there is no other owner to ask.
 * A loader that returns from in there returns holding a face, and the
 * program it was opened over with it.
 *
 * Nothing sees that at the end of a run. The library takes every face
 * still open with it when the font machinery goes down, so a process
 * that leaked one on every load and a process that leaked none end
 * alike; it is a long-lived run that pays. So the assertion here is how
 * many faces the module holds while it runs, read either side of a load
 * that fails, and the failure is arranged rather than waited for.
 *
 * What arranges it is a font dictionary the loader cannot write to. The
 * loader reads the program out of the dictionary, opens the face, and
 * puts /FontBBox back; a dictionary the program has made read-only
 * refuses that put the way it refuses any other, which lands the loader
 * on the path under test with a face open and its program malloc'd.
 * That is an ordinary error return reached without arranging for an
 * allocation to fail, and it is the same return every allocation failure
 * on that path takes.
 *
 * A run is a job that rewinds, so the loads happen in one run and the
 * count is read from inside it. What reads it is the handler taking the
 * run's output: the program prints a letter at each point the count is
 * wanted, and the count is taken as the letter arrives. What the program
 * printed and what was held when it printed it are then one record.
 *
 * The controls carry the weight. A load refused before the face is
 * opened must leave the count alone, or a count that never moved would
 * satisfy the assertion without the release having run; a load that
 * succeeds must raise it by one, or a loader that closed the face it had
 * just handed over would pass; and each load must end the way its case
 * needs, or the path meant to be under test was not the one taken.
 *
 * A host with no TrueType face carries no program to load and the run
 * says so, the way the suite says it elsewhere: the note is in the log
 * and the verdict is computed from the tally, there being nothing here
 * that went wrong.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "xpost.h"
#include "xpost_object.h"
#include "xpost_font.h"

#include "xpost_test.h"

/* What the run printed, and how many faces were open as each character
   of it arrived. */
static char out_buf[256];
static long out_held[256];
static size_t out_len;

static size_t out_sink(void *user, const char *buf, size_t len)
{
    size_t i;

    (void)user;
    for (i = 0; i < len && out_len < sizeof out_buf - 1; i++)
    {
        out_held[out_len] = xpost_font_faces_held();
        out_buf[out_len++] = buf[i];
    }
    out_buf[out_len] = '\0';
    return len;
}

/* How many faces were open as the given letter was printed, or -1 where
   the run did not print it. */
static long held_at(char c)
{
    const char *p = strchr(out_buf, c);

    if (!p)
        return -1;
    return out_held[p - out_buf];
}

/* The run. A Type 42 dictionary needs a program, and the program comes
   from whatever TrueType face this host resolves; where there is none
   the run stops after the search and the test says so rather than
   passing.

   Each load is bracketed by a mark and a cleartomark, so that one which
   stopped part-way leaves the stack as the next finds it, and each
   prints a letter for how it ended and a letter for the count to be read
   at. The font that loads is kept under a name, so nothing between one
   reading and the next can collect it. */
static const char *prog =
    "/tt null def\n"
    "[ /DejaVuSans /LiberationSans /NotoSans /FreeSerif /Arial /Helvetica\n"
    "  /Times-Roman /Courier ]\n"
    "{ /nm exch def\n"
    "  mark { nm findfont } stopped\n"
    "  { cleartomark }\n"
    "  { /f exch def cleartomark\n"
    "    f /FontType get 42 eq { /tt f def exit } if } ifelse\n"
    "} forall\n"
    "tt null eq { flush quit } if\n"
    "/loader 1183615869 internaldict /.loadfont42 get def\n"
    "/mkfont { 5 dict dup /sfnts tt /sfnts get put } bind def\n"
    /* the baseline: the faces this run holds that are no load's */
    "(S) print flush\n"
    /* a dictionary carrying no program at all: refused before a face */
    "mark { 5 dict loader exec } stopped { (n) print }{ (N) print } ifelse\n"
    "cleartomark (A) print flush\n"
    /* the load that succeeds, and keeps its face */
    "mark { mkfont dup loader exec } stopped { (o) print }\n"
    "{ /kept exch def (O) print } ifelse cleartomark (B) print flush\n"
    /* the load that fails after it has opened a face */
    "mark { mkfont readonly dup loader exec } stopped { (r) print }\n"
    "{ (R) print } ifelse cleartomark (C) print flush\n";

int main(void)
{
    Xpost_Context *ctx;
    long s, a, b, c;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        xpost_quit();
        return verdict();
    }

    xpost_stdout_handler_set(ctx, out_sink, NULL);
    (void)xpost_run(ctx, XPOST_INPUT_STRING, prog, 0);
    xpost_stdout_handler_set(ctx, NULL, NULL);

    s = held_at('S');
    a = held_at('A');
    b = held_at('B');
    c = held_at('C');

    if (s < 0)
    {
        /* said the way the suite says it elsewhere: a note the log
           carries, and a verdict computed from the tally like any
           other, since there is nothing here that went wrong */
        printf("skip no Type 42 face on this host: a load is not reached\n");
    }
    else
    {
        /* each load ended the way its case needs, or the path under test
           was not the one taken */
        if (!strchr(out_buf, 'n'))
            report_failure("a dictionary carrying no program was loaded");
        if (!strchr(out_buf, 'O'))
            report_failure("a Type 42 dictionary carrying a program was"
                           " refused");
        if (!strchr(out_buf, 'r'))
            report_failure("a load into a dictionary it cannot write to was"
                           " not refused, so the path under test was not"
                           " reached");

        if (a != s)
            report_failure("a load refused before it opened a face left %ld"
                           " faces open where the %ld before it were"
                           " expected", a, s);
        if (b != s + 1)
            report_failure("a load that succeeded left %ld faces open where"
                           " one more than the %ld before it was expected",
                           b, s);
        if (c != b)
            report_failure("a load that failed after opening a face left %ld"
                           " faces open where the %ld before it were"
                           " expected", c, b);
    }

    xpost_destroy(ctx);
    xpost_quit();
    return verdict();
}
