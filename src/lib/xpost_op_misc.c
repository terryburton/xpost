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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h> /* NULL strtod */
#include <stddef.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h> /* time */

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#include "xpost.h"
#include "xpost_compat.h"
#include "xpost_main.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_error.h"
#include "xpost_file.h"  /* the sandbox denies environment access once engaged */
#include "xpost_name.h"
#include "xpost_string.h"
#include "xpost_array.h"
#include "xpost_dict.h"

//#include "xpost_interpreter.h"
#include "xpost_operator.h"
#include "xpost_op_dict.h"
#include "xpost_op_misc.h"

/* the procedures already walked in this bind, so a procedure that
   reaches itself -- directly or through others -- binds once and
   terminates: access flags ride on the reference, not the value, so
   marking the copy in hand cannot break the cycle */
typedef struct
{
    unsigned int *ents;
    int n, cap;
} Bind_Seen;

static
Xpost_Object bind(Xpost_Context *ctx,
                  Xpost_Object p,
                  Bind_Seen *seen)
{
    Xpost_Object t, d;
    unsigned int ent;
    int i, j, z;

    /* a plain read-only procedure -- one made read-only after creation
       rather than by the packing machinery -- is left exactly as it is:
       bind neither rewrites its names nor descends into it. bind does
       rewrite a packed array (it carries the packed flag), matching the
       reference implementations, which bind packed arrays but not
       ordinary read-only arrays. */
    if (!xpost_object_is_packed(p)
     && xpost_object_get_access(ctx, p) < XPOST_OBJECT_TAG_ACCESS_UNLIMITED)
        return p;

    ent = xpost_object_get_ent(p);
    for (i = 0; i < seen->n; i++)
        if (seen->ents[i] == ent)
            return xpost_object_set_access(ctx, p, XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
    if (seen->n == seen->cap)
    {
        int ncap = seen->cap ? seen->cap * 2 : 64;
        unsigned int *nents = realloc(seen->ents, (size_t)ncap * sizeof(*nents));

        if (!nents)
            return xpost_object_set_access(ctx, p, XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
        seen->ents = nents;
        seen->cap = ncap;
    }
    seen->ents[seen->n++] = ent;

    for (i = 0; i < p.comp_.sz; i++)
    {
        t = xpost_array_get(ctx, p, i);
        switch(xpost_object_get_type(t))
        {
            default: break;
            case nametype:
                if (!xpost_object_is_exe(t)) break; /* bind only replaces executable names */
                z = xpost_stack_count(ctx->lo, ctx->ds);
                for (j = 0; j < z; j++) {
                    d = xpost_stack_topdown_fetch(ctx->lo, ctx->ds, j);
                    t = xpost_dict_get_name(ctx, d, t);
                    if (xpost_object_get_type(t) != invalidtype) {
                        if (xpost_object_get_type(t) == operatortype) {
                            /* bind rewrites the procedure itself, which
                               is read-only once packed: the raw layer
                               writes without the program-facing access
                               check */
                            /* the index was just read from this same
                               array, so the store reaches it */
                            XPOST_REFUSAL_IMPOSSIBLE(
                                xpost_array_put_memory(
                                    xpost_context_select_memory(ctx, p),
                                    p, i, t));
                        }
                        break;
                    }
                    t = xpost_array_get(ctx, p, i); /* keep searching for the name */
                }
                break;
            case arraytype:
                /* descend into every executable sub-procedure; bind()
                   rewrites the packed and writable ones in place and
                   leaves a plain read-only one (an already-bound
                   procedure keeps its finished contents) untouched */
                if (xpost_object_is_exe(t))
                {
                    t = bind(ctx, t, seen);
                    /* as above: i indexes the array being walked */
                    XPOST_REFUSAL_IMPOSSIBLE(
                        xpost_array_put_memory(
                            xpost_context_select_memory(ctx, p), p, i, t));
                }
        }
    }
    return xpost_object_set_access(ctx, p, XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
}

/* proc  bind  proc
   replace names with operators in proc and make read-only */
static
int Pbind(Xpost_Context *ctx,
          Xpost_Object P)
{
    Bind_Seen seen;

    seen.ents = NULL;
    seen.n = seen.cap = 0;
    xpost_stack_push(ctx->lo, ctx->os, bind(ctx, P, &seen));
    free(seen.ents);
    return 0;
}

/* -  realtime  int
   return real time in milliseconds */
static
int realtime(Xpost_Context *ctx)
{
    long long ms;

    ms = xpost_get_realtime_ms();
    ms &= 0x00000000ffffffff; /* truncate any large value */
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons((int)ms)))
        return stackoverflow;

    return 0;
}

/* -  usertime  int
   return execution time in milliseconds */
static
int usertime(Xpost_Context *ctx)
{
    long long ms;

    ms = xpost_get_usertime_ms();
    ms &= 0x00000000ffffffff; /* truncate any large value */
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons((int)ms)))
        return stackoverflow;
    return 0;
}

