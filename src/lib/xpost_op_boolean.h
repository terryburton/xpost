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

#ifndef XPOST_OP_BOOLEAN_H
#define XPOST_OP_BOOLEAN_H

/*
 * The verdict each of eq, ne, lt, le, gt and ge draws from the
 * three-way comparison of its two operands, shared between those
 * operators and the interpreter's fused procedure execution so the
 * relations exist once.
 */
typedef enum
{
    XPOST_OP_REL_EQ,
    XPOST_OP_REL_NE,
    XPOST_OP_REL_LT,
    XPOST_OP_REL_LE,
    XPOST_OP_REL_GT,
    XPOST_OP_REL_GE
} Xpost_Op_Relation;

/**
 * @brief whether a relation is one of the ordered four.
 *
 * eq and ne compare any two objects; lt, le, gt and ge order them, and
 * only a pair of numbers or a pair of strings can be ordered. Asked by
 * name rather than by where the relation sits in the enumeration, so
 * that reordering the enumeration cannot quietly change which relations
 * are restricted on one road and not the other.
 */
static inline int xpost_op_relation_is_ordered(Xpost_Op_Relation rel)
{
    switch (rel)
    {
        case XPOST_OP_REL_LT: /*@fallthrough@*/
        case XPOST_OP_REL_LE: /*@fallthrough@*/
        case XPOST_OP_REL_GT: /*@fallthrough@*/
        case XPOST_OP_REL_GE: return 1;
        default:              return 0;
    }
}

static inline int xpost_op_relation(Xpost_Op_Relation rel, int cmp)
{
    switch (rel)
    {
        case XPOST_OP_REL_EQ: return cmp == 0;
        case XPOST_OP_REL_NE: return cmp != 0;
        case XPOST_OP_REL_LT: return cmp < 0;
        case XPOST_OP_REL_LE: return cmp <= 0;
        case XPOST_OP_REL_GT: return cmp > 0;
        default:              return cmp >= 0;
    }
}

/**
 * @brief whether the ordered relations may be applied to this pair.
 *
 * lt, le, gt and ge take two numbers or two strings; anything else,
 * including one of each, is a typecheck (PLRM 8.2). eq and ne are the
 * general pair and take any two objects, so they do not ask this.
 * Shared with the interpreter's fused execution, which reaches the same
 * comparison by another road and must reach the same answer.
 */
static inline int xpost_op_ordered_comparable(Xpost_Object x, Xpost_Object y)
{
    int xt = xpost_object_get_type(x);
    int yt = xpost_object_get_type(y);
    int xnum = (xt == integertype) || (xt == realtype);
    int ynum = (yt == integertype) || (yt == realtype);

    if (xnum && ynum)
        return 1;
    return (xt == stringtype) && (yt == stringtype);
}

int xpost_oper_init_bool_ops(Xpost_Context *ctx, Xpost_Object sd);

#endif
