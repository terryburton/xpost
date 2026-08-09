/*
 * A name longer than a string object can count.
 *
 * The interpreter carries a file name to the operator that opens it as a
 * string object, and a string object counts its own length in a field
 * narrower than a path may be -- sixteen bits unless the interpreter was
 * built for large objects. A name the field cannot count was recorded as
 * the length it wrapped to, and it was the shortened name that was
 * opened: the caller asked for one file and the interpreter opened
 * another, chosen by whoever supplied the name.
 *
 * The boundary is stated as the field's own edge rather than as some
 * large number, so the test says what it is about: the last length the
 * field can count is accepted, the first it cannot is refused.
 *
 * This drives the embedding call rather than the command line. A name of
 * this length cannot be passed as an argument everywhere -- Windows caps
 * a command line at 32767 characters -- and the defect is in what the
 * interpreter does with the name, not in how the name reached it.
 *
 * The edge is only reached on a build whose field is narrow enough for a
 * name of that length to be made. On the large-object build the field
 * counts to four thousand million, and a name long enough to pass it
 * would have to be four gigabytes: the boundary is left unexercised
 * there and said to be, rather than approximated with a shorter name
 * that proves nothing about the edge. The rest of the test runs on both.
 */

/* the width of the object fields is a build option recorded in config.h,
   so this has to be read before the header that acts on it -- without it
   the test would measure the narrow field against a library built for
   the wide one */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpost.h"
#include "xpost_object.h" /* the width of the field under test */

#include "xpost_test.h"

/* the last length the length field can count, and the first it cannot */
#define FITS    ((size_t)XPOST_OBJECT_COMP_MAX_SZ)
#define OVERRUN ((size_t)XPOST_OBJECT_COMP_MAX_SZ + 1)

/* Whether a name that passes the field is one this test will build. The
   narrow field is sixteen bits and a name past it is 64 kilobytes; the
   large-object field is thirty-two and a name past it is four
   gigabytes, which is not a thing to allocate to make a point about a
   boundary. The two are told apart before compiling rather than after,
   so the arm that does not apply is not built at all. */

/* A name of exactly len characters whose leading characters are the path
   of a file that exists. Under the defect the interpreter opened that
   file; the name as given names nothing.

   Only the narrow build asks for one, so on the other build this is not
   compiled rather than compiled and left uncalled. */
#ifndef WANT_LARGE_OBJECT
static char *decoy_name(const char *decoy, size_t len)
{
    size_t n = strlen(decoy);
    char *s;

    if (len <= n)
        return NULL;
    s = malloc(len + 1);
    if (!s)
        return NULL;
    memcpy(s, decoy, n);
    memset(s + n, 'x', len - n);
    s[len] = '\0';
    return s;
}
#endif

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Run_Status st;
    const char *decoy = "xpost_name_length_decoy.ps";
#ifndef WANT_LARGE_OBJECT
    char *name;
#endif
    FILE *fp;

    /* the file the shortened name would reach. It writes nothing and
       quits, so a run that reaches it still reports completion -- which
       is the outcome under test, since the defect is that a run
       completes at all when the name it was given names nothing. */
    fp = fopen(decoy, "w");
    if (!fp)
    {
        report_failure("cannot write the decoy file");
        return verdict();
    }
    fputs("quit\n", fp);
    fclose(fp);

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
        return verdict();
    }
    xpost_job_snapshots_set(ctx, 0);

#ifndef WANT_LARGE_OBJECT
    {
        /* a name one character past what the field counts */
        name = decoy_name(decoy, OVERRUN);
        if (!name)
        {
            report_failure("cannot build the over-long name");
            xpost_destroy(ctx);
            return verdict();
        }
        st = xpost_run(ctx, XPOST_INPUT_FILENAME, name, 0);
        check(st == XPOST_RUN_FAILED,
              "a name longer than a string can count is refused");
        free(name);

        /* the same name at the last length the field can count is not
           refused for its length: it names no file, so the run reports
           the program's error rather than the caller's */
        name = decoy_name(decoy, FITS);
        if (name)
        {
            st = xpost_run(ctx, XPOST_INPUT_FILENAME, name, 0);
            check(st != XPOST_RUN_FAILED,
                  "a name the field can count is not refused for its length");
            free(name);
        }
    }
#else
    printf("# the length field counts to %llu here: a name that passes"
           " it is larger than this test will build\n",
           (unsigned long long)FITS);
#endif

    /* the name of a file that does exist still runs */
    st = xpost_run(ctx, XPOST_INPUT_FILENAME, decoy, 0);
    check(st == XPOST_RUN_COMPLETE, "an ordinary name still runs");

    xpost_destroy(ctx);
    xpost_quit();
    remove(decoy);

    return verdict();
}
