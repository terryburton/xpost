/* Embedding contract: one interpreter instance at a time.
 *
 * The interpreter is a single instance holding a table of execution
 * contexts, so a process gets one instance from xpost_create and keeps
 * it until xpost_destroy. A second xpost_create while the first
 * instance is live is refused with NULL, the failure return every
 * caller already tests for, and the live instance goes on running.
 *
 * The refusal is about liveness and not about the arguments: the same
 * arguments create an instance before the first exists and again after
 * it is destroyed, which is what makes the middle call's NULL mean what
 * it says.
 */

#include <stdio.h>
#include <string.h>

#include "xpost.h"

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

static Xpost_Context *make(void)
{
    return xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                        XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                        XPOST_USE_SIZE, 100, 100);
}

/* whether the context runs a program and reaches its operators */
static int runs(Xpost_Context *ctx)
{
    Xpost_Run_Status st;

    out_len = 0;
    out_buf[0] = '\0';
    st = xpost_run(ctx, XPOST_INPUT_STRING, "2 3 add (ok) print flush", 0);
    out_buf[out_len < sizeof out_buf ? out_len : sizeof out_buf - 1] = '\0';
    return st == XPOST_RUN_COMPLETE && strcmp(out_buf, "ok") == 0;
}

int main(void)
{
    Xpost_Context *first;
    Xpost_Context *second;
    Xpost_Context *later;

    if (!xpost_init())
    {
        printf("FAIL: xpost_init\n");
        return 1;
    }

    first = make();
    if (!first)
    {
        printf("FAIL: the first instance was not created\n");
        xpost_quit();
        return 1;
    }
    xpost_job_snapshots_set(first, 0);
    xpost_stdout_handler_set(first, out_sink, NULL);

    second = make();
    check(second == NULL, "a second instance is refused while one is live");
    if (second && second != first)
        xpost_destroy(second);

    check(runs(first), "the live instance runs after the refusal");

    xpost_stdout_handler_set(first, NULL, NULL);
    xpost_destroy(first);

    /* the same arguments, so a success here says the refusal above was
       about the live instance and not about what was asked for */
    later = make();
    check(later != NULL, "an instance is created once the previous one is destroyed");
    if (later)
    {
        xpost_job_snapshots_set(later, 0);
        xpost_stdout_handler_set(later, out_sink, NULL);
        check(runs(later), "the instance created afterwards runs a program");
        xpost_stdout_handler_set(later, NULL, NULL);
        xpost_destroy(later);
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
