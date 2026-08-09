/*
 * What the two clocks count.
 *
 * PLRM 8.2 gives them different jobs. realtime counts in real time,
 * independently of the interpreter's execution; usertime counts the
 * execution the interpreter has done, one for every millisecond of it.
 * An interval in which the interpreter runs nothing therefore shows on
 * one and not the other, and that is what tells them apart: a clock
 * reading the same as realtime is a clock that is not usertime, whatever
 * it is called.
 *
 * The interval is spent asleep, which is the only way a process reliably
 * passes real time without consuming execution -- a loop that tries to
 * idle still runs. The bounds are wide because the runner may be loaded:
 * what is asserted is that the sleep shows in full on one clock and
 * barely at all on the other, not the exact figures.
 *
 * The second half is the converse, and it is the half that fails if
 * usertime is made to answer a constant: work is done, and the clock
 * that counts execution has to have moved for it.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>

#ifdef _WIN32
# include <windows.h> /* Sleep */
#else
# include <time.h> /* nanosleep */
#endif

#include "xpost.h"
#include "xpost_compat.h"

#include "xpost_test.h"

/* how long the idle interval is, and how much of it each clock may
   show. The sleeping half of the process consumes no execution, so the
   allowance on usertime covers only what the two calls around it cost;
   the allowance on realtime is short of the sleep rather than over it,
   because a sleep may be granted early by the clock's own resolution. */
#define IDLE_MS      400
#define IDLE_REAL_MIN 300
#define IDLE_USER_MAX 150

/* how long the working interval is, measured in real time so that it is
   the same interval on a fast machine and a slow one, and how much
   execution it has to show. A loaded runner may give the process a
   fraction of that interval, so the floor is a small part of it. */
#define WORK_MS      400
#define WORK_USER_MIN 50

static void idle_for(long ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* Work the processor for an interval of real time. The accumulator is
   volatile so that the loop is not the compiler's to remove. */
static void work_for(long ms)
{
    static volatile unsigned long sink;
    long long end = xpost_get_realtime_ms() + ms;
    unsigned long n = 0;

    while (xpost_get_realtime_ms() < end)
    {
        int i;

        for (i = 0; i < 10000; i++)
            n += (unsigned long)i * 3u + 1u;
    }
    sink = n;
    (void)sink;
}

int main(void)
{
    long long r0, r1, r2, u0, u1, u2;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    r0 = xpost_get_realtime_ms();
    u0 = xpost_get_usertime_ms();

    idle_for(IDLE_MS);

    r1 = xpost_get_realtime_ms();
    u1 = xpost_get_usertime_ms();

    check(r1 - r0 >= IDLE_REAL_MIN,
          "real time passes while the interpreter executes nothing");
    check(u1 - u0 <= IDLE_USER_MAX,
          "execution time does not pass while the interpreter executes nothing");

    work_for(WORK_MS);

    r2 = xpost_get_realtime_ms();
    u2 = xpost_get_usertime_ms();

    check(r2 - r1 >= WORK_MS / 2, "real time passes while work is done");
    check(u2 - u1 >= WORK_USER_MIN, "execution time passes while work is done");

    /* neither clock runs backwards, and neither counts execution the
       process has not had */
    check(r2 >= r1 && r1 >= r0, "the real-time clock does not go backwards");
    check(u2 >= u1 && u1 >= u0, "the execution clock does not go backwards");
    check(u2 - u0 <= (r2 - r0) + IDLE_USER_MAX,
          "execution time does not exceed the real time it was spent in");

    xpost_quit();

    return verdict();
}
