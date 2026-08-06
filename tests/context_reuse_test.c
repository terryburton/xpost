/*
 * Embedding-contract test: a process may create, use and destroy a
 * context repeatedly.
 *
 * An embedder that serves one job per context creates and destroys
 * contexts for as long as the process lives. Each context owns its
 * operator table and its two memory files, so nothing an earlier
 * context installed or allocated may limit a later one, and a context
 * that has been destroyed must hold nothing: a job server that gained
 * a context's worth of memory per job would grow without bound.
 *
 * The memory claim is measured as the peak resident size, which only
 * ever rises -- so memory returned and reused registers as no growth,
 * while memory retained registers on every cycle. Where the platform
 * does not report it the cycles still run and their results are still
 * checked; only the growth comparison is left out, which the test says
 * so on its output.
 */

#include <stdio.h>
#include <string.h>
#include "xpost.h"

#ifndef _WIN32
# include <sys/time.h>
# include <sys/resource.h>
#endif

#include "xpost_test.h"

#define CYCLES 8

/* the peak resident size of this process in KiB, or 0 where the
   platform does not report it */
static long peak_resident_kib(void)
{
#ifdef _WIN32
    return 0;
#else
    struct rusage ru;

    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
# ifdef __APPLE__
    return (long)(ru.ru_maxrss / 1024); /* bytes */
# else
    return (long)ru.ru_maxrss;          /* KiB */
# endif
#endif
}

static char out_buf[256];
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

int main(void)
{
    long settled = 0;
    long grown = 0;
    int i;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    for (i = 0; i < CYCLES; i++)
    {
        Xpost_Context *ctx;
        Xpost_Run_Status st;

        ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                           XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                           XPOST_USE_SIZE, 100, 100);
        if (!ctx)
        {
            report_failure("context %d of %d was not created", i + 1, CYCLES);
            break;
        }
        xpost_job_snapshots_set(ctx, 0);
        xpost_stdout_handler_set(ctx, out_sink, NULL);

        /* an operator installed into this context's own table, reached
           by name through this context's own dictionaries */
        out_len = 0;
        st = xpost_run(ctx, XPOST_INPUT_STRING, "2 3 add (ok) print flush", 0);
        out_buf[out_len] = '\0';
        check(st == XPOST_RUN_COMPLETE, "each context runs a program");
        check(strcmp(out_buf, "ok") == 0, "each context reaches its operators");

        xpost_stdout_handler_set(ctx, NULL, NULL);
        xpost_destroy(ctx);

        /* the first two cycles reach the peak that serving one context
           costs; from there a further cycle must cost nothing */
        if (i == 1)
            settled = peak_resident_kib();
        grown = peak_resident_kib();
    }

    if (settled > 0)
        check(grown - settled < 512,
              "a destroyed context leaves the process no larger");
    else
        printf("NOTE: peak resident size unavailable; growth not compared\n");

    xpost_quit();

    return verdict();
}
