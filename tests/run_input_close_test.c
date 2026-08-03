/*
 * Embedding-contract test: a run closes the file it wrapped around the
 * program, however the run ended.
 *
 * xpost_run given a program as a string writes it to a temporary file
 * and executes that. A run that reads its program to the end closes the
 * file there, but a run that stops before the end -- which is every run
 * that errors -- left it open. A context serving one job per request
 * gained an open descriptor for every job that failed, and once it held
 * as many as the process is allowed it could not run anything at all,
 * for the rest of its life.
 *
 * The descriptor allowance is lowered here so the test reaches that
 * point in a moment rather than after a thousand jobs. Where the
 * platform does not let it be lowered the jobs still run and their
 * results are still checked; the test says on its output that it could
 * not narrow the allowance.
 */

#include <stdio.h>
#include <string.h>
#include "xpost.h"

#ifndef _WIN32
# include <sys/time.h>
# include <sys/resource.h>
#endif

/* comfortably more jobs than the allowance below */
#define JOBS 200
#define ALLOWANCE 64

static int failures = 0;

static void check(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* lower this process's open-file allowance; 1 if it was lowered */
static int narrow_allowance(void)
{
#ifdef _WIN32
    return 0;
#else
    struct rlimit rl;

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
        return 0;
    if (rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur > (rlim_t)ALLOWANCE)
        rl.rlim_cur = (rlim_t)ALLOWANCE;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
        return 0;
    return 1;
#endif
}

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Run_Status st;
    int narrowed;
    int i;

    narrowed = narrow_allowance();
    if (!narrowed)
        printf("NOTE: the open-file allowance could not be narrowed\n");

    if (!xpost_init())
    {
        printf("FAIL: xpost_init\n");
        return 1;
    }
    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        printf("FAIL: xpost_create\n");
        return 1;
    }
    xpost_job_snapshots_set(ctx, 0);

    /* every one of these ends in an uncaught error, so none of them
       reaches the end of its program */
    for (i = 0; i < JOBS; i++)
    {
        st = xpost_run(ctx, XPOST_INPUT_STRING, "1 0 div", 0);
        if (st != XPOST_RUN_ERRORED)
        {
            printf("FAIL: erroring job %d of %d reported %d, not an error\n",
                   i + 1, JOBS, (int)st);
            failures++;
            break;
        }
    }

    /* a job that completes, and one that errors, still work afterwards */
    st = xpost_run(ctx, XPOST_INPUT_STRING, "/survived 1 def", 0);
    check(st == XPOST_RUN_COMPLETE,
          "a job still completes after many erroring jobs");
    st = xpost_run(ctx, XPOST_INPUT_STRING, "survived 1 eq { } { 1 0 div } ifelse", 0);
    check(st == XPOST_RUN_COMPLETE,
          "the definition the completed job made is still there");

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