/* string  getenv  string
   return value for environment variable */
static
int Sgetenv(Xpost_Context *ctx,
            Xpost_Object S)
{
    char *str;
    char *r;
    if (xpost_path_control_is_engaged())
        return invalidaccess;
    str = xpost_string_allocate_cstring(ctx, S);
    r = getenv(str);
    if (r)
    {
        Xpost_Object strobj;
        strobj = xpost_string_cons(ctx, strlen(r), r);
        if (xpost_object_get_type(strobj) == nulltype){
	    free(str);
            return VMerror;
	}
        xpost_stack_push(ctx->lo, ctx->os, strobj);
    }
    else
    {
        free(str);
        return undefined;
    }
    free(str);
    return 0;
}

/* string string  putenv
   set value for environment variable */
static
int SSputenv(Xpost_Context *ctx,
             Xpost_Object N,
             Xpost_Object S)
{
    char *n;
    char *v;

    if (xpost_path_control_is_engaged())
        return invalidaccess;
    n = xpost_string_allocate_cstring(ctx, N);
    if (!n)
        return VMerror;
    v = xpost_string_allocate_cstring(ctx, S);
    if (!v)
    {
        free(n);
        return VMerror;
    }

#ifdef _WIN32
    /* the runtime copies the string it is given, so the joined one is
       this function's to release */
    {
        size_t len = strlen(n) + 1 + strlen(v) + 1;
        char *joined = malloc(len);

        if (!joined)
        {
            free(n);
            free(v);
            return VMerror;
        }
        memcpy(joined, n, strlen(n));
        joined[strlen(n)] = '=';
        memcpy(joined + strlen(n) + 1, v, strlen(v) + 1);
        putenv(joined);
        free(joined);
    }
#else
    /* putenv would take the pointer rather than a copy of it, and the
       environment would be left holding memory released on the way out
       of here; setenv copies what it is given */
    if (setenv(n, v, 1) != 0)
    {
        free(n);
        free(v);
        return VMerror;
    }
#endif

    free(n);
    free(v);
    return 0;
}

static
int _array_swap(Xpost_Context *ctx,
                Xpost_Object a,
                Xpost_Object i,
                Xpost_Object j)
{
    Xpost_Object a_i, a_j;
    int ret;

    a_i = xpost_array_get(ctx, a, i.int_.val);
    a_j = xpost_array_get(ctx, a, j.int_.val);
    ret = xpost_array_put(ctx, a, i.int_.val, a_j);
    if (ret)
        return ret;
    ret = xpost_array_put(ctx, a, j.int_.val, a_i);
    if (ret)
        return ret;
    return 0;
}

#if 0
static
int traceon (Xpost_Context *ctx)
{
    (void)ctx;
    _xpost_interpreter_is_tracing = 1;
    return 0;
}
static
int traceoff(Xpost_Context *ctx)
{
    (void)ctx;
    _xpost_interpreter_is_tracing = 0;
    return 0;
}
#endif

static
int debugloadon(Xpost_Context *ctx)
{
    (void)ctx;
    DEBUGLOAD = 1;
    return 0;
}
static
int debugloadoff(Xpost_Context *ctx)
{
    (void)ctx;
    DEBUGLOAD = 0;
    return 0;
}

static
int Odumpnames(Xpost_Context *ctx)
{
    unsigned int names;
    printf("\nGlobal Name stack: ");
    names = xpost_memory_name_stack_adr(ctx->gl);
    xpost_stack_dump(ctx->gl, names);
    (void)puts("");
    printf("\nLocal Name stack: ");
    names = xpost_memory_name_stack_adr(ctx->lo);
    xpost_stack_dump(ctx->lo, names);
    (void)puts("");
    return 0;
}

/*
FIXME: interaction with file dump mechanism ?
*/
static
int dumpvm(Xpost_Context *ctx)
{
    xpost_memory_file_dump(ctx->lo);
    xpost_memory_table_dump(ctx->lo);
    xpost_memory_file_dump(ctx->gl);
    xpost_memory_table_dump(ctx->gl);
    return 0;
}

static
int returntocaller(Xpost_Context *ctx)
{
    (void)ctx;
    return yieldtocaller;
}

