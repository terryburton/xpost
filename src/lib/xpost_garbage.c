/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * Copyright (C) 2013, Thorsten Behrens
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
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h> /* close */
#endif

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_compat.h" /* xpost_mkstemp */
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_free.h"
#include "xpost_context.h"
#include "xpost_array.h"
#include "xpost_string.h"
#include "xpost_dict.h"
#include "xpost_save.h"
#include "xpost_name.h"
#include "xpost_file.h"
#include "xpost_handle.h"

//#include "xpost_interpreter.h"
#include "xpost_garbage.h"

#ifdef DEBUG_GC
#include <stdio.h>
#endif


/* Marking walks the object graph over the worklist below rather than
   over the C stack. Nothing in the language bounds how deeply a program
   may nest one composite inside another -- a chain of arrays each
   holding the next costs only the entity numbers and the virtual memory
   it spends -- so a walk that took a C frame per link would reach the
   end of the stack while the program was still inside every limit the
   specification names, and end the process rather than the program.

   The worklist is storage of the collector's own, fixed at load time, so
   walking never allocates: a mark that had to allocate in order to walk
   would fail exactly where the collector is what a shortage calls for.
   What fixed storage cannot promise is room for a frontier of any width,
   so an entity is marked before it is queued, and one that finds the
   worklist full is left carrying a bit in its mark word in place of a
   seat in the queue. When the worklist next empties, the tables are
   walked for entities carrying that bit and the descent resumes from
   each. An entity is queued only in the step that marks it, and an
   entity is marked once, so every such pass takes up entities no earlier
   pass reached and the walk ends. */

/* the mark word packs four fields into its low 31 bits (see
   Xpost_Memory_Table_Mark_Data); the top bit is the collector's, and
   says that an entity is marked but not yet descended into */
#define XPOST_GARBAGE_MARK_WAITING 0x80000000u

#define XPOST_GARBAGE_WORK_MAX 8192

/* an entity whose contents are still to be walked, with the memory file
   it belongs to: a composite in one bank may hold objects in the other */
typedef struct
{
    Xpost_Memory_File *mem;
    unsigned int ent;
    unsigned int isdict;
} Xpost_Garbage_Work;

static Xpost_Garbage_Work _xpost_garbage_work[XPOST_GARBAGE_WORK_MAX];
static unsigned int _xpost_garbage_work_n;
static int _xpost_garbage_work_waiting;
static int _xpost_garbage_work_ready;

/* clear the waiting flag over a table, leaving the marks as they are */
static
void _xpost_garbage_unwait(Xpost_Memory_File *mem)
{
    unsigned int i;

    if (!mem) return;

    for (i = mem->start; i < mem->table.nextent; i++)
    {
        mem->table.tab[i].mark &= ~(unsigned int)XPOST_GARBAGE_MARK_WAITING;
    }
}

/* queue a marked entity for its contents to be walked, or, if the
   worklist is full, leave it for the table pass to take up.

   The flag is read back out of the mark word, so a bit found set has to
   be one this walk set: a table slot handed out for the first time
   carries whatever the table's growth left in that word. Both banks are
   swept clear of it here, on the first entity of a collection that has
   to wait, rather than where a collection begins -- a walk whose
   frontier stays inside the worklist never reads the flag at all, and
   most do. */
static
void _xpost_garbage_work_push(Xpost_Context *ctx,
                              Xpost_Memory_File *mem,
                              unsigned int ent,
                              unsigned int isdict)
{
    if (_xpost_garbage_work_n < XPOST_GARBAGE_WORK_MAX)
    {
        _xpost_garbage_work[_xpost_garbage_work_n].mem = mem;
        _xpost_garbage_work[_xpost_garbage_work_n].ent = ent;
        _xpost_garbage_work[_xpost_garbage_work_n].isdict = isdict;
        ++_xpost_garbage_work_n;
        return;
    }

    if (!_xpost_garbage_work_ready)
    {
        _xpost_garbage_unwait(ctx->lo);
        _xpost_garbage_unwait(ctx->gl);
        _xpost_garbage_work_ready = 1;
    }
    mem->table.tab[ent].mark |= XPOST_GARBAGE_MARK_WAITING;
    _xpost_garbage_work_waiting = 1;
}

/* iterate through all tables,
    clear the MARK in the mark. */
static
void _xpost_garbage_unmark(Xpost_Memory_File *mem)
{
    unsigned int i;

    if (!mem) return;

    for (i = mem->start; i < mem->table.nextent; i++)
    {
        mem->table.tab[i].mark &= ~XPOST_MEMORY_TABLE_MARK_DATA_MARK_MASK;
    }
}

/* set the MARK in the mark in the tab[ent] */
static
int _xpost_garbage_mark_ent(Xpost_Memory_File *mem,
                            unsigned int ent)
{
    if (!mem) return 0;

    if (ent < mem->start)
    {
        XPOST_LOG_ERR("attempt to mark ent %u < mem->start", ent);
        return 1;
    }

    if (!xpost_ent_valid(mem, ent))
    {
        XPOST_LOG_ERR("cannot find ent %u", ent);
        return 0;
    }
    mem->table.tab[ent].mark |= XPOST_MEMORY_TABLE_MARK_DATA_MARK_MASK;
    return 1;
}

/* is it marked? */
static
int _xpost_garbage_ent_is_marked(Xpost_Memory_File *mem,
                                 unsigned int ent,
                                 int *retval)
{
    if (!mem) return 0;

    if (!xpost_ent_valid(mem, ent))
    {
        XPOST_LOG_ERR("cannot find table for ent %u", ent);
        return 0;
    }
    *retval = (mem->table.tab[ent].mark & XPOST_MEMORY_TABLE_MARK_DATA_MARK_MASK)
        >> XPOST_MEMORY_TABLE_MARK_DATA_MARK_OFFSET;

    return 1;
}

/* mark what an object refers to, queueing it if it has contents to walk */
static
int _xpost_garbage_mark_reach(Xpost_Context *ctx, Xpost_Memory_File *mem, Xpost_Object o, int markall);

