/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Xpost software product nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* The editing streams -- %lineedit and %statementedit -- pinch a temporary
   file off the standard input and hand the program a file over it. A request
   that cannot be satisfied must leave nothing behind: the temporary file is
   opened before the text is known to be acceptable, so a refusal has one open
   already and is the only thing that can close it.
 *
   %statementedit refuses text nested deeper than it will follow. Ask it
   repeatedly for such text and count the descriptors: a stream left open by a
   refusal takes the lowest free descriptor with it, so the number a fresh
   temporary file is given is what says whether anything was kept. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_context.h"
#include "xpost_string.h"
#include "xpost_file.h"
#include "xpost_error.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (cond)
    {
        printf("ok: %s\n", what);
    }
    else
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* the lowest descriptor the system has free, which a temporary file is
   given because open() answers with the lowest available one */
static int lowest_free_fd(void)
{
    FILE *probe;
    int fd;

    probe = tmpfile();
    if (!probe)
        return -1;
    fd = fileno(probe);
    fclose(probe);
    return fd;
}

enum { NREQUESTS = 24 };

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Memory_File *mem;
    Xpost_Object s;
    Xpost_Object f;
    char name[] = "%statementedit";
    char mode[] = "r";
    char *deep;
    FILE *in;
    char inpath[] = "edit_stream_close_test.in";
    int before, after;
    int i;
    int refused = 0;

    /* text nested deeper than the reader will follow, enough of it that
       every request meets the limit rather than the end of the input */
    deep = (char *)malloc(NREQUESTS * 64 + 1);
    if (!deep)
    {
        printf("FAIL: cannot build the input\n");
        return 1;
    }
    memset(deep, '{', NREQUESTS * 64);
    deep[NREQUESTS * 64] = '\0';

    in = fopen(inpath, "w");
    if (!in || fputs(deep, in) == EOF)
    {
        printf("FAIL: cannot write the input\n");
        return 1;
    }
    fclose(in);
    free(deep);

    if (!freopen(inpath, "r", stdin))
    {
        printf("FAIL: cannot read the input as standard input\n");
        remove(inpath);
        return 1;
    }

    if (!xpost_init())
    {
        printf("FAIL: xpost_init\n");
        remove(inpath);
        return 1;
    }
    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_RETURN, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        printf("FAIL: xpost_create\n");
        remove(inpath);
        return 1;
    }

    s = xpost_string_cons(ctx, 1, "x");
    mem = xpost_context_select_memory(ctx, s);

    before = lowest_free_fd();
    check(before >= 0, "a temporary file can be opened at all");

    for (i = 0; i < NREQUESTS; i++)
    {
        int ret = xpost_file_open(mem, name, mode, &f);
        if (ret)
            refused++;
    }
    check(refused == NREQUESTS,
          "every request for text nested too deep is refused");

    after = lowest_free_fd();
    check(after == before,
          "a refused editing stream leaves no descriptor open");

    xpost_destroy(ctx);
    xpost_quit();
    remove(inpath);

    if (failures)
    {
        printf("FAILURES: %d\n", failures);
        return 1;
    }
    printf("SUCCESS\n");
    return 0;
}
