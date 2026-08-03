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

#ifndef XPOST_OP_STACK_H
#define XPOST_OP_STACK_H

/* stack operators */

/*
 * The index and roll rules, shared between the operators and the
 * interpreter's fused procedure execution so each exists once.
 */

/*
 * index counts down from the top, 0 selecting the topmost operand. A
 * negative count is a rangecheck; one that reaches past the operands
 * held is a stackunderflow. The fused walker passes the count of
 * operands it can reach in one stack segment, so a selection beyond
 * that leaves the fast path for the operator, which counts them all.
 */
static inline int xpost_op_index_check(integer n, int count)
{
    if (n < 0)
        return rangecheck;
    if (n >= count)
        return stackunderflow;
    return 0;
}

/*
 * roll takes its shift modulo the count of operands rolled, and a
 * negative shift rolls the other way, so the shift always lands in
 * [0, n).
 */
static inline integer xpost_op_roll_shift(integer n, integer j)
{
    j %= n;
    if (j < 0)
        j += n;
    return j;
}

/*
 * After the roll, the operand at position i counting down from the top
 * of the rolled group is the one that was at position (i + j).
 */
static inline integer xpost_op_roll_source(integer i, integer n, integer j)
{
    return (i + j) % n;
}

int xpost_op_cleartomark(Xpost_Context *ctx);
int xpost_op_counttomark(Xpost_Context *ctx);

int xpost_oper_init_stack_ops(Xpost_Context *ctx, Xpost_Object sd);

#endif
