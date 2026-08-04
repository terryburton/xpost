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
#include "xpost_free.h"
#include "xpost_file.h"
#include "xpost_context.h"
#include "xpost_error.h"
#include "xpost_name.h"
#include "xpost_string.h"
#include "xpost_dict.h"

//#include "xpost_interpreter.h"
#include "xpost_operator.h"
#include "xpost_op_save.h"

/* -  save  save
   create save object representing vm contents */
static
int Zsave(Xpost_Context *ctx)
{
    unsigned int vs;

    /* each object's mark records the save level (as level+1) in an 8-bit
       field, so the save stack cannot exceed 255 levels without aliasing
       another level's bookkeeping */
    Xpost_Object v;

    if (xpost_memory_table_get_addr(ctx->lo,
            XPOST_MEMORY_TABLE_SPECIAL_SAVE_STACK, &vs)
        && xpost_stack_count(ctx->lo, vs) >= 255)
        return limitcheck;
    v = xpost_save_create_snapshot_object(ctx->lo);
    /* remember the packing mode at this level so restore reverts it */
    if (v.save_.lev < sizeof ctx->packing_hist)
        ctx->packing_hist[v.save_.lev] = (unsigned char)ctx->packing;
    if (!xpost_stack_push(ctx->lo, ctx->os, v))
        return stackoverflow;
    return 0;
}

/* save  restore  -
   rewind vm to saved state */
static
int Vrestore(Xpost_Context *ctx,
             Xpost_Object V)
{
    int z;
    unsigned int vs;
    int ret;
    ++ctx->namebind_gen; /* restored dicts may change bindings */

    ret = xpost_memory_table_get_addr(ctx->lo,
                                      XPOST_MEMORY_TABLE_SPECIAL_SAVE_STACK, &vs);
    if (!ret)
    {
        XPOST_LOG_ERR("cannot retrieve address for save stack");
        return VMerror;
    }
    z = xpost_stack_count(ctx->lo, vs);
    while(z > V.save_.lev)
    {
        xpost_save_restore_snapshot(ctx->lo);
        z--;
    }
    /* the packing mode is save/restore-subject: revert it to this level */
    if (V.save_.lev < sizeof ctx->packing_hist)
        ctx->packing = ctx->packing_hist[V.save_.lev];

    /* restore closes a file created since the corresponding save (PLRM
       3.8.2): sweep the local table for file entities born above the
       restored depth and release them -- closing and freeing exactly as
       the collector would, since nothing surviving the restore can
       reach them. A file still referenced from a stack stays open: the
       spec answers that situation with invalidrestore, which is not
       implemented -- see the note in tests/save_restore_test.ps -- and
       closing under a live reference would be worse. */
    if (ctx->lo->file_birth_max > (unsigned int)V.save_.lev + 1)
    {
        unsigned int ent, i, stamp;
        unsigned int stacks[4];
        int k, n;

        stacks[0] = ctx->os; stacks[1] = ctx->es;
        stacks[2] = ctx->ds; stacks[3] = ctx->hold;
        for (ent = ctx->lo->start; ent < ctx->lo->table.nextent; ent++)
        {
            if (ctx->lo->table.tab[ent].tag != filetype)
                continue;
            stamp = (ctx->lo->table.tab[ent].mark
                     & XPOST_MEMORY_TABLE_MARK_DATA_LOWLEVEL_MASK)
                    >> XPOST_MEMORY_TABLE_MARK_DATA_LOWLEVEL_OFFSET;
            if (stamp == 0 || stamp <= (unsigned int)V.save_.lev + 1)
                continue;
            for (k = 0; k < 4; k++)
            {
                n = xpost_stack_count(ctx->lo, stacks[k]);
                for (i = 0; i < (unsigned int)n; i++)
                {
                    Xpost_Object o =
                        xpost_stack_bottomup_fetch(ctx->lo, stacks[k], i);
                    if (xpost_object_get_type(o) == filetype
                     && (unsigned int)o.mark_.padw == ent)
                        goto keep;
                }
            }
            {
                /* the vtable close releases the stream and clears the
                   entity's stored pointer; a reusable stream survives
                   its close rewound, keeps its pointer, and then keeps
                   its entity too */
                Xpost_Object o;

                o.mark_.tag = filetype;
                o.mark_.pad0 = 0;
                o.mark_.padw = ent;
                xpost_file_object_close(ctx->lo, o);
                if (xpost_file_get_file_pointer(ctx->lo, o) == NULL)
                    xpost_free_memory_ent(ctx->lo, ent);
            }
        keep:;
        }
    }
    return 0;
}

/* bool  setglobal  -
   set vm allocation mode in current context. true is global. */
static
int Bsetglobal(Xpost_Context *ctx,
               Xpost_Object B)
{
    ctx->vmmode = B.int_.val? GLOBAL: LOCAL;
    return 0;
}

/* -  currentglobal  bool
   return vm allocation mode for current context */
static
int Zcurrentglobal(Xpost_Context *ctx)
{
    xpost_stack_push(ctx->lo, ctx->os, xpost_bool_cons(ctx->vmmode==GLOBAL));
    return 0;
}

/* any  gcheck  bool
   check whether value is a legal element of a global composite
   object: simple objects always are; composite objects are when
   their value lives in global VM */
static
int Agcheck(Xpost_Context *ctx,
            Xpost_Object A)
{
    Xpost_Object r;
    switch(xpost_object_get_type(A))
    {
        default:
            r = xpost_bool_cons(1); break;
        case stringtype:
        case dicttype:
        case arraytype:
        case filetype:
            r = xpost_bool_cons((A.tag&XPOST_OBJECT_TAG_DATA_FLAG_BANK)!=0);
    }
    xpost_stack_push(ctx->lo, ctx->os, r);
    return 0;
}

#if 0
/* -  vmstatus  level used max
   return size information for (local) vm */
static
int Zvmstatus(Xpost_Context *ctx)
{
    unsigned int vs;

    xpost_memory_table_get_addr(ctx->lo,
                                XPOST_MEMORY_TABLE_SPECIAL_SAVE_STACK, &vs);
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(xpost_stack_count(ctx->lo, vs)));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(ctx->lo->used));
    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(ctx->lo->max));
    return 0;
}
#endif

int xpost_oper_init_save_ops(Xpost_Context *ctx,
                             Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;
    unsigned int optadr;

    assert(ctx->gl->base);
    //xpost_memory_table_get_addr(ctx->gl, XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE, &optadr);
    //optab = (void *)(ctx->gl->base + optadr);

    op = xpost_operator_cons(ctx, "save", (Xpost_Op_Func)Zsave, 1, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "restore", (Xpost_Op_Func)Vrestore, 0, 1, savetype);
    INSTALL;
    op = xpost_operator_cons(ctx, "setglobal", (Xpost_Op_Func)Bsetglobal, 0, 1, booleantype);
    INSTALL;
    op = xpost_operator_cons(ctx, "currentglobal", (Xpost_Op_Func)Zcurrentglobal, 1, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "gcheck", (Xpost_Op_Func)Agcheck, 1, 1, anytype);
    INSTALL;
#if 0
    op = xpost_operator_cons(ctx, "vmstatus", (Xpost_Op_Func)Zvmstatus, 3, 0);
    INSTALL;
#endif

    /* xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
    xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "mark"), mark); */

    return 0;
}