/* mark what a dictionary's keys and values refer to */
static
int _xpost_garbage_reach_dict(Xpost_Context *ctx,
                              Xpost_Memory_File *mem,
                              unsigned int adr,
                              int markall)
{
    if (!mem) return 0;

    {
        dichead *dp = xpost_dict_head_at(mem, adr);
        dicrec *tp = xpost_dict_table_of(dp);
        /* the table is more than twice as long as the size it is derived
           from, so it is counted in a type wide enough for that product
           rather than in the type the size itself is stored in */
        unsigned int n = DICTABN(dp->sz);
        unsigned int j;
#ifdef DEBUG_GC
        Xpost_Object_Type type;
        printf("markdict: nused=%u\n", (unsigned int)dp->nused);
#endif

        for (j = 0; j < n; j++)
        {
            if (xpost_object_get_type(tp[j].key) != nulltype){
                if (!_xpost_garbage_mark_reach(ctx,
                            xpost_context_select_memory(ctx,tp[j].key), tp[j].key, markall))
                    return 0;
#ifdef DEBUG_GC
            switch(type = xpost_object_get_type(tp[j].key)){
            default: {
                    printf("%s", xpost_object_type_names[type]);
                }
                break;
            case nametype: {
                unsigned int address;
                Xpost_Object str;

                address = xpost_memory_name_stack_adr(
                    xpost_context_select_memory(ctx, tp[j].key));

                str = xpost_stack_bottomup_fetch(
                    xpost_context_select_memory(ctx,tp[j].key),
                    address, tp[j].key.mark_.padw);
                printf("%*s", str.comp_.sz, xpost_string_get_pointer(ctx,str));

                }
                break;
            }
            printf(":");
            printf("%s\n", xpost_object_type_names[xpost_object_get_type(tp[j].value)]);
#endif
                if (!_xpost_garbage_mark_reach(ctx,
                            xpost_context_select_memory(ctx,tp[j].value), tp[j].value, markall))
                    return 0;
            }
        }
    }

    return 1;
}

/* mark what an array's elements refer to */
static
int _xpost_garbage_reach_array(Xpost_Context *ctx,
                               Xpost_Memory_File *mem,
                               unsigned int adr,
                               unsigned int sz,
                               int markall)
{
    if (!mem) return 0;

#ifdef DEBUG_GC
    printf("markarray: sz=%u\n", sz);
#endif

    {
        Xpost_Object *op = xpost_vm_ptr(mem, adr);
        unsigned int j;

        for (j = 0; j < sz; j++)
        {
#ifdef DEBUG_GC
            printf("%u:%s\n", j, xpost_object_type_names[xpost_object_get_type(op[j])]);
#endif
            if (!_xpost_garbage_mark_reach(ctx,
                                           xpost_context_select_memory(ctx,op[j]),
                                           op[j], markall))
                return 0;
        }
    }

    return 1;
}

/* mark the entity an object names, and queue it if it has contents of
   its own to walk.
   if markall is true, this is a collection of global vm,
   so we must mark objects
   even if it means switching memory files
 */
static
int _xpost_garbage_mark_reach(Xpost_Context *ctx,
                              Xpost_Memory_File *mem,
                              Xpost_Object o,
                              int markall)
{
    int ret;
    unsigned int ent;
    Xpost_Object_Type type;
    Xpost_Memory_File *objmem;

    if (!mem) return 0;

    if (xpost_object_get_type(o) == filetype)
    {
        unsigned int fent = (unsigned int)o.mark_.padw;
        Xpost_Memory_File *fm = xpost_context_select_memory(ctx, o);
        /* a filter reads through the stream beneath it, which has an
           entity of its own that no object names -- the string forms of
           filter build one and hand it over. Follow the chain down from
           the filter, or the sweep takes a source out from under a filter
           still reading it. The walk stops at a stream already marked,
           which is also what ends it if a chain ever reaches itself.

           Every file object is answered here, so the type switch below
           carries no arm for one. */
        while (fm && xpost_ent_in_collector_band(fm, fent)
               && !(fm->table.tab[fent].mark
                    & XPOST_MEMORY_TABLE_MARK_DATA_MARK_MASK))
        {
            int nheld, h;

            /* a stream whose bytes a procedure supplies or disposes of
               holds that procedure, and the strings it has exchanged
               with it, in the struct rather than in virtual memory:
               nothing else names any of them, so the sweep would take
               them out from under a filter still reading or writing */
            nheld = xpost_file_held_count(fm, fent);
            for (h = 0; h < nheld; h++)
            {
                Xpost_Object held = xpost_file_held_object(fm, fent, h);

                if (!_xpost_garbage_mark_reach(ctx,
                        xpost_context_select_memory(ctx, held),
                        held, markall))
                    return 0;
            }
            if (!_xpost_garbage_mark_ent(fm, fent))
                break;
            fent = xpost_file_underlying_entity(fm, fent);
        }
        return 1;
    }

    if (!xpost_object_is_composite(o))
        return 1;

    ent = xpost_object_get_ent(o);
    type = xpost_object_get_type(o);

#ifdef DEBUG_GC
            printf("markobject: ent %u, addr %u, %s (size %u)\n",
                   ent,
                   xpost_context_select_memory(ctx,o)==mem?
                       (!xpost_ent_valid(mem, ent)?
                        (unsigned)-1: mem->table.tab[ent].adr) : 0,
                   xpost_object_type_names[type],
                   (unsigned int)o.comp_.sz);
#endif

    switch(type)
    {
        default: break;

        case arraytype:
            if (ent == 0)
            {
                return 1;
            }

            objmem = xpost_context_select_memory(ctx, o);
            if (!objmem) return 0;
            if (objmem != mem) {
                if (!markall)
                    break;
            }
            if (ent < objmem->start)
            {
                XPOST_LOG_ERR("attempt to mark %s object %d",
                        xpost_object_type_names[type],
                        ent);
                return 0;
            }
            if (!_xpost_garbage_ent_is_marked(objmem, ent, &ret))
                return 0;
            if (!ret) {
                ret = _xpost_garbage_mark_ent(objmem, ent);
                if (!ret)
                {
                    XPOST_LOG_ERR("cannot mark array %d", ent);
                    return 0;
                }
                /* subarray views share the entity: the descent covers the
                   whole underlying allocation, not the view window, so
                   elements reachable only through another view stay marked
                   (the ent-level marked flag makes the first view visited
                   the only one queued) */
                _xpost_garbage_work_push(ctx, objmem, ent, 0);
            }
            break;

        case dicttype:
            objmem = xpost_context_select_memory(ctx, o);
            if (objmem != mem)
            {
                if (!markall)
                    break;
            }
            if (ent < objmem->start)
            {
                XPOST_LOG_ERR("attempt to mark %s object %d",
                        xpost_object_type_names[type],
                        ent);
                return 0;
            }
            if (!_xpost_garbage_ent_is_marked(objmem, ent, &ret))
                return 0;
            if (!ret)
            {
                ret = _xpost_garbage_mark_ent(objmem, ent);
                if (!ret)
                {
                    XPOST_LOG_ERR("cannot mark dict");
                    return 0;
                }
                _xpost_garbage_work_push(ctx, objmem, ent, 1);
            }
            break;

        case stringtype:
            if (ent == 0)
            {
                return 1;
            }

            objmem = xpost_context_select_memory(ctx, o);
            if (objmem != mem)
            {
                if (!markall)
                    break;
            }

            if (ent < objmem->start)
            {
                XPOST_LOG_ERR("attempt to mark %s object %d",
                        xpost_object_type_names[type],
                        ent);
                return 0;
            }
            ret = _xpost_garbage_mark_ent(objmem, ent);
            if (!ret)
            {
                XPOST_LOG_ERR("cannot mark string");
                return 0;
            }
            break;
    }

    return 1;
}

