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
#include "xpost_file.h"
#include "xpost_context.h"
#include "xpost_error.h"
#include "xpost_name.h"
#include "xpost_string.h"
#include "xpost_dict.h"
#include "xpost_dev_generic.h"
#include "xpost_garbage.h" /* the collector setting VMReclaim names */

//#include "xpost_interpreter.h"
#include "xpost_operator.h"
#include "xpost_op_save.h"

/* The name FontDirectory denotes the local font directory while the
   allocation mode is local and GlobalFontDirectory while it is global
   (PLRM), so a font defined in terms of another finds the directory its
   own fonts are going into. Rebinding it is a write to systemdict, which
   is read-only once the language has loaded; the write replaces an entry
   that is already there, so it allocates nothing and is safe on the error
   path, where setglobal is reached while an error is being reported. */
static
void _rebind_fontdirectory(Xpost_Context *ctx)
{
    Xpost_Object sd;
    Xpost_Object fd;
    Xpost_Object_Tag_Access access;
    int ignore;

    /* both are null until the boot file has defined them */
    if (xpost_object_get_type(ctx->globalfontdir) != dicttype)
        return;
    sd = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);
    if (xpost_object_get_type(sd) != dicttype)
        return;

    fd = (ctx->vmmode == GLOBAL) ? ctx->globalfontdir : ctx->localfontdir;
    ignore = ctx->ignoreinvalidaccess;
    access = xpost_object_get_access(ctx, sd);
    ctx->ignoreinvalidaccess = 1;
    /* Opening the window is a write to systemdict's value, so it backs
       systemdict up to the save level it stands under before it takes
       effect, and a level that ends here gives back the systemdict this
       found rather than the one this made -- a program that may write
       systemdict may redefine the language. Refused, the rebinding is
       abandoned rather than made unrevertable: what it rebinds is a
       convenience the PLRM describes and not something the interpreter's
       own correctness rests on. */
    if (xpost_object_get_type(
            xpost_object_set_access(ctx, sd,
                                    XPOST_OBJECT_TAG_ACCESS_UNLIMITED))
        == invalidtype)
    {
        ctx->ignoreinvalidaccess = ignore;
        XPOST_LOG_ERR("cannot open systemdict to rebind FontDirectory");
        return;
    }
    /* the name is already in systemdict, so the store replaces an entry
       rather than making one: it allocates nothing and cannot be
       refused, which is what makes this safe on the error path */
    XPOST_REFUSAL_IMPOSSIBLE(
        xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "FontDirectory"), fd));
    /* systemdict is backed up at this level now, so shutting the window
       writes its head and takes no further backup: it cannot be refused */
    xpost_object_set_access(ctx, sd, access);
    ctx->ignoreinvalidaccess = ignore;
}

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

    vs = xpost_memory_save_stack_ent(ctx->lo);
    if (xpost_stack_count(ctx->lo, vs) >= 255)
        return limitcheck;
    v = xpost_save_create_snapshot_object(ctx->lo);
    /* the snapshot answers null when it could not be recorded, and a
       save object that records nothing would restore nothing */
    if (xpost_object_get_type(v) != savetype)
        return VMerror;
    /* remember the packing mode at this level so restore reverts it */
    if (v.save_.lev < sizeof ctx->packing_hist)
        ctx->packing_hist[v.save_.lev] = (unsigned char)ctx->packing;
    /* and the allocation mode, which restore reverts likewise */
    if (v.save_.lev < sizeof ctx->vmmode_hist)
        ctx->vmmode_hist[v.save_.lev] = (unsigned char)ctx->vmmode;
    /* and the user parameters, which restore reverts likewise. VMReclaim
       is recorded as the collector setting it names, that setting being
       the whole of the parameter. */
    if (v.save_.lev < sizeof ctx->autobanks_hist)
        ctx->autobanks_hist[v.save_.lev] =
            (unsigned char)xpost_garbage_auto_banks(ctx);
    if (v.save_.lev < sizeof ctx->vmthreshold_hist / sizeof ctx->vmthreshold_hist[0])
        ctx->vmthreshold_hist[v.save_.lev] = ctx->vmthreshold;
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
    ++ctx->namebind_gen; /* restored dicts may change bindings */

    vs = xpost_memory_save_stack_ent(ctx->lo);
    z = xpost_stack_count(ctx->lo, vs);
    /* the depth is counted, the level recorded: comparing them in the
       wider signed type keeps a depth that came back short of the level
       below it rather than above it */
    while(z > (integer)V.save_.lev)
    {
        xpost_save_restore_snapshot(ctx->lo);
        z--;
    }
    /* the packing mode is save/restore-subject: revert it to this level */
    if (V.save_.lev < sizeof ctx->packing_hist)
        ctx->packing = ctx->packing_hist[V.save_.lev];

    /* restore reverts the page device (PLRM 6.1): the snapshots above
       have just put the device the saved graphics state named back into
       it, and the device that was installed over it is displaced. Retire
       that one here, while it can still be reached -- what it holds is
       outside virtual memory, so nothing later in the run will, and the
       collector is free to take its dictionary from this point on. */
    xpost_device_retire_restored(ctx, (unsigned int)V.save_.lev);

    /* restore closes a file created since the corresponding save (PLRM
       3.8.2): sweep the local table for file entities born above the
       restored depth and close them. A file still referenced from a
       stack stays open: the spec answers that situation with
       invalidrestore, which is not implemented -- see the note in
       tests/save_restore_test.ps -- and closing under a live reference
       would be worse.

       The close is all this does. It does not reclaim the entity, and
       the scan below is why: it sees the objects lying on the stacks,
       and a file named from inside an array or a dictionary is named
       just as surely without appearing there. Reclaiming an entity on
       that evidence hands its number back to the free list while an
       object still holds it, and the object then names the list's own
       link word, or the next file the program opens. The payload of a
       file entity is a pointer this interpreter calls through, so the
       first is a jump to an address the free list wrote and the second
       is one file's operations landing on another.

       Which entities nothing reaches is the collector's question, and
       the collector descends into composites to answer it. A file
       closed here is left for that sweep to reclaim: its stream is
       gone, so what remains is one pointer's worth of table row.

       Each stack is read a segment at a time, in one pass from its
       root. A stack is a chain of segments and an index into one is
       reached by walking that chain, so asking for index 0, then index
       1, and so on to the top would walk the chain again for every
       element and cost the stack's depth once per element. Nothing
       allocates inside the walk, so the segment pointer stays good
       across it. */
    if (ctx->lo->file_birth_max > (unsigned int)V.save_.lev + 1)
    {
        unsigned int ent, stamp;
        unsigned int stacks[4];
        int k;

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
                Xpost_Stack *s;

                for (s = xpost_stack_at(ctx->lo, stacks[k]); s;
                     s = xpost_stack_next_segment(ctx->lo, s))
                {
                    unsigned int i;

                    for (i = 0; i < s->top; i++)
                    {
                        Xpost_Object o = s->data[i];

                        if (xpost_object_get_type(o) == filetype
                         && (unsigned int)o.mark_.padw == ent)
                            goto keep;
                    }
                }
            }
            {
                /* the vtable close releases the stream and clears the
                   entity's stored pointer; a reusable stream survives
                   its close rewound, keeps its pointer, and then keeps
                   its entity too */
                Xpost_Object o = { 0 };

                o.mark_.tag = filetype;
                o.mark_.pad0 = 0;
                o.mark_.padw = ent;
                /* restore is not a place a stream can refuse to close:
                   the file object is going away with the save level
                   whatever the close had left to write (PLRM 3.7.2) */
                (void)xpost_file_object_close(ctx->lo, o);
            }
        keep:;
        }
    }

    /* The allocation mode is save/restore-subject too (PLRM 8.2 restore),
       and the name FontDirectory denotes whichever directory the mode
       calls for, so reverting the one rebinds the other exactly as
       setglobal does.

       Both come last, after the rewind and after the teardown the rewind
       sets off: the device retirement runs a device's own release method
       and the sweep above closes files, and each of them works in the
       mode the level being discarded was running under. What the write
       to systemdict here is backed up against is virtual memory already
       rewound. */
    if (V.save_.lev < sizeof ctx->vmmode_hist
        && ctx->vmmode_hist[V.save_.lev] != (unsigned char)ctx->vmmode)
    {
        ctx->vmmode = ctx->vmmode_hist[V.save_.lev];
        _rebind_fontdirectory(ctx);
    }

    /* The user interpreter parameters are named in the same sentence
       (PLRM 8.2 restore, PLRM C.1.1), and each of them is a number a
       program reads back and a way the interpreter then behaves, so
       giving the number back means putting the behaviour back with it.
       VMReclaim is both at once: what a program reads is the setting
       that says which banks a collection running of its own accord
       reclaims, so putting that setting back is the whole of reverting
       it, and the two cannot come apart. VMThreshold is a count this
       interpreter records and reports and nothing else reads, so
       reverting the count is all there is to revert.

       These come last with the allocation mode, and for the same
       reason: the teardown above allocates, and both it and the
       collector it may set off belong to the level being discarded. */
    if (V.save_.lev < sizeof ctx->autobanks_hist)
        xpost_garbage_auto_banks_set(ctx, ctx->autobanks_hist[V.save_.lev]);
    if (V.save_.lev < sizeof ctx->vmthreshold_hist / sizeof ctx->vmthreshold_hist[0])
        ctx->vmthreshold = ctx->vmthreshold_hist[V.save_.lev];

    return 0;
}

/* bool  setglobal  -
   set vm allocation mode in current context. true is global. */
static
int Bsetglobal(Xpost_Context *ctx,
               Xpost_Object B)
{
    unsigned int mode = B.int_.val? GLOBAL: LOCAL;

    if (mode == ctx->vmmode)
        return 0;
    ctx->vmmode = mode;
    _rebind_fontdirectory(ctx);
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


int xpost_oper_init_save_ops(Xpost_Context *ctx,
                             Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;

    assert(ctx->gl->base);

    op = xpost_operator_cons(ctx, "save", (Xpost_Op_Func)Zsave, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "restore", (Xpost_Op_Func)Vrestore, 1, savetype);
    INSTALL;
    op = xpost_operator_cons(ctx, "setglobal", (Xpost_Op_Func)Bsetglobal, 1, booleantype);
    INSTALL;
    op = xpost_operator_cons(ctx, "currentglobal", (Xpost_Op_Func)Zcurrentglobal, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, "gcheck", (Xpost_Op_Func)Agcheck, 1, anytype);
    INSTALL;

    /* xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
    xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "mark"), mark); */

    return 0;
}