/* -  .sysdictunlock  -
   Make systemdict writeable so the graphics language can define into it. This
   is a one-shot: once the language is loaded (.sysdictrelock has run), it does
   nothing, so a program that reaches the name cannot reopen systemdict. The
   window it opens runs only the interpreter's own graphics files, before any
   program, and the error handler relocks systemdict if a load faults. */
static
int op_sysdictunlock(Xpost_Context *ctx)
{
    Xpost_Object sd;
    if (ctx->sysdict_load_done)
        return 0;
    sd = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);
    xpost_object_set_access(ctx, sd, XPOST_OBJECT_TAG_ACCESS_UNLIMITED);
    ctx->sysdict_unlocked = 1;
    return 0;
}

/* -  .sysdictrelock  -
   Restore systemdict to read-only after the graphics language has loaded, and
   spend the one-shot. */
static
int op_sysdictrelock(Xpost_Context *ctx)
{
    Xpost_Object sd = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);
    xpost_object_set_access(ctx, sd, XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
    ctx->sysdict_unlocked = 0;
    ctx->sysdict_load_done = 1;
    return 0;
}

/* dict  .setprivatedict  -
   Record the interpreter's private local machinery dictionary in the context,
   where the collector roots it and the C reaches it, without it ever going on
   the dict stack. Called once from init.ps. */
static
int op_setprivatedict(Xpost_Context *ctx,
                      Xpost_Object D)
{
    ctx->privatedict = D;
    return 0;
}

/* -  .privatedict  dict
   Push the private local machinery dictionary. Like .gscratch, it hands a local
   object to whatever asks; a global procedure may use the result transiently
   without holding a local reference. */
static
int op_privatedict(Xpost_Context *ctx)
{
    xpost_stack_push(ctx->lo, ctx->os, ctx->privatedict);
    return 0;
}

int xpost_oper_init_misc_ops(Xpost_Context *ctx,
                             Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;

    const char *productstr = "Xpost";
    const char *versionstr = "0.0";
    int revno = 1;
    int serno = 0;

    assert(ctx->gl->base);

    op = xpost_operator_cons(ctx, "bind", (Xpost_Op_Func)Pbind, 1, 1, proctype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".sysdictunlock", (Xpost_Op_Func)op_sysdictunlock, 0, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, ".setprivatedict", (Xpost_Op_Func)op_setprivatedict, 0, 1, dicttype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".privatedict", (Xpost_Op_Func)op_privatedict, 1, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, ".sysdictrelock", (Xpost_Op_Func)op_sysdictrelock, 0, 0);
    INSTALL;
    if (xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "null"), null))
        return VMerror;
    if (xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "version"),
                       xpost_object_cvlit(xpost_string_cons(ctx,
                               strlen(versionstr), versionstr))))
        return VMerror;
    op = xpost_operator_cons(ctx, "realtime", (Xpost_Op_Func)realtime, 1, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "usertime", (Xpost_Op_Func)usertime, 1, 0);
    INSTALL;
    //languagelevel
    if (xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "product"),
                       xpost_object_cvlit(xpost_string_cons(ctx,
                               strlen(productstr), productstr))))
        return VMerror;
    if (xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "revision"),
                       xpost_int_cons(revno)))
        return VMerror;
    if (xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "serialnumber"),
                       xpost_int_cons(serno)))
        return VMerror;
    //executive: see init.ps
    //echo: see opf.c
    //prompt: see init.ps

    op = xpost_operator_cons(ctx, "getenv", (Xpost_Op_Func)Sgetenv, 1, 1, stringtype);
    INSTALL;
    op = xpost_operator_cons(ctx, "putenv", (Xpost_Op_Func)SSputenv, 0, 2, stringtype, stringtype);
    INSTALL;

    op = xpost_operator_cons(ctx, ".swap", (Xpost_Op_Func)_array_swap, 0, 3,
                             arraytype, integertype, integertype);
    INSTALL;

#if 0
    op = xpost_operator_cons(ctx, "traceon", (Xpost_Op_Func)traceon, 0, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "traceoff", (Xpost_Op_Func)traceoff, 0, 0);
    INSTALL;
#endif
    op = xpost_operator_cons(ctx, "debugloadon", (Xpost_Op_Func)debugloadon, 0, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "debugloadoff", (Xpost_Op_Func)debugloadoff, 0, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "dumpnames", (Xpost_Op_Func)Odumpnames, 0, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "dumpvm", (Xpost_Op_Func)dumpvm, 0, 0);
    INSTALL;

    /* xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
    xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "mark"), mark); */

    op = xpost_operator_cons(ctx, "returntocaller", (Xpost_Op_Func)returntocaller, 0, 0);
    INSTALL;

    return 0;
}
