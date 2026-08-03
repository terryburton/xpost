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

#include <stdlib.h> /* NULL */
#include <stddef.h>

#include <assert.h>
#include <stdio.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_error.h"
#include "xpost_name.h"
#include "xpost_dict.h"

//#include "xpost_interpreter.h"
#include "xpost_operator.h"
#include "xpost_op_stack.h"


/* any  pop  -
   discard top element */
static
int Apop(Xpost_Context *ctx,
         Xpost_Object x)
{
    (void)ctx;
    (void)x;
    return 0;
}

/* any1 any2  exch  any2 any1
   exchange top two elements */
static
int AAexch(Xpost_Context *ctx,
           Xpost_Object x,
           Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os, y);
    xpost_stack_push(ctx->lo, ctx->os, x);
    return 0;
}

/* any  dup  any any
   duplicate top element */
static
int Adup(Xpost_Context *ctx,
         Xpost_Object x)
{
    xpost_stack_push(ctx->lo, ctx->os, x);
    if (!xpost_stack_push(ctx->lo, ctx->os, x))
        return stackoverflow;
    return 0;
}

/* any1..anyN N  copy  any1..anyN any1..anyN
   duplicate top n elements */
static
int Icopy(Xpost_Context *ctx,
          Xpost_Object n)
{
    int nn = n.int_.val;
    Xpost_Object *src;
    int i;

    if (nn < 0)
        return rangecheck;
    if (nn > xpost_stack_count(ctx->lo, ctx->os))
        return stackunderflow;
    if (nn == 0)
        return 0;

    /* Snapshot the top n operands in one pass, then push the copies. The
       former loop re-fetched a fixed deep index (n-1) on every push, and each
       fetch walked O(n) stack segments, so copy was O(n^2) on a deep stack.
       Snapshot before pushing because a push may grow and move the memory
       file, invalidating any cached base pointer. src[0] is the topmost, so
       pushing src[n-1]..src[0] restores the operands' order above the copy. */
    src = malloc((size_t)nn * sizeof(Xpost_Object));
    if (!src)
        return VMerror;
    xpost_stack_peek_top(ctx->lo, ctx->os, nn, src);
    for (i = nn - 1; i >= 0; i--)
    {
        if (!xpost_stack_push(ctx->lo, ctx->os, src[i]))
        {
            free(src);
            return stackoverflow;
        }
    }
    free(src);
    return 0;
}

/* anyN..any0 N  index  anyN..any0 anyN
   duplicate arbitrary element */
static
int Iindex(Xpost_Context *ctx,
           Xpost_Object n)
{
    int ret = xpost_op_index_check(n.int_.val,
                                   xpost_stack_count(ctx->lo, ctx->os));
    if (ret)
        return ret;
    //printf("index %d\n", n.int_.val);
    if (!xpost_stack_push(ctx->lo, ctx->os,
                          xpost_stack_topdown_fetch(ctx->lo, ctx->os, n.int_.val)))
        return stackoverflow;
    return 0;
}

/* a(n-1)..a(0) n j  roll  a((j-1)mod n)..a(0) a(n-1)..a(j mod n)
   roll n elements j times */
static
int IIroll(Xpost_Context *ctx,
           Xpost_Object N,
           Xpost_Object J)
{
    Xpost_Object *src;
    unsigned char *base;
    Xpost_Stack *root, *top, *seg;
    int n = N.int_.val;
    integer j;
    int got;
    if (n < 0)
        return rangecheck;
    if (n == 0) return 0;
    if (n > xpost_stack_count(ctx->lo, ctx->os))
        return stackunderflow;
    j = xpost_op_roll_shift(n, J.int_.val);
    if (j == 0) return 0;

    /* roll touches each of the top n operands a constant number of times,
       so it is inherently O(n). The former per-element topdown_fetch /
       topdown_replace each walked O(index) stack segments, making a roll of
       a deep stack O(n^2). Snapshot the top n operands in a single top-down
       pass (src[0] is the topmost), then write them back rotated in one more
       pass, each position taking the operand the shared rule names.
       Neither pass allocates VM, so the cached base/segment pointers stay
       valid throughout. */
    src = malloc((size_t)n * sizeof(Xpost_Object));
    if (!src)
        return VMerror;

    xpost_stack_peek_top(ctx->lo, ctx->os, n, src);

    base = ctx->lo->base;
    root = (Xpost_Stack *)(base + ctx->os);
    top = (Xpost_Stack *)(base + root->prevseg);

    seg = top; got = 0;
    while (got < n)
    {
        int t = (int)seg->top;
        int put = (n - got < t) ? (n - got) : t;
        int m;
        for (m = 0; m < put; m++)
            seg->data[t - 1 - m] = src[xpost_op_roll_source(got + m, n, j)];
        got += put;
        if (got < n)
            seg = (Xpost_Stack *)(base + seg->prevseg);
    }

    free(src);
    return 0;
}

