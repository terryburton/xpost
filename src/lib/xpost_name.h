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

#ifndef XPOST_NM_H
#define XPOST_NM_H

/**
 * @file xpost_name.h
 * @brief array functions
 *
 * The name mechanism associates strings with integers
 * using a ternary search tree
 * and a stack of string objects.
 *
 * @{
 */

#include "xpost_private.h" /* XPOST_TEST_VISIBLE */

typedef struct tst
{
    unsigned val,
             lo,
             eq,
             hi;
} tst;
int xpost_name_init(Xpost_Context *ctx);
XPOST_TEST_VISIBLE Xpost_Object xpost_name_cons(Xpost_Context *ctx, const char *s);

/*
   construct a name object from a counted string, which may contain
   any bytes, embedded nuls included
 */
Xpost_Object xpost_name_cons_n(Xpost_Context *ctx, const char *s, unsigned int n);

/**
 * @brief Construct a name object in global VM regardless of the
 * current allocation mode. Operator names must live in the global
 * name space: the operator table records them by global index.
 */
Xpost_Object xpost_name_cons_global(Xpost_Context *ctx, const char *s);
Xpost_Object xpost_name_get_string(Xpost_Context *ctx, Xpost_Object n);

/**
 * @brief How many times a string has been offered to the name mechanism
 * and had to be looked up in the tree.
 *
 * A name already interned still costs a walk, and a global name costs
 * two -- the local bank is searched first and misses. So this counts
 * the work of resolving names, not the names that exist: a caller that
 * resolves the same name once per unit of work it does is the shape
 * this number is read for. It saturates rather than wrapping.
 */
unsigned int xpost_name_lookups(void);

/**
 * @}
 */

#endif