/* mark what a queued entity holds. Its slot is read directly: an entity
   reaches the worklist only through the step that marks it, which is
   where the number is checked against the table. */
static
int _xpost_garbage_mark_contents(Xpost_Context *ctx,
                                 Xpost_Memory_File *mem,
                                 unsigned int ent,
                                 unsigned int isdict,
                                 int markall)
{
    if (isdict)
        return _xpost_garbage_reach_dict(ctx, mem,
                                         mem->table.tab[ent].adr, markall);

    return _xpost_garbage_reach_array(ctx, mem,
                                      mem->table.tab[ent].adr,
                                      mem->table.tab[ent].used
                                          / sizeof(Xpost_Object),
                                      markall);
}

/* walk the worklist until nothing is queued */
static
int _xpost_garbage_mark_drain(Xpost_Context *ctx, int markall)
{
    while (_xpost_garbage_work_n)
    {
        Xpost_Garbage_Work w = _xpost_garbage_work[--_xpost_garbage_work_n];

        if (!_xpost_garbage_mark_contents(ctx, w.mem, w.ent, w.isdict, markall))
        {
            _xpost_garbage_work_n = 0;
            return 0;
        }
    }

    return 1;
}

/* take up the entities the worklist had no room for, descending each
   before the next is queued so that the worklist has room for it */
static
int _xpost_garbage_mark_resume(Xpost_Context *ctx,
                               Xpost_Memory_File *mem,
                               int markall)
{
    unsigned int i;

    if (!mem) return 1;

    for (i = mem->start; i < mem->table.nextent; i++)
    {
        if (!(mem->table.tab[i].mark & XPOST_GARBAGE_MARK_WAITING))
            continue;
        mem->table.tab[i].mark &= ~(unsigned int)XPOST_GARBAGE_MARK_WAITING;
        _xpost_garbage_work_push(ctx, mem, i,
                                 mem->table.tab[i].tag == dicttype);
        if (!_xpost_garbage_mark_drain(ctx, markall))
            return 0;
    }

    return 1;
}

/* walk everything queued, and everything queued by walking it */
static
int _xpost_garbage_mark_walk(Xpost_Context *ctx, int markall)
{
    for (;;)
    {
        if (!_xpost_garbage_mark_drain(ctx, markall))
            return 0;
        if (!_xpost_garbage_work_waiting)
            return 1;
        _xpost_garbage_work_waiting = 0;
        /* an entity waits only where it was marked, and a context marks
           in the two banks it selects between, so these are all of them */
        if (!_xpost_garbage_mark_resume(ctx, ctx->lo, markall))
            return 0;
        if (!_xpost_garbage_mark_resume(ctx, ctx->gl, markall))
            return 0;
    }
}

/* mark an object and everything reachable through it */
static
int _xpost_garbage_mark_object(Xpost_Context *ctx,
                               Xpost_Memory_File *mem,
                               Xpost_Object o,
                               int markall)
{
    if (!_xpost_garbage_mark_reach(ctx, mem, o, markall))
    {
        _xpost_garbage_work_n = 0;
        return 0;
    }

    return _xpost_garbage_mark_walk(ctx, markall);
}

/* mark an array's elements and everything reachable through them */
static
int _xpost_garbage_mark_array(Xpost_Context *ctx,
                              Xpost_Memory_File *mem,
                              unsigned int adr,
                              unsigned int sz,
                              int markall)
{
    if (!_xpost_garbage_reach_array(ctx, mem, adr, sz, markall))
    {
        _xpost_garbage_work_n = 0;
        return 0;
    }

    return _xpost_garbage_mark_walk(ctx, markall);
}

/* mark a dictionary's entries and everything reachable through them */
static
int _xpost_garbage_mark_dict(Xpost_Context *ctx,
                             Xpost_Memory_File *mem,
                             unsigned int adr,
                             int markall)
{
    if (!_xpost_garbage_reach_dict(ctx, mem, adr, markall))
    {
        _xpost_garbage_work_n = 0;
        return 0;
    }

    return _xpost_garbage_mark_walk(ctx, markall);
}


