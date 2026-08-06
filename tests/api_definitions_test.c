/*
 * Embedding-contract test: the definitions an embedder supplies, and the
 * paths the library reports.
 *
 * xpost_add_definitions takes argv-style "key=value" strings and defines
 * each in userdict, where the program reads them by name. The value is
 * scanned as a PostScript token, so what the program finds is an object
 * of the type the text spells, not a string of it. A key given without a
 * value is defined as null.
 */

#include <stdio.h>
#include <string.h>
#include "xpost.h"

#include "xpost_test.h"

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

/* run a program and answer what it printed */
static const char *ran(Xpost_Context *ctx, const char *prog)
{
    out_len = 0;
    (void)xpost_run(ctx, XPOST_INPUT_STRING, prog, 0);
    out_buf[out_len] = '\0';
    return out_buf;
}

int main(void)
{
    /* the strings are not const: the library splits each at its '=' and
       puts the byte back afterwards */
    static char d_int[]  = "answer=42";
    static char d_real[] = "ratio=2.5";
    static char d_str[]  = "greeting=(hello)";
    static char d_name[] = "which=/second";
    static char d_bare[] = "bare";
    char *defs[5];

    Xpost_Context *ctx;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    /* the library reports where it and its data live */
    check(xpost_lib_dir_get() != NULL, "the library reports its own directory");
    check(xpost_data_dir_get() != NULL, "the library reports its data directory");

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return verdict();
    }
    xpost_job_snapshots_set(ctx, 0);
    xpost_stdout_handler_set(ctx, out_sink, NULL);

    defs[0] = d_int; defs[1] = d_real; defs[2] = d_str;
    defs[3] = d_name; defs[4] = d_bare;
    check(xpost_add_definitions(ctx, 5, defs) != 0,
          "the definitions are accepted");

    /* each value is an object of the type its text spells */
    check(strcmp(ran(ctx, "answer type /integertype eq { (y) print } if flush"), "y") == 0,
          "a whole number is defined as an integer");
    check(strcmp(ran(ctx, "answer 42 eq { (y) print } if flush"), "y") == 0,
          "the integer has the value it was given");
    check(strcmp(ran(ctx, "ratio type /realtype eq { (y) print } if flush"), "y") == 0,
          "a fractional number is defined as a real");
    check(strcmp(ran(ctx, "greeting (hello) eq { (y) print } if flush"), "y") == 0,
          "a parenthesised value is defined as the string it spells");
    check(strcmp(ran(ctx, "which /second eq { (y) print } if flush"), "y") == 0,
          "a slashed value is defined as the name it spells");

    /* a key with no value is defined, and defined as null */
    check(strcmp(ran(ctx, "userdict /bare known { (y) print } if flush"), "y") == 0,
          "a key given without a value is still defined");
    check(strcmp(ran(ctx, "userdict /bare get null eq { (y) print } if flush"), "y") == 0,
          "a key given without a value is defined as null");

    /* the strings the caller passed are its own again */
    check(strcmp(d_int, "answer=42") == 0,
          "the caller's string is left as it was found");

    xpost_stdout_handler_set(ctx, NULL, NULL);
    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