/* |- any1..anyN  clear  |-
   discard all elements */
static
int Zclear(Xpost_Context *ctx)
{
    //Xpost_Stack *s = (void *)(ctx->lo->base + ctx->os);
    //s->top = 0;
    xpost_stack_clear(ctx->lo, ctx->os);
#if 0
    if (s->nextseg) /* trim the stack */
    {
        xpost_stack_free(ctx->lo, s->nextseg);
        s->nextseg = 0;
    }
#endif
    return 0;
}

/* |- any1..anyN  count  |- any1..anyN N
   count elements on stack */
static
int Zcount(Xpost_Context *ctx)
{
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(xpost_stack_count(ctx->lo, ctx->os))))
        return stackoverflow;
    return 0;
}

/* -  mark  mark
   push mark on stack */
/* the name "mark" is defined in systemdict as a marktype object */

/* mark obj1..objN  cleartomark  -
   discard elements down through mark */
int xpost_op_cleartomark(Xpost_Context *ctx)
{
    Xpost_Object o;
    do {
        o = xpost_stack_pop(ctx->lo, ctx->os);
        if (xpost_object_get_type(o) == invalidtype)
            return unmatchedmark;
    } while (o.tag != marktype);
    return 0;
}

/* mark obj1..objN  counttomark  N
   count elements down to mark */
int xpost_op_counttomark(Xpost_Context *ctx)
{
    /* One top-down pass to the mark. The former loop fetched each operand with
       xpost_stack_topdown_fetch per index, and each of those re-walks the
       segment chain, so counttomark was O(depth^2). counttomark also underlies
       the [ ] and << >> constructors (via xpost_op_array_to_mark /
       xpost_op_dict_to_mark), so a large array or dictionary literal inherited
       that quadratic cost. */
    int i = xpost_stack_topdown_find_type(ctx->lo, ctx->os, marktype, NULL);
    if (i < 0)
        return unmatchedmark;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(i)))
        return stackoverflow;
    return 0;
}

int xpost_oper_init_stack_ops(Xpost_Context *ctx,
                              Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;
    unsigned int optadr;

    assert(ctx->gl->base);
    //xpost_memory_table_get_addr(ctx->gl, XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE, &optadr);
    //optab = (void *)(ctx->gl->base + optadr);
    op = xpost_operator_cons(ctx, "pop", (Xpost_Op_Func)Apop, 0, 1, anytype);
    ctx->opcode_shortcuts.oppop = op.mark_.padw;
    INSTALL;
    op = xpost_operator_cons(ctx, "exch", (Xpost_Op_Func)AAexch, 2, 2, anytype, anytype);
    ctx->opcode_shortcuts.opexch = op.mark_.padw;
    INSTALL;
    op = xpost_operator_cons(ctx, "dup", (Xpost_Op_Func)Adup, 2, 1, anytype);
    ctx->opcode_shortcuts.opdup = op.mark_.padw;
    INSTALL;
    op = xpost_operator_cons(ctx, "copy", (Xpost_Op_Func)Icopy, 0, 1, integertype);
    INSTALL;
    op = xpost_operator_cons(ctx, "index", (Xpost_Op_Func)Iindex, 1, 1, integertype);
    ctx->opcode_shortcuts.opindex = op.mark_.padw;
    INSTALL;
    //xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
    op = xpost_operator_cons(ctx, "roll", (Xpost_Op_Func)IIroll, 0, 2, integertype, integertype);
    INSTALL;
    ctx->opcode_shortcuts.oproll = op.mark_.padw;
    op = xpost_operator_cons(ctx, "clear", (Xpost_Op_Func)Zclear, 0, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "count", (Xpost_Op_Func)Zcount, 1, 0);
    INSTALL;
    xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "mark"), mark);
    op = xpost_operator_cons(ctx, "cleartomark", (Xpost_Op_Func)xpost_op_cleartomark, 0, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "counttomark", (Xpost_Op_Func)xpost_op_counttomark, 1, 0);
    INSTALL;
    return 0;
}
