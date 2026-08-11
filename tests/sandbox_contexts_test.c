/* The file-access sandbox across the contexts of one process.
 *
 * The sandbox belongs to the process, not to a context. The latch is
 * one-way and there is one permitted set, frozen when the latch engages,
 * so a context created afterwards finds the sandbox engaged and finds the
 * same permitted directories. That is not incidental: a context reads its
 * own start-up files through the same enforcement as everything else, so
 * the directory holding them is permitted by whichever context reached it
 * first and every later one relies on that entry still being there. What
 * the sandbox confines is this process's disk access against the program
 * it runs; it does not divide one job in the process from another.
 *
 * Two things follow, and this test holds both.
 *
 * Permitting a directory the set already covers changes nothing, so it
 * costs no room however many times a process asks. An embedder serving a
 * job per context asks once per context for the start-up directory alone,
 * and a table that grew by an entry each time would fill, after which no
 * directory could be permitted at all -- including the one the next job
 * needs.
 *
 * A permit that is not granted says so. The set is frozen once engaged,
 * so a later job's prolog asking for a directory of its own cannot have
 * it; being told is the difference between a prolog that fails and a
 * prolog that believes it configured a sandbox it does not have.
 */

#ifndef _GNU_SOURCE
# define _GNU_SOURCE /* mkdtemp */
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* the Windows CRT mkdir takes no mode argument */
#ifdef _WIN32
# define test_mkdir(p) mkdir(p)
#else
# define test_mkdir(p) mkdir((p), 0700)
#endif

#include "xpost.h"

#include "xpost_test.h"

/* More create/destroy cycles than the permit table holds entries, so a
   table that gained one per context would be full before the end. */
#define CYCLES 70

/* Repeats of one directory, likewise more than the table holds. */
#define REPEATS 200

/* Distinct directories permitted after the cycles: more than could be
   taken by a table those cycles had filled, and few enough to leave the
   room the rest of the test spends. */
#define SPARE_DIRS 32

/* swallow the interpreter's error report for the runs meant to fail */
static size_t discard(void *user, const char *buf, size_t len)
{
    (void)user; (void)buf;
    return len;
}

static Xpost_Context *make_context(void)
{
    Xpost_Context *ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                                      XPOST_SHOWPAGE_RETURN,
                                      XPOST_OUTPUT_MESSAGE_QUIET,
                                      XPOST_USE_SIZE, 100, 100);

    if (ctx)
    {
        xpost_job_snapshots_set(ctx, 0);
        xpost_stderr_handler_set(ctx, discard, NULL);
    }
    return ctx;
}

/* run a one-line program and report whether it completed */
static int completes(Xpost_Context *ctx, const char *prog)
{
    return xpost_run(ctx, XPOST_INPUT_STRING, prog, 0) == XPOST_RUN_COMPLETE;
}

/* run a program expected to be refused by the sandbox */
static int refused(Xpost_Context *ctx, const char *prog)
{
    Xpost_Run_Status st = xpost_run(ctx, XPOST_INPUT_STRING, prog, 0);

    return st == XPOST_RUN_ERRORED &&
           strcmp(xpost_error_name_get(ctx), "invalidfileaccess") == 0;
}