/* mark all names in stack except 0::BOGUSNAME */
static
int _xpost_garbage_mark_names(Xpost_Context *ctx,
                              Xpost_Memory_File *mem,
                              unsigned int stackadr,
                              int markall)
{
    unsigned int start = 1; /* skip 0::BOGUSNAME */
    Xpost_Stack *s;
    unsigned int i;
    if (!mem) return 0;

#ifdef DEBUG_GC
    printf("marking stack of size %d\n", xpost_stack_count(mem, stackadr));
#endif

    for (s = xpost_stack_at(mem, stackadr); s;
         s = xpost_stack_next_segment(mem, s), start = 0)
    {
        for (i = start; i < s->top; i++)
        {
            if (!_xpost_garbage_mark_object(ctx, mem, s->data[i], markall))
                return 0;
        }
    }

    return 1;
}


/* mark all allocations referred to by objects in stack */
static
int _xpost_garbage_mark_stack(Xpost_Context *ctx,
                              Xpost_Memory_File *mem,
                              unsigned int stackadr,
                              int markall)
{
    Xpost_Stack *s;
    unsigned int i;
    if (!mem) return 0;

#ifdef DEBUG_GC
    printf("marking stack of size %d\n", xpost_stack_count(mem, stackadr));
#endif

    for (s = xpost_stack_at(mem, stackadr); s;
         s = xpost_stack_next_segment(mem, s))
    {
        for (i = 0; i < s->top; i++)
        {
            Xpost_Memory_File *objmem;
            objmem = xpost_context_select_memory(ctx, s->data[i]);
            if (objmem == mem || markall)
                if (!_xpost_garbage_mark_object(ctx, objmem, s->data[i], markall))
                    return 0;
        }
    }

    return 1;
}

/* mark all allocations referred to by objects in save object's stack of saverec_'s */
static
int _xpost_garbage_mark_save_stack(Xpost_Context *ctx,
                                   Xpost_Memory_File *mem,
                                   unsigned int stackadr)
{
    if (!mem) return 0;

    {
        Xpost_Stack *s;
        unsigned int i;
        unsigned int ad;
        int ret;
        (void)ctx;

#ifdef DEBUG_GC
        printf("marking saverec stack of size %d\n", xpost_stack_count(mem, stackadr));
#endif

    for (s = xpost_stack_at(mem, stackadr); s;
         s = xpost_stack_next_segment(mem, s))
    {
        for (i = 0; i < s->top; i++)
        {
            /* saverec entity numbers may exceed a word; decode their full
               value and type through the accessors (see xpost_save.h)
               rather than reading the packed fields raw. */
            unsigned int rsrc = XPOST_SAVEREC_SRC(s->data[i]);
            unsigned int rcpy = XPOST_SAVEREC_CPY(s->data[i]);
            unsigned int rtype = XPOST_SAVEREC_TYPE(s->data[i]);
            /* _xpost_garbage_mark_object(ctx, mem, s->data[i]); */
            /* _xpost_garbage_mark_save_stack(ctx, mem, s->data[i].save_.stk); */
            ret = _xpost_garbage_mark_ent(mem, rsrc);
            if (!ret)
            {
                XPOST_LOG_ERR("cannot mark array");
                return 0;
            }
            ret = _xpost_garbage_mark_ent(mem, rcpy);
            if (!ret)
            {
                XPOST_LOG_ERR("cannot mark array");
                return 0;
            }
            if (rtype == dicttype)
            {
                ret = xpost_memory_table_get_addr(mem, rsrc, &ad);
                if (!ret)
                {
                    XPOST_LOG_ERR("cannot retrieve address for ent %u",
                                  rsrc);
                    return 0;
                }
                if (!_xpost_garbage_mark_dict(ctx, mem, ad, 0))
                    return 0;
                ret = xpost_memory_table_get_addr(mem, rcpy, &ad);
                if (!ret)
                {
                    XPOST_LOG_ERR("cannot retrieve address for ent %u",
                                  rcpy);
                    return 0;
                }
                if (!_xpost_garbage_mark_dict(ctx, mem, ad, 0))
                    return 0;
            }
            if (rtype == arraytype)
            {
                unsigned int sz = s->data[i].saverec_.pad;
                ret = xpost_memory_table_get_addr(mem, rsrc, &ad);
                if (!ret)
                {
                    XPOST_LOG_ERR("cannot retrieve address for array ent %u",
                                  rsrc);
                    return 0;
                }
                if (!_xpost_garbage_mark_array(ctx, mem, ad, sz, 0))
                    return 0;
                ret = xpost_memory_table_get_addr(mem, rcpy, &ad);
                if (!ret)
                {
                    XPOST_LOG_ERR("cannot retrieve address for array ent %u",
                                  rcpy);
                    return 0;
                }
                if (!_xpost_garbage_mark_array(ctx, mem, ad, sz, 0))
                    return 0;
            }
        }
    }
    }

    return 1;
}

/* mark all allocations referred to by objects in save stack */
static
int _xpost_garbage_mark_save(Xpost_Context *ctx,
                             Xpost_Memory_File *mem,
                             unsigned int stackadr)
{
    Xpost_Stack *s;
    unsigned int i;
    if (!mem) return 0;

#ifdef DEBUG_GC
    printf("marking save stack of size %d\n", xpost_stack_count(mem, stackadr));
#endif

    for (s = xpost_stack_at(mem, stackadr); s;
         s = xpost_stack_next_segment(mem, s))
    {
        for (i = 0; i < s->top; i++)
        {
            if (!_xpost_garbage_mark_save_stack(ctx, mem, s->data[i].save_.stk))
                return 0;
        }
    }
    return 1;
}

/* discard the free list.
   iterate through tables,
        if element is unmarked and not zero-sized,
            free it.
   return reclaimed size
 */
