/*
 * Embedding-contract test: a process may create, use and destroy a
 * context repeatedly.
 *
 * An embedder that serves one job per context creates and destroys
 * contexts for as long as the process lives. Each context owns its
 * operator table and its two memory files, so nothing an earlier
 * context installed or allocated may limit a later one.
 */

#include <stdio.h>
#include <string.h>
#include "xpost.h"

#define CYCLES 5

static int failures = 0;

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

static void check(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    int i;

    if (!xpost_init())
    {
        printf("FAIL: xpost_init\n");
        return 1;
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
            printf("FAIL: context %d of %d was not created\n", i + 1, CYCLES);
            failures++;
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
    }

    xpost_quit();

    if (failures)
    {
        printf("FAILURES: %d\n", failures);
        return 1;
    }
    printf("SUCCESS\n");
    return 0;
}
