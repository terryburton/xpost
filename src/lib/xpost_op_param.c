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
#include "xpost_op_math.h"   /* a count, as the object the PLRM gives it */
#include "xpost_op_param.h"

static
int vmreclaim (Xpost_Context *ctx, Xpost_Object I)
{
    switch (I.int_.val)
    {
        default: return rangecheck;

        /* PLRM 8.2: the negative operands turn automatic collection off,
           for one bank or for both, and zero turns it on again. What is
           turned off is only the collection that runs of its own accord;
           an immediate collection the operator is asked for below still
           runs, which is what makes a program able to say when it would
           rather pay for one. */
        case -2: /* disable automatic collection in local and global vm */
            ctx->lo->garbage_collect_auto = 0;
            ctx->gl->garbage_collect_auto = 0;
            break;
        case -1: /* disable automatic collection in local vm */
            ctx->lo->garbage_collect_auto = 0;
            break;
        case 0: /* enable automatic collection */
            ctx->lo->garbage_collect_auto = 1;
            ctx->gl->garbage_collect_auto = 1;
            break;

        /* An immediate collection marks both banks whichever it
           reclaims: an object in one may be named from the other, so a
           walk that stopped at the boundary would take a named object
           for garbage. Which bank is then reclaimed is what the operand
           says. */
        case 1: /* perform immediate collection in local vm */
            if (ctx->garbage_collect_function(ctx->lo,
                                              XPOST_GARBAGE_SWEEP_LOCAL, 1) == -1)
                return VMerror;
            break;
        case 2: /* perform immediate collection in local and global vm */
            if (ctx->garbage_collect_function(ctx->lo,
                                              XPOST_GARBAGE_SWEEP_BOTH, 1) == -1)
                return VMerror;
            break;
    }
    return 0;
}

static
int vmstatus (Xpost_Context *ctx)
{
    int lev;
    Xpost_Memory_File *vm;
    unsigned int vstk;

    vstk = xpost_memory_save_stack_adr(ctx->lo);
    lev = xpost_stack_count(ctx->lo, vstk);
    /* PLRM 8.2: the two counts are of the bank the allocation mode
       selects, virtual memory being accounted for separately in each.
       The level is not: it is the depth of save nesting, which a
       program has one of. */
    vm = (ctx->vmmode == GLOBAL) ? ctx->gl : ctx->lo;

    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(lev)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_count_cons(vm->used)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_count_cons(vm->max)))
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

/* nobjects nbytes  .vmreserve  bool
   Whether the virtual memory now being allocated from can take nobjects
   more composite elements and nbytes more bytes of string storage,
   having obtained the room for them.

   A caller that allocates a large structure one piece at a time cannot
   recover from running out partway: the pieces it has already taken are
   reachable from nothing and the memory they occupy is not returned to
   it, so the failure leaves the interpreter with less than it had
   before. Pricing the whole structure and asking for it here moves that
   failure to before the first piece. The file either has the room
   already, or grows once to hold it, or answers false having changed
   nothing -- a grow that cannot be satisfied leaves the existing mapping
   in place -- so the caller's error costs no memory at all.

   The counts arrive as reals because a structure large enough to be
   worth refusing overflows a 32-bit integer. An object addresses its
   memory file through an unsigned 32-bit offset, so a request past that
   span cannot be met however much memory the host has, and is refused
   without asking for it. */
static
int vmreserve (Xpost_Context *ctx, Xpost_Object nobjects, Xpost_Object nbytes)
{
    Xpost_Memory_File *mem;
    double want;
    int fits;

    /* written as a failed lower bound rather than as a comparison
       against zero, so that a count that is not a number at all is
       refused here instead of being converted to an integer it has no
       value for */
    if (!(nobjects.real_.val >= 0.0) || !(nbytes.real_.val >= 0.0))
        return rangecheck;

    want = (double)nobjects.real_.val * (double)sizeof(Xpost_Object)
         + (double)nbytes.real_.val;

    mem = (ctx->vmmode == GLOBAL) ? ctx->gl : ctx->lo;

    if (want > (double)0xffffffffu
        || (double)mem->used + want > (double)0xffffffffu)
        fits = 0;
    else if ((size_t)mem->used + (size_t)want < (size_t)mem->max)
        fits = 1;
    else
        fits = xpost_memory_file_grow(mem, (unsigned int)want) ? 1 : 0;

    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_bool_cons(fits)))
        return stackoverflow;
    return 0;
}

static
int globalvmstatus (Xpost_Context *ctx)
{
    int lev;
    unsigned int vstk;

    vstk = xpost_memory_save_stack_adr(ctx->gl);
    lev = xpost_stack_count(ctx->gl, vstk);
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(lev)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_count_cons(ctx->gl->used)))
        return stackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->os, xpost_count_cons(ctx->gl->max)))
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
    op = xpost_operator_cons(ctx, ".vmreserve", (Xpost_Op_Func)vmreserve, 2, floattype, floattype);
    INSTALL;

    /* xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
    op = xpost_operator_cons(ctx, "save", (Xpost_Op_Func)Zsave, 1, 0);
    INSTALL;
    xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "mark"), mark); */

    return 0;
}