static
unsigned int _xpost_garbage_sweep(Xpost_Memory_File *mem)
{
    unsigned int zero = 0;
    unsigned int z;
    unsigned int i;
    unsigned int sz = 0;
    int quarantine = 0;

#ifdef WANT_DEBUG_HOOKS
    /* Quarantine: sweep nothing, so a freed entity is never handed out
       again. What that separates is a holder of a recycled entity, whose
       symptom goes away when nothing is recycled, from corruption in
       place, whose symptom does not.

       It is compiled in only for a build configured to want it
       (-Ddebug-hooks=true, --enable-debug-hooks), because the switch is
       read from the environment and the environment belongs to the host,
       not to the library. Reclamation is the whole of what a collector
       promises its host; a shipped library that could be told from a
       variable not to reclaim would let whoever can set one in the
       process turn a long-running embedding into unbounded growth, with
       nothing in the run saying so. A variable may configure a run --
       where the data is, how loud the log is, how often to collect --
       and it may ask for a diagnostic that only reports, as the ones
       below do. Taking a guarantee away is a build's decision. */
    if (getenv("XPOST_GC_NO_REUSE"))
        return 0;
    /* Withhold: reclaim as usual but never offer the entity again. An
       entity the free list hands back is opened again by the allocation
       that takes it, so a reference still held to it reads storage that
       is by then legitimately live and says nothing. Withholding leaves
       the stale read on storage nothing has taken, which a described
       arena reports against whoever read it. This is what makes such a
       read observable rather than merely wrong, and it is a build's
       decision for the same reason the switch above is. */
    quarantine = getenv("XPOST_GC_WITHHOLD") != NULL;
#endif

    z = xpost_memory_free_lists_adr(mem); /* address of the free list heads */

    /* discard the lists; previously-freed entities are gathered again */
    for (i = 0; i < XPOST_FREE_NBUCKETS; i++)
        memcpy(xpost_vm_ptr(mem, z + i * sizeof(unsigned int)), &zero, sizeof zero);

    {
    unsigned int bstat[XPOST_FREE_NBUCKETS] = {0};
    for (i = mem->start; i < mem->table.nextent; i++)
    {
        if (((mem->table.tab[i].mark & XPOST_MEMORY_TABLE_MARK_DATA_MARK_MASK) == 0) &&
            (mem->table.tab[i].sz != 0))
        {
            unsigned int bz;
            unsigned int b;
            /* a file is an entity in here and a struct outside, and this
               is the last reach anything has to either: give the struct
               up before the entity that points at it joins the free
               list, since the release writes through that same word. */
            if (mem->table.tab[i].tag == filetype)
                xpost_file_release_entity(mem, i);
            /* a handle is likewise an entity in here and a block
               outside, and this is the last reach anything has to
               either */
            if (mem->table.tab[i].tag & XPOST_MEMORY_TABLE_TAG_HANDLE)
                xpost_handle_release_entity(mem, i);
            mem->table.tab[i].tag = 0;
            if (quarantine)
            {
                XPOST_VG_POISON_ENT(mem->base, mem->table.tab[i].adr,
                                    mem->table.tab[i].sz);
                sz += mem->table.tab[i].sz;
                continue;
            }
            b = xpost_free_bucket_for_size(mem->table.tab[i].sz);
            bstat[b]++;
            bz = z + b * sizeof(unsigned int);
            memcpy(xpost_ent_ptr(mem, i), xpost_vm_ptr(mem, bz), sizeof(unsigned int));
            memcpy(xpost_vm_ptr(mem, bz), &i, sizeof(unsigned int));
            /* the entity has been reclaimed: a reference something still
               holds now reads storage the interpreter has taken back,
               which is the defect the arena is described for */
            XPOST_VG_POISON_ENT(mem->base, mem->table.tab[i].adr,
                                mem->table.tab[i].sz);
            sz += mem->table.tab[i].sz;
        }
    }
    if (getenv("XPOST_GC_BUCKETSTAT"))
    {
        fprintf(stderr, "BUCKETS:");
        for (i = 0; i < XPOST_FREE_NBUCKETS; i++)
            fprintf(stderr, " %u", bstat[i]);
        fprintf(stderr, "\n");
    }
    }

    return sz;
}

/* the independent reachability verifier and entity census live in
   xpost_garbage_diag.c; the collector calls them behind their
   XPOST_GC_* environment gates */

/* PLRM 3.7.2: an object in global VM must not reference an object in local
   VM -- with one interpreter-maintained exception (PLRM 3.7.7 and the note
   under systemdict): "systemdict, a global dictionary, contains several
   entries whose values are local dictionaries, such as userdict and $error."
   errordict, statusdict, serverdict and FontDirectory are local for the same
   reason. A local collection marks only local roots and does not descend the
   global systemdict, so these sanctioned local dictionaries -- reachable only
   through global systemdict -- would be swept and their storage recycled,
   corrupting the error machinery and other services on the next job. Mark the
   local values systemdict holds so the exception references keep them alive.
   No other global container may hold a local reference (invalidaccess bars
   it), so systemdict is the only one that needs this treatment. */
static int _xpost_garbage_mark_systemdict_exceptions(Xpost_Context *ctx,
                                                     Xpost_Memory_File *mem,
                                                     int markall)
{
    Xpost_Object sd;
    Xpost_Memory_File *sdmem;
    unsigned int adr, ent;
    dichead *dp;
    dicrec *tp;
    unsigned int n;
    unsigned int j;

    /* systemdict is the permanent bottom entry of the dictionary stack */
    sd = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);
    if (xpost_object_get_type(sd) != dicttype)
        return 1;
    sdmem = xpost_context_select_memory(ctx, sd);
    if (!sdmem || sdmem != ctx->gl)
        return 1; /* systemdict already covered if it is not global */
    ent = xpost_object_get_ent(sd);
    if (!xpost_ent_valid(sdmem, ent))
        return 1;
    adr = sdmem->table.tab[ent].adr;
    dp = xpost_dict_head_at(sdmem, adr);
    tp = xpost_dict_table_of(dp);
    /* the table is more than twice as long as the size it is derived
       from, so it is counted in a type wide enough for that product
       rather than in the type the size itself is stored in */
    n = DICTABN(dp->sz);
    for (j = 0; j < n; j++)
    {
        Xpost_Object v = tp[j].value;
        if (xpost_object_get_type(tp[j].key) == nulltype)
            continue;
        if (!xpost_object_is_composite(v))
            continue;
        /* only the local-VM values are the sanctioned exceptions; a global
           value is reached through the global roots, not swept here */
        if (xpost_context_select_memory(ctx, v) != mem)
            continue;
        if (!_xpost_garbage_mark_object(ctx, mem, v, markall))
            return 0;
    }
    return 1;
}

