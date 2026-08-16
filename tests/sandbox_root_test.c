/* A permitted directory that is itself a path separator contains what is
   under it.

   The containment rule is a prefix that ends at a separator: a permitted
   "/tmp" contains "/tmp/x" because the byte after the prefix is the "/"
   between them. The filesystem root has no such byte -- it *is* the
   separator -- so matched the same way it would contain nothing, and a
   caller that permitted the root would have permitted the one directory
   that admits no file. Silently, and backwards: the widest permit anyone
   can write would be the narrowest one in force.

   This is its own program because the permit set belongs to the process
   and the latch is one-way. A test that permits the root cannot then check
   that anything is refused for being outside a tree, so it cannot share a
   process with the tests that do.

   Read is permitted here and write is not, which is also the negative
   control: if the refusals below came from the sandbox being off rather
   than from the permit being read-only, the write would go through. */

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

#include "xpost.h"
#include "xpost_compat.h" /* xpost_realpath: the permit set is canonical */

#include "xpost_test.h"

static size_t discard(void *user, const char *buf, size_t len)
{
    (void)user; (void)buf;
    return len;
}

static int completes(Xpost_Context *ctx, const char *prog)
{
    return xpost_run(ctx, XPOST_INPUT_STRING, prog, 0) == XPOST_RUN_COMPLETE;
}

static int errors_with(Xpost_Context *ctx, const char *prog, const char *name)
{
    Xpost_Run_Status st = xpost_run(ctx, XPOST_INPUT_STRING, prog, 0);
    return st == XPOST_RUN_ERRORED &&
           strcmp(xpost_error_name_get(ctx), name) == 0;
}

int main(void)
{
    Xpost_Context *ctx;
    char name[] = "xpost_sbr_XXXXXX";
    char prog[1400];
    char *target;
    FILE *w;

    /* a file of our own, named absolutely: under a root permit its whole
       path is inside the permitted tree, and under no other permit is any
       of it */
    w = fopen(name, "wb");
    if (!w) { report_failure("could not make a file to read"); return verdict(); }
    fputs("DATA", w);
    fclose(w);
    target = xpost_realpath(name);
    if (!target) { report_failure("could not resolve it"); unlink(name); return verdict(); }

    if (!xpost_init()) { report_failure("xpost_init"); return verdict(); }
    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_RETURN, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx) { report_failure("xpost_create"); return verdict(); }
    xpost_job_snapshots_set(ctx, 0);
    xpost_stderr_handler_set(ctx, discard, NULL);

    check(xpost_path_permit_read("/") == 1, "the root is permitted for reading");
    xpost_path_control_engage();

    snprintf(prog, sizeof prog, "(%s) (r) file closefile", target);
    check(completes(ctx, prog), "a file under the permitted root reads");

    /* nothing was permitted for writing, so nothing may be written --
       including under the root that may be read */
    snprintf(prog, sizeof prog, "(%s) (w) file", target);
    check(errors_with(ctx, prog, "invalidfileaccess"),
          "a write is refused under a read-only root");

    /* the permitted directory is not a file within it */
    check(errors_with(ctx, "(/) (r) file", "invalidfileaccess"),
          "the root itself is not openable");

    xpost_destroy(ctx);
    xpost_quit();

    free(target);
    unlink(name);
    return verdict();
}
