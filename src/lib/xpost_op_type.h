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

#ifndef XPOST_OP_TYPE_H
#define XPOST_OP_TYPE_H

/*
 * The type an object reports, shared between the type operator and the
 * interpreter's fused procedure execution so the naming exists once.
 *
 * The answer is an index rather than a string because the interpreter
 * caches one executable name per index: the object types index
 * themselves, and a packed array -- stored as a read-only array but a
 * type of its own -- takes the one index beyond them. An object whose
 * type word is out of range reports the invalid type.
 */
#define XPOST_OP_TYPE_PACKEDARRAY XPOST_OBJECT_NTYPES
#define XPOST_OP_TYPE_NNAMES (XPOST_OBJECT_NTYPES + 1)

static inline unsigned int xpost_op_type_index(Xpost_Object o)
{
    Xpost_Object_Type type = xpost_object_get_type(o);

    if (type >= XPOST_OBJECT_NTYPES)
        return (unsigned int)invalidtype;
    if (type == arraytype && xpost_object_is_packed(o))
        return XPOST_OP_TYPE_PACKEDARRAY;
    return (unsigned int)type;
}

static inline const char *xpost_op_type_name(unsigned int index)
{
    return index == XPOST_OP_TYPE_PACKEDARRAY ? "packedarraytype"
                                              : xpost_object_type_names[index];
}

/**
 * @brief the type-pattern code a type name denotes, or -1.
 *
 * The names the type operator answers, plus the pattern names the
 * signature machinery understands: numbertype, proctype, anytype.
 */
int xpost_op_type_code(Xpost_Context *ctx, Xpost_Object name);

int xpost_oper_init_type_ops(Xpost_Context *ctx, Xpost_Object sd);

#endif
