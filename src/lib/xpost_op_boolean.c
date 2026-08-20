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

/* relational, boolean, and bitwise operators */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_name.h"
#include "xpost_dict.h"
#include "xpost_error.h"

//#include "xpost_interpreter.h"
#include "xpost_operator.h"
#include "xpost_op_boolean.h"

/* any1 any2  eq  bool
   test equal */
static
int xpost_op_any_any_eq (Xpost_Context *ctx,
                         Xpost_Object x,
                         Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(xpost_op_relation(XPOST_OP_REL_EQ,
                         xpost_dict_compare_objects(ctx, x, y))));
    return 0;
}

/* any1 any2  ne  bool
   test not equal */
static
int xpost_op_any_any_ne (Xpost_Context *ctx,
                         Xpost_Object x,
                         Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(xpost_op_relation(XPOST_OP_REL_NE,
                         xpost_dict_compare_objects(ctx, x, y))));
    return 0;
}

/* any1 any2  ge  bool
   test greater or equal */
static
int xpost_op_any_any_ge (Xpost_Context *ctx,
                         Xpost_Object x,
                         Xpost_Object y)
{
    if (!xpost_op_ordered_comparable(x, y))
        return typecheck;
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(xpost_op_relation(XPOST_OP_REL_GE,
                         xpost_dict_compare_objects(ctx, x, y))));
    return 0;
}

/* any1 any2  gt  bool
   test greater than */
static
int xpost_op_any_any_gt (Xpost_Context *ctx,
                         Xpost_Object x,
                         Xpost_Object y)
{
    if (!xpost_op_ordered_comparable(x, y))
        return typecheck;
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(xpost_op_relation(XPOST_OP_REL_GT,
                         xpost_dict_compare_objects(ctx, x, y))));
    return 0;
}

/* any1 any2  le  bool
   test less or equal */
static
int xpost_op_any_any_le (Xpost_Context *ctx,
                         Xpost_Object x,
                         Xpost_Object y)
{
    if (!xpost_op_ordered_comparable(x, y))
        return typecheck;
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(xpost_op_relation(XPOST_OP_REL_LE,
                         xpost_dict_compare_objects(ctx, x, y))));
    return 0;
}

/* any1 any2  lt  bool
   test less than */
static
int xpost_op_any_any_lt (Xpost_Context *ctx,
                         Xpost_Object x,
                         Xpost_Object y)
{
    if (!xpost_op_ordered_comparable(x, y))
        return typecheck;
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(xpost_op_relation(XPOST_OP_REL_LT,
                         xpost_dict_compare_objects(ctx, x, y))));
    return 0;
}

/* bool1|int1 bool2|int2  and  bool3|int3
   logical|bitwise and */
static
int xpost_op_bool_bool_and (Xpost_Context *ctx,
                            Xpost_Object x,
                            Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(x.int_.val & y.int_.val));
    return 0;
}

static
int xpost_op_int_int_and (Xpost_Context *ctx,
                          Xpost_Object x,
                          Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons(x.int_.val & y.int_.val));
    return 0;
}

/* bool1|int1  not  bool2|int2
   logical|bitwise not */
static
int xpost_op_bool_not (Xpost_Context *ctx,
                       Xpost_Object x)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons( ! x.int_.val ));
    return 0;
}

static
int xpost_op_int_not (Xpost_Context *ctx,
                      Xpost_Object x)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons( ~ x.int_.val ));
    return 0;
}

/* bool1|int1 bool2|int2  or  bool3|int3
   logical|bitwise inclusive or */
static
int xpost_op_bool_bool_or (Xpost_Context *ctx,
                           Xpost_Object x,
                           Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(x.int_.val | y.int_.val));
    return 0;
}

static
int xpost_op_int_int_or (Xpost_Context *ctx,
                         Xpost_Object x,
                         Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons(x.int_.val | y.int_.val));
    return 0;
}

/* bool1|int1 bool2|int2  xor  bool3|int3
   exclusive or */
static
int xpost_op_bool_bool_xor (Xpost_Context *ctx,
                            Xpost_Object x,
                            Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_bool_cons(x.int_.val ^ y.int_.val));
    return 0;
}

static
int xpost_op_int_int_xor (Xpost_Context *ctx,
                          Xpost_Object x,
                          Xpost_Object y)
{
    xpost_stack_push(ctx->lo, ctx->os,
                     xpost_int_cons(x.int_.val ^ y.int_.val));
    return 0;
}

/* true */
/* false */
/* defined as the booleantype object directly */