/*
   determine GLOBAL/LOCAL
   clear all marks,
   mark all root stacks,
   sweep.
   return reclaimed size or -1 if error occured.
 */
int xpost_garbage_collect(Xpost_Memory_File *mem, int dosweep, int markall)
{
    unsigned int i;
    unsigned int *cid;
    Xpost_Context *ctx = NULL;
    int isglobal;
    unsigned int sz = 0;
    unsigned int ad;

    if (mem->interpreter_get_initializing()) /* do not collect while initializing */
        return 0;

    /* printf("\ncollect:\n"); */

    /* determine global/local */
    isglobal = 0;
    ad = xpost_memory_context_list_adr(mem);
    cid = xpost_vm_ptr(mem, ad);
    for (i = 0; i < MAXCONTEXT && cid[i]; i++)
    {
        ctx = mem->interpreter_cid_get_context(cid[i]);
        if (ctx->state != 0)
        {
            if (mem == ctx->gl)
            {
                isglobal = 1;
                break;
            }
        }
    }
    if (ctx == NULL)
    {
        XPOST_LOG_ERR("cannot find context");
        return -1;
    }
#ifdef DEBUG_GC
    printf("using cid=%u\n", ctx->id);
#endif

    /* the walk begins with nothing queued and nothing waiting: a
       collection abandoned partway through leaves both behind, and this
       collection is not the one they were left for */
    _xpost_garbage_work_n = 0;
    _xpost_garbage_work_waiting = 0;
    _xpost_garbage_work_ready = 0;

    if (isglobal)
    {
        /* Global VM is not collected, so intermediate working storage
           belongs in local VM, which is. The rest of this branch is the
           global collection itself -- unmark, mark from the save and
           name stacks, then walk each context's local VM -- and nothing
           reaches it: the return is above it. It is parked here, beside
           the decision it belongs to, rather than being kept anywhere
           else; a reader meeting it should read it as not yet enabled
           rather than as something left behind. */
        dosweep = 0;
        return 0;

        _xpost_garbage_unmark(mem);

        ad = xpost_memory_save_stack_adr(mem);
        if (!_xpost_garbage_mark_save(ctx, mem, ad))
            return -1;
        ad = xpost_memory_name_stack_adr(mem);
        if (!_xpost_garbage_mark_names(ctx, mem, ad, markall))
            return -1;

        for (i = 0; i < MAXCONTEXT && cid[i]; i++)
        {
            ctx = mem->interpreter_cid_get_context(cid[i]);
            if (xpost_garbage_collect(ctx->lo, 0, markall) < 0)
                return -1;
        }

    }
    else /* local */
    {
        //printf("collect!\n");
        /* the name-lookup cache holds objects outside the root set;
           entities recycled by this collection must not be served from
           it afterwards */
        for (i = 0; i < MAXCONTEXT && cid[i]; i++)
        {
            Xpost_Context *cctx = mem->interpreter_cid_get_context(cid[i]);
            if (cctx)
                ++cctx->namebind_gen;
        }
        _xpost_garbage_unmark(mem);
        if (markall)
            _xpost_garbage_unmark(ctx->gl);

        ad = xpost_memory_save_stack_adr(mem);
        if (!_xpost_garbage_mark_save(ctx, mem, ad))
            return -1;
        ad = xpost_memory_name_stack_adr(mem);
#ifdef DEBUG_GC
        printf("marking name stack\n");
#endif
        if (!_xpost_garbage_mark_names(ctx, mem, ad, markall))
            return -1;

        for (i = 0; i < MAXCONTEXT && cid[i]; i++)
        {
            ctx = mem->interpreter_cid_get_context(cid[i]);

#ifdef DEBUG_GC
            printf("marking os\n");
#endif
            if (!_xpost_garbage_mark_stack(ctx, mem, ctx->os, markall))
                return -1;

#ifdef DEBUG_GC
            printf("marking ds\n");
#endif
            if (!_xpost_garbage_mark_stack(ctx, mem, ctx->ds, markall))
                return -1;

#ifdef DEBUG_GC
            printf("marking es\n");
#endif
            if (!_xpost_garbage_mark_stack(ctx, mem, ctx->es, markall))
                return -1;

#ifdef DEBUG_GC
            printf("marking hold\n");
#endif
            if (!_xpost_garbage_mark_stack(ctx, mem, ctx->hold, markall))
                return -1;
#ifdef DEBUG_GC
            printf("marking window device\n");
#endif
            if (!_xpost_garbage_mark_object(ctx, mem, ctx->window_device, markall))
                return -1;

            /* the page device the graphics state template names: rooted
               so its entity cannot be recycled while the interpreter
               still holds it to retire, the memory it holds outside
               virtual memory being reachable through that entity and
               nothing else */
            if (!_xpost_garbage_mark_object(ctx, mem, ctx->pagedevice, markall))
                return -1;

            /* privatedict holds the local machinery off the dict stack; root it
               here so its contents survive collection without a userdict anchor */
            if (!_xpost_garbage_mark_object(ctx, mem, ctx->privatedict, markall))
                return -1;

            /* the object being executed may exist only here */
            if (!_xpost_garbage_mark_object(ctx, mem, ctx->currentobject, markall))
                return -1;

            /* the file the run wrapped around its program: once the
               program has been read the run holds the only reference,
               and the run closes it when it ends */
            if (!_xpost_garbage_mark_object(ctx, mem, ctx->run_input_file, markall))
                return -1;

            /* the sanctioned global->local references: the local
               dictionaries systemdict holds (userdict, $error, errordict,
               statusdict, serverdict, FontDirectory) -- see PLRM 3.7.2 */
            if (!_xpost_garbage_mark_systemdict_exceptions(ctx, mem, markall))
                return -1;
#if 0
#ifdef DEBUG_GC
            printf("marking event handler\n");
#endif
            if (!_xpost_garbage_mark_object(ctx, mem, ctx->event_handler, markall))
                return -1;
#endif
        }
    }

    if (dosweep) {
#ifdef DEBUG_GC
        printf("sweep\n");
#endif
        if (!isglobal && getenv("XPOST_GC_VERIFY") && ctx)
            _xpost_garbage_diag_verify(ctx, mem);
        if (!isglobal && getenv("XPOST_GC_XBANK_CHECK") && ctx && ctx->gl)
            _xpost_garbage_diag_xbank(ctx, mem);
        
        sz += _xpost_garbage_sweep(mem);
        if (isglobal)
        {
            for (i = 0; i < MAXCONTEXT && cid[i]; i++)
            {
#ifdef DEBUG_GC
                printf("sweep context(%u)->gl\n", cid[i]);
#endif
                ctx = mem->interpreter_cid_get_context(cid[i]);
                sz += _xpost_garbage_sweep(ctx->lo);
            }
        }
    }

    XPOST_LOG_INFO("collect recovered %u bytes", sz);
    return sz;
}

