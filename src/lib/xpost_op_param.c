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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h> /* NULL strtod */
#include <string.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_save.h"
#include "xpost_context.h"
#include "xpost_name.h"
#include "xpost_string.h"
#include "xpost_dict.h"
#include "xpost_error.h"

#include "xpost_garbage.h"
//#include "xpost_interpreter.h"
#include "xpost_operator.h"
#include "xpost_op_param.h"

static
int vmreclaim (Xpost_Context *ctx, Xpost_Object I)
{
    switch (I.int_.val)
    {
        default: return rangecheck;
        case -2: /* disable automatic collection in local and global vm */
            break;
        case -1: /* disable automatic collection in local vm */
            break;
        case 0: /* enable automatic collection */
            break;
        case 1: /* perform immediate collection in local vm */
            if (ctx->garbage_collect_function(ctx->lo, 1, 0) == -1)
                return VMerror;
            break;
        case 2: /* local and global; global collection is disabled (see
                   xpost_garbage_collect), so perform the local collection --
                   passing the global mfile would collect nothing. Global
                   objects cannot reference local ones, so the local sweep is
                   complete on its own. */
            if (ctx->garbage_collect_function(ctx->lo, 1, 0) == -1)
                return VMerror;
            break;
    }
    return 0;
}

static
int vmstatus (Xpost_Context *ctx)
{
    int lev, used, max;
    unsigned int vstk;

    vstk = xpost_memory_save_stack_adr(ctx->lo);
    lev = xpost_stack_count(ctx->lo, vstk);
    used = ctx->gl->used + ctx->lo->used;
    max = ctx->gl->max + ctx->lo->max;

    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(lev)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(used)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(max)))
        return stackoverflow;
    return 0;
}

/* -  .vmentcount  local global
   The number of entity slots each memory table has handed out. Entity
   numbers are a budget of their own, separate from the byte counts
   vmstatus reports: an entity freed goes on a free list and is handed
   out again, so this number rises only where nothing reclaims what a
   job has stopped using. */
static
int vmentcount (Xpost_Context *ctx)
{
    if (!xpost_stack_push(ctx->lo, ctx->os,
                          xpost_int_cons((int)ctx->lo->table.nextent)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os,
                          xpost_int_cons((int)ctx->gl->table.nextent)))
        return stackoverflow;
    return 0;
}

/* -  .vmfreescan  local global
   The number of free-list entries the allocator has examined in each
   memory file, saturating rather than wrapping. What an allocation
   costs is this number and not the bytes it asks for, so it is the
   measure of whether the cost of allocating tracks the allocations a
   job makes or the memory it has already released. */
static
int vmfreescan (Xpost_Context *ctx)
{
    if (!xpost_stack_push(ctx->lo, ctx->os,
                          xpost_int_cons((int)ctx->lo->free_scan)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os,
                          xpost_int_cons((int)ctx->gl->free_scan)))
        return stackoverflow;
    return 0;
}

static
int globalvmstatus (Xpost_Context *ctx)
{
    int lev, used, max;
    unsigned int vstk;

    vstk = xpost_memory_save_stack_adr(ctx->gl);
    lev = xpost_stack_count(ctx->gl, vstk);
    used = ctx->gl->used;
    max = ctx->gl->max;

    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(lev)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(used)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(max)))
        return stackoverflow;
    return 0;
}


int xpost_oper_init_param_ops(Xpost_Context *ctx,
                              Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;

    assert(ctx->gl->base);

    op = xpost_operator_cons(ctx, "vmreclaim", (Xpost_Op_Func)vmreclaim, 1, integertype);
    INSTALL;
    op = xpost_operator_cons(ctx, "vmstatus", (Xpost_Op_Func)vmstatus, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "globalvmstatus", (Xpost_Op_Func)globalvmstatus, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, ".vmentcount", (Xpost_Op_Func)vmentcount, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, ".vmfreescan", (Xpost_Op_Func)vmfreescan, 0);
    INSTALL;

    /* xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
    op = xpost_operator_cons(ctx, "save", (Xpost_Op_Func)Zsave, 1, 0);
    INSTALL;
    xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "mark"), mark); */

    return 0;
}