/* The shift below moves a bit pattern within the integer's own width, so
   the unsigned type carrying it has to be exactly that wide -- a wider
   one lets a right shift bring down bits the integer does not have, and
   a narrower one drops bits it does. dword is that type in both object
   widths; this says so rather than leaving it to hold by luck. (A
   negative array size rather than _Static_assert: this builds as C99
   with -pedantic-errors, which rejects the latter.) */
typedef char xpost_bitshift_field_is_the_integer_width[
    sizeof(dword) == sizeof(integer) ? 1 : -1];

/* int1 shift  bitshift  int2
   bitwise shift of int1 (positive is left) */
static
int xpost_op_int_int_bitshift (Xpost_Context *ctx,
                               Xpost_Object x,
                               Xpost_Object y)
{
    /* PLRM 8.2: bitshift "shifts the binary representation of int1",
       bits shifted out are lost and bits shifted in are 0. What moves is
       therefore the operand's bit pattern and not its value, which
       decides the three cases the value-shaped reading leaves open: a
       negative int1 shifts as its two's-complement pattern (PLRM says
       only that the result is then not arithmetically meaningful, not
       that it is disallowed), a right shift fills with 0 rather than
       with the sign, and a count that reaches the width has carried
       every bit out and leaves 0.

       The pattern is held unsigned for the shift, so none of it is C's
       undefined shifting: a left shift of a negative value, and a count
       at or past the type's width, are both undefined on a signed
       operand. The width is the integer's, so the answer follows the
       object width and nothing else. */
    const integer width = (integer)(sizeof(integer) * 8);
    dword bits = (dword)x.int_.val;
    integer count = y.int_.val;
    dword res;

    if (count >= 0)
        res = (count >= width) ? 0 : (dword)(bits << count);
    else
        /* the count is negated only once it is known to lie inside the
           width, so the most negative count -- which has no positive
           counterpart -- is answered by the zero above it */
        res = (count <= -width) ? 0 : (dword)(bits >> -count);

    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons((integer)res));
    return 0;
}

int xpost_oper_init_bool_ops(Xpost_Context *ctx,
                             Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;
    int ret;

    assert(ctx->gl->base);

    op = xpost_operator_cons(ctx, "eq", (Xpost_Op_Func)xpost_op_any_any_eq, 2, anytype, anytype);
    INSTALL;
    op = xpost_operator_cons(ctx, "ne", (Xpost_Op_Func)xpost_op_any_any_ne, 2, anytype, anytype);
    INSTALL;
    op = xpost_operator_cons(ctx, "ge", (Xpost_Op_Func)xpost_op_any_any_ge, 2, anytype, anytype);
    INSTALL;
    op = xpost_operator_cons(ctx, "gt", (Xpost_Op_Func)xpost_op_any_any_gt, 2, anytype, anytype);
    INSTALL;
    op = xpost_operator_cons(ctx, "le", (Xpost_Op_Func)xpost_op_any_any_le, 2, anytype, anytype);
    INSTALL;
    op = xpost_operator_cons(ctx, "lt", (Xpost_Op_Func)xpost_op_any_any_lt, 2, anytype, anytype);
    INSTALL;
    op = xpost_operator_cons(ctx, "and", (Xpost_Op_Func)xpost_op_bool_bool_and, 2, booleantype, booleantype);
    INSTALL;
    op = xpost_operator_cons(ctx, "and", (Xpost_Op_Func)xpost_op_int_int_and, 2, integertype, integertype);
    INSTALL;
    op = xpost_operator_cons(ctx, "not", (Xpost_Op_Func)xpost_op_bool_not, 1, booleantype);
    INSTALL;
    op = xpost_operator_cons(ctx, "not", (Xpost_Op_Func)xpost_op_int_not, 1, integertype);
    INSTALL;
    op = xpost_operator_cons(ctx, "or", (Xpost_Op_Func)xpost_op_bool_bool_or, 2, booleantype, booleantype);
    INSTALL;
    op = xpost_operator_cons(ctx, "or", (Xpost_Op_Func)xpost_op_int_int_or, 2, integertype, integertype);
    INSTALL;
    op = xpost_operator_cons(ctx, "xor", (Xpost_Op_Func)xpost_op_bool_bool_xor, 2, booleantype, booleantype);
    INSTALL;
    op = xpost_operator_cons(ctx, "xor", (Xpost_Op_Func)xpost_op_int_int_xor, 2, integertype, integertype);
    INSTALL;
    ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "true"), xpost_bool_cons(1));
    if (ret)
        return ret;
    ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "false"), xpost_bool_cons(0));
    if (ret)
        return ret;
    op = xpost_operator_cons(ctx, "bitshift", (Xpost_Op_Func)xpost_op_int_int_bitshift, 2, integertype, integertype);
    INSTALL;

    /* xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL); */

    return 0;
}