#if 0

static
Xpost_Context *ctx;

static
int init_test_garbage(int (*xpost_interpreter_cid_init)(unsigned int *cid),
                      Xpost_Context *(*xpost_interpreter_cid_get_context)(unsigned int cid),
                      int (*xpost_interpreter_get_initializing)(void),
                      void (*xpost_interpreter_set_initializing)(int),
                      Xpost_Memory_File *(*xpost_interpreter_alloc_global_memory)(void),
                      Xpost_Memory_File *(*xpost_interpreter_alloc_local_memory)(void))
{
    int fd;
    unsigned int cid;
    char fname[] = "xmemXXXXXX";
    unsigned int tadr;
    int ret;
    unsigned int ent;

    /* create interpreter and context */
    itpdata = malloc(sizeof*itpdata);
    if (!itpdata) return 0;
    memset(itpdata, 0, sizeof*itpdata);
    ret = xpost_interpreter_cid_init(&cid);
    if (!ret)
        return 0;
    ctx = xpost_interpreter_cid_get_context(cid);
    ctx->id = cid;

    /* create global memory file */
    ctx->gl = xpost_interpreter_alloc_global_memory();
    if (ctx->gl == NULL)
    {
        return 0;
    }
    if (!xpost_mkstemp(fname, &fd))
    {
        return 0;
    }
    ret = xpost_memory_file_init(ctx->gl, fname, fd, xpost_interpreter_cid_get_context,
            xpost_interpreter_get_initializing, xpost_interpreter_set_initializing);
    if (!ret)
    {
        close(fd);
        return 0;
    }
    ret = xpost_memory_table_init(ctx->gl);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }
    ret = xpost_free_init(ctx->gl);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }
    ret = xpost_save_init(ctx->gl);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }
    ret = xpost_context_init_ctxlist(ctx->gl);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }
    xpost_context_append_ctxlist(ctx->gl, ctx->id);
    ctx->gl->start = XPOST_MEMORY_COLLECT_START_GLOBAL;

    /* create local memory file */
    ctx->lo = xpost_interpreter_alloc_local_memory();
    if (ctx->lo == NULL)
    {
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }
    strcpy(fname, "xmemXXXXXX");
    if (!xpost_mkstemp(fname, &fd))
    {
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }
    ret = xpost_memory_file_init(ctx->lo, fname, fd, xpost_interpreter_cid_get_context,
            xpost_interpreter_get_initializing, xpost_interpreter_set_initializing);
    if (!ret)
    {
        close(fd);
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }
    ret = xpost_memory_table_init(ctx->lo);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        xpost_memory_file_exit(ctx->lo);
        return 0;
    }
    ret = xpost_free_init(ctx->lo);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        xpost_memory_file_exit(ctx->lo);
        return 0;
    }
    ret = xpost_save_init(ctx->lo);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        xpost_memory_file_exit(ctx->lo);
        return 0;
    }
    ret = xpost_context_init_ctxlist(ctx->lo);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        xpost_memory_file_exit(ctx->lo);
        return 0;
    }
    xpost_context_append_ctxlist(ctx->lo, ctx->id);
    ctx->lo->start = XPOST_MEMORY_COLLECT_START_LOCAL;

    /* create names in both mfiles */
    ret = xpost_name_init(ctx);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        xpost_memory_file_exit(ctx->lo);
        return 0;
    }

    /* create global OPTAB */
    ctx->vmmode = GLOBAL;
    /*ret = initoptab(ctx);*/ /* NO! only need to allocate something in the table slot */
    ret = xpost_memory_table_alloc(ctx->gl, 1024, 0, &ent);
    if (!ret)
    {
        xpost_memory_file_exit(ctx->gl);
        xpost_memory_file_exit(ctx->lo);
        return 0;
    }
    /* ... no initop(). don't need operators for this. */

    /* only need one stack */
    ctx->vmmode = LOCAL;
    xpost_stack_init(ctx->lo, &ctx->hold);
    ctx->os = ctx->ds = ctx->es = ctx->hold;

    ctx->gl->interpreter_set_initializing(0); /* garbage collector won't run otherwise */

    return 1;
}

static
void exit_test_garbage(void)
{
    xpost_memory_file_exit(ctx->lo);
    xpost_memory_file_exit(ctx->gl);
    free(itpdata);
    itpdata = NULL;

    ctx->gl->interpreter_set_initializing(1);
}

static
int _clear_hold(Xpost_Context *_ctx)
{
    //Xpost_Stack *s = xpost_stack_at(_ctx->lo, _ctx->hold);
    //s->top = 0;
    xpost_stack_clear(_ctx->lo, _ctx->hold);
    return 1;
}

