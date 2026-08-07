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

#ifndef XPOST_OP_MATH_H
#define XPOST_OP_MATH_H

int xpost_oper_init_math_ops(Xpost_Context *ctx, Xpost_Object sd);

/*
 * Integer arithmetic with PLRM 3.3.2 range semantics, shared verbatim
 * between the add/sub/mul operators and the interpreter's fused
 * procedure execution so the two can never disagree: a result outside
 * the PostScript integer range becomes a real of the true value rather
 * than wrapping.
 *
 * The bounds are those of the `integer` type, so a build with a wider
 * integer widens them with it.
 */
#define XPOST_INTEGER_MAX \
    ((long long)(((unsigned long long)1 << (sizeof(integer)*8 - 1)) - 1))
#define XPOST_INTEGER_MIN (-XPOST_INTEGER_MAX - 1)

/* The operands arrive in the integer's own width, which is the width the
   bounds above are drawn in. `long` is that width on some platforms and
   half of it on others, so an operand taken as a `long` would be a
   different operand on each. */
typedef char xpost_integer_range_spans_the_integer[
    sizeof(long long) >= sizeof(integer)
    && sizeof(dword) == sizeof(integer) ? 1 : -1];

static inline int xpost_int_add_willover(integer x, integer y)
{
    if (y < 0) return x < XPOST_INTEGER_MIN - y;
    return x > XPOST_INTEGER_MAX - y;
}

static inline int xpost_int_sub_willunder(integer x, integer y)
{
    if (y < 0) return x > XPOST_INTEGER_MAX + y;
    return x < XPOST_INTEGER_MIN + y;
}

static inline int xpost_int_mul_willover(integer x, integer y)
{
    /* the magnitudes are held unsigned, so the most negative operand --
       which has no positive counterpart -- is measured without leaving the
       field, and the division that follows is over two magnitudes */
    dword xx = x < 0 ? (dword)0 - (dword)x : (dword)x;
    dword yy = y < 0 ? (dword)0 - (dword)y : (dword)y;
    if (xx == 0 || yy == 0) return 0;
    return xx > (dword)XPOST_INTEGER_MAX / yy;
}

/**
 * @brief the sum/difference/product of two integer objects, as the
 * object the PLRM prescribes: an integer, or a real when the exact
 * result leaves the integer range.
 */
static inline Xpost_Object xpost_int_add(integer x, integer y)
{
    return xpost_int_add_willover(x, y) ? xpost_real_cons((real)x + y)
                                        : xpost_int_cons(x + y);
}

static inline Xpost_Object xpost_int_sub(integer x, integer y)
{
    return xpost_int_sub_willunder(x, y) ? xpost_real_cons((real)x - y)
                                         : xpost_int_cons(x - y);
}

static inline Xpost_Object xpost_int_mul(integer x, integer y)
{
    return xpost_int_mul_willover(x, y) ? xpost_real_cons((real)x * y)
                                        : xpost_int_cons(x * y);
}

#endif