int main(void)
{
    Xpost_Context *ctx;
    char root[] = "xpost_sbc_XXXXXX";  /* relative: a native binary need not share /tmp */
    char absent[600];
    char repeated[600];
    char tenant_a[600];
    char tenant_b[600];
    char file_a[700];
    char file_b[700];
    char spare[700];
    char prog[1400];
    FILE *w;
    int granted;
    int i;

    if (!mkdtemp(root)) { report_failure("mkdtemp"); return verdict(); }
    snprintf(absent, sizeof absent, "%s/absent", root);   /* never created */
    snprintf(repeated, sizeof repeated, "%s/repeated", root);
    test_mkdir(repeated);
    snprintf(tenant_a, sizeof tenant_a, "%s/tenant_a", root);
    test_mkdir(tenant_a);
    snprintf(tenant_b, sizeof tenant_b, "%s/tenant_b", root);
    test_mkdir(tenant_b);
    snprintf(file_a, sizeof file_a, "%s/data", tenant_a);
    w = fopen(file_a, "wb");
    if (w) { fputs("A", w); fclose(w); }
    snprintf(file_b, sizeof file_b, "%s/data", tenant_b);
    w = fopen(file_b, "wb");
    if (w) { fputs("B", w); fclose(w); }

    if (!xpost_init()) { report_failure("xpost_init"); return verdict(); }

    /* The control for every permit below: the answer distinguishes, so a
       run of ones is a run of grants rather than a function that cannot
       say no. */
    check(xpost_path_permit_read(absent) == 0,
          "a directory that is not there is not permitted");

    /* asking again for what is already permitted costs no room */
    granted = 1;
    for (i = 0; i < REPEATS; i++)
        granted = granted && xpost_path_permit_read(repeated);
    check(granted, "one directory may be permitted repeatedly");

    /* nor does a context's own start-up permit, once per context */
    for (i = 0; i < CYCLES; i++)
    {
        Xpost_Context *cycle = make_context();

        if (!cycle) { report_failure("xpost_create on cycle %d", i); return verdict(); }
        xpost_destroy(cycle);
    }

    /* so the room is still there for directories that need it */
    granted = 1;
    for (i = 0; i < SPARE_DIRS; i++)
    {
        snprintf(spare, sizeof spare, "%s/spare_%02d", root, i);
        test_mkdir(spare);
        granted = granted && xpost_path_permit_read(spare);
    }
    check(granted, "distinct directories are still permitted after many contexts");

    /* and a program run in a context after all of those still gets the
       sandbox it asks for */
    ctx = make_context();
    if (!ctx) { report_failure("xpost_create for the permitted run"); return verdict(); }
    snprintf(prog, sizeof prog, "(%s) .permitfileread .lockdown", tenant_a);
    check(completes(ctx, prog), "a program permits its directory and engages");
    snprintf(prog, sizeof prog, "(%s) (r) file closefile", file_a);
    check(completes(ctx, prog), "the directory it permitted is readable");
    snprintf(prog, sizeof prog, "(%s) (r) file", file_b);
    check(refused(ctx, prog), "a directory it did not permit is refused");
    xpost_destroy(ctx);

    /* A context created after all that: it asked for no sandbox and is in
       one, holding what was permitted before it existed. This is the
       process-wide latch, and the entries it froze are the same entries
       this context read its own start-up files through. */
    ctx = make_context();
    if (!ctx) { report_failure("xpost_create after engaging"); return verdict(); }
    snprintf(prog, sizeof prog, "(%s) (r) file closefile", file_a);
    check(completes(ctx, prog), "a later context sees the permitted set");
    snprintf(prog, sizeof prog, "(%s) (r) file", file_b);
    check(refused(ctx, prog), "a later context is confined by the engaged sandbox");

    /* what it may not do is quietly fail to extend that set */
    snprintf(prog, sizeof prog, "(%s) .permitfileread", tenant_b);
    check(refused(ctx, prog), "a permit the frozen set cannot take is refused");
    snprintf(prog, sizeof prog, "(%s) (r) file", file_b);
    check(refused(ctx, prog), "and the directory it asked for stays unreadable");
    xpost_destroy(ctx);

    /* the same pair at the C interface: already covered is granted, which
       is what a later context's start-up permit is, and anything else is
       refused */
    check(xpost_path_permit_read(tenant_a) == 1,
          "a directory the engaged set covers is permitted");
    check(xpost_path_permit_read(tenant_b) == 0,
          "a directory the engaged set does not cover is refused");

    xpost_quit();

    /* cleanup (best effort) */
    unlink(file_a);
    unlink(file_b);
    for (i = 0; i < SPARE_DIRS; i++)
    {
        snprintf(spare, sizeof spare, "%s/spare_%02d", root, i);
        rmdir(spare);
    }
    rmdir(tenant_a);
    rmdir(tenant_b);
    rmdir(repeated);
    rmdir(root);

    return verdict();
}