int test_garbage_collect(int (*xpost_interpreter_cid_init)(unsigned int *cid),
                         Xpost_Context *(*xpost_interpreter_cid_get_context)(unsigned int cid),
                         int (*xpost_interpreter_get_initializing)(void),
                         void (*xpost_interpreter_set_initializing)(int),
                         Xpost_Memory_File *(*xpost_interpreter_alloc_local_memory)(void),
                         Xpost_Memory_File *(*xpost_interpreter_alloc_global_memory)(void))
{
    if (!init_test_garbage(xpost_interpreter_cid_init,
                           xpost_interpreter_cid_get_context,
                           xpost_interpreter_get_initializing,
                           xpost_interpreter_set_initializing,
                           xpost_interpreter_alloc_local_memory,
                           xpost_interpreter_alloc_global_memory))
        return 0;

    {
        Xpost_Object str;
        unsigned int pre, post, sz, ret;

        pre = ctx->lo->used;
        str = xpost_string_cons(ctx, 7, "0123456");
        post = ctx->lo->used;
        sz = post-pre;
        /* printf("str sz=%u\n", sz); */

        xpost_stack_push(ctx->lo, ctx->os, str);
        _clear_hold(ctx);
        ret = collect(ctx->lo, 1, 0);
        //assert(ret == 0);
        if (ret != 0)
        {
            XPOST_LOG_ERR("Warning: collect returned %d, expected %d", ret, 0);
        }

        xpost_stack_pop(ctx->lo, ctx->os);
        _clear_hold(ctx);
        ret = collect(ctx->lo, 1, 0);
        /* printf("collect returned %u\n", ret); */
        //assert(ret >= sz);
        if (! (ret >= sz) )
        {
            XPOST_LOG_ERR("Warning: collect returned %d, expected >= %d", ret, sz);
        }
    }
    {
        Xpost_Object arr;
        unsigned int pre, post, sz, ret;

        pre = ctx->lo->used;
        arr = xpost_array_cons(ctx, 5);
        xpost_array_put(ctx, arr, 0, xpost_int_cons(12));
        xpost_array_put(ctx, arr, 1, xpost_int_cons(13));
        xpost_array_put(ctx, arr, 2, xpost_int_cons(14));
        xpost_array_put(ctx, arr, 3, xpost_string_cons(ctx, 5, "fubar"));
        xpost_array_put(ctx, arr, 4, xpost_string_cons(ctx, 4, "buzz"));
        post = ctx->lo->used;
        sz = post-pre;

        xpost_stack_push(ctx->lo, ctx->os, arr);
        _clear_hold(ctx);
        ret = collect(ctx->lo, 1, 0);
        //assert(ret == 0);
        if (ret != 0)
        {
            XPOST_LOG_ERR("Warning: collect returned %d, expected %d", ret, 0);
        }

        xpost_stack_pop(ctx->lo, ctx->os);
        _clear_hold(ctx);
        ret = collect(ctx->lo, 1, 0);
        //assert(ret >= sz);
        if (! (ret >= sz) )
        {
            XPOST_LOG_ERR("Warning: collect returned %d, expected >= %d", ret, sz);
        }

    }
    exit_test_garbage();
    return 1;
}

#endif

#ifdef TESTMODULE_GC

Xpost_Context *ctx;
Xpost_Memory_File *mem;
unsigned int stac;


/* void init(void) { */
/*     xpost_memory_file_init(&mem, "x.mem"); */
/*     (void)xpost_memory_table_init(&mem); */
/*     xpost_free_init(&mem); */
/*     xpost_save_init(&mem); */
/*     xpost_context_init_ctxlist(&mem); */
/*     Xpost_Memory_Table *tab = &mem->table; */
/*     unsigned int ent = xpost_memory_table_alloc(&mem, 0, 0); */
/*     stac = mem->table.tab[ent].adr = initstack(&mem); */
/*     /\* mem.roots[0] = XPOST_MEMORY_TABLE_SPECIAL_SAVE_STACK; *\/ */
/*     /\* mem.roots[1] = ent; *\/ */
/*     mem.start = ent+1; */
/* } */


extern Xpost_Interpreter *itpdata;

void init(void) {
    itpdata = malloc(sizeof*itpdata);
    memset(itpdata, 0, sizeof*itpdata);
    xpost_interpreter_init(itpdata);
}

int main(void)
{
    if (!xpost_init())
    {
        fprintf(stderr, "Fail to initialize xpost dict test\n");
        return -1;
    }

    init();
    printf("\n^test gc.c\n");
    ctx = &itpdata->ctab[0];
    mem = ctx->lo;
    stac = ctx->os;

    xpost_stack_push(mem, stac, xpost_int_cons(5));
    xpost_stack_push(mem, stac, xpost_int_cons(6));
    xpost_stack_push(mem, stac, xpost_real_cons(7.0));
    Xpost_Object ar;
    ar = xpost_array_cons_memory(mem, 3);
    int i;
    for (i=0; i < 3; i++)
        xpost_array_put_memory(mem, ar, i, xpost_stack_pop(mem, stac));
    xpost_stack_push(mem, stac, ar);                   /* array on stack */

    xpost_stack_push(mem, stac, xpost_int_cons(1));
    xpost_stack_push(mem, stac, xpost_int_cons(2));
    xpost_stack_push(mem, stac, xpost_int_cons(3));
    ar = xpost_array_cons_memory(mem, 3);
    for (i=0; i < 3; i++)
        xpost_array_put_memory(mem, ar, i, xpost_stack_pop(mem, stac));
    xpost_object_dump(ar);
    /* array not on stack */

#define CNT_STR(x) sizeof(x), x
    xpost_stack_push(mem, stac, xpost_string_cons_memory(mem, CNT_STR("string on stack")));

    xpost_object_dump(xpost_string_cons_memory(mem, CNT_STR("string not on stack")));

    collect(mem);
    xpost_stack_push(mem, stac, xpost_string_cons_memory(mem, CNT_STR("string on stack")));
    xpost_object_dump(xpost_string_cons_memory(mem, CNT_STR("string not on stack")));

    collect(mem);
    xpost_memory_file_dump(mem);
    printf("stackaedr: %04x\n", stac);
    dumpmtab(mem, 0);
    /*     ^ent 8 (8): adr 3404 0x0d4c, sz [24], mark _ */
    /*     ^ 06  00  00  00  6en 67g 20  6en 6fo 74t 20 */
    printf("gc: look at the mark field . . . . . . . .^\n");
    printf("also, see that the first 4 bytes of strings not on stack\n"
           "have been obliterated to link-up the free list.\n");

    xpost_quit();

// WTF???  }
    return 0;
}

#endif
