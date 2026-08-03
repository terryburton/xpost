/*
 * Embedding-contract test: the page operators observe the page
 * semantics the embedder chose.
 *
 * An embedder selects one of three behaviours at a page boundary
 * (Xpost_Showpage_Semantics): pause and announce the page on the
 * standard output, do neither, or return control to the caller. Both
 * showpage and copypage end a page -- copypage transmits it without
 * erasing it (PLRM 8.2) -- so both must take the behaviour that was
 * chosen. An operator that announces a page under the quiet semantics
 * writes into the program's own output stream, and one that reads the
 * line editor there consumes a line of the program's input.
 */

#include <stdio.h>
#include <string.h>
#include "xpost.h"

static int failures = 0;

static char out_buf[512];
static size_t out_len = 0;

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

static void check(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* run one program with the standard output captured; the captured text
   is left in out_buf, terminated */
static Xpost_Run_Status run_captured(Xpost_Context *ctx, const char *prog)
{
    Xpost_Run_Status st;

    out_len = 0;
    st = xpost_run(ctx, XPOST_INPUT_STRING, prog, 0);
    out_buf[out_len] = '\0';
    return st;
}

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Run_Status st;

    if (!xpost_init())
    {
        printf("FAIL: xpost_init\n");
        return 1;
    }

    /* --- the quiet semantics: neither operator announces a page --- */

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        printf("FAIL: xpost_create nopause\n");
        return 1;
    }
    xpost_job_snapshots_set(ctx, 0);
    xpost_stdout_handler_set(ctx, out_sink, NULL);

    st = run_captured(ctx, "(a) print showpage (b) print flush");
    check(st == XPOST_RUN_COMPLETE, "showpage completes under the quiet semantics");
    check(strcmp(out_buf, "ab") == 0,
          "showpage writes nothing of its own under the quiet semantics");

    st = run_captured(ctx, "(a) print copypage (b) print flush");
    check(st == XPOST_RUN_COMPLETE, "copypage completes under the quiet semantics");
    check(strcmp(out_buf, "ab") == 0,
          "copypage writes nothing of its own under the quiet semantics");

    xpost_stdout_handler_set(ctx, NULL, NULL);
    xpost_destroy(ctx);

    /* --- the returning semantics: both operators hand back control --- */

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_RETURN, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        printf("FAIL: xpost_create return\n");
        return 1;
    }
    xpost_job_snapshots_set(ctx, 0);

    st = xpost_run(ctx, XPOST_INPUT_STRING, "showpage /a 1 def", 0);
    check(st == XPOST_RUN_YIELDED, "showpage returns control to the caller");
    st = xpost_run(ctx, XPOST_INPUT_RESUME, "", 0);
    check(st == XPOST_RUN_COMPLETE, "the run resumes past showpage");

    st = xpost_run(ctx, XPOST_INPUT_STRING, "copypage /b 1 def", 0);
    check(st == XPOST_RUN_YIELDED, "copypage returns control to the caller");
    st = xpost_run(ctx, XPOST_INPUT_RESUME, "", 0);
    check(st == XPOST_RUN_COMPLETE, "the run resumes past copypage");

    st = xpost_run(ctx, XPOST_INPUT_STRING,
                   "a 1 eq b 1 eq and { (STATE-OK) print } if flush", 0);
    check(st == XPOST_RUN_COMPLETE, "the context survives both page boundaries");

    xpost_destroy(ctx);
    xpost_quit();

    if (failures)
    {
        printf("FAILURES: %d\n", failures);
        return 1;
    }
    printf("SUCCESS\n");
    return 0;
}
