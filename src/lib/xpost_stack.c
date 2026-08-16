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

#include <stdlib.h> /* NULL */

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_object.h"
#include "xpost_memory.h"
#include "xpost_error.h"
#include "xpost_stack.h"

/*
 * The stack type is a chain of segments.
 *
 * root->prevseg == top segment
 * tail->nextseg == 0
 *

typedef struct
{
    unsigned int nextseg;
    unsigned int prevseg;
    unsigned int top;
    Xpost_Object data[XPOST_STACK_SEGMENT_SIZE];
} Xpost_Stack;
*/

/* allocate memory for one stack segment */
XPOST_TEST_VISIBLE int xpost_stack_init(Xpost_Memory_File *mem,
                                unsigned int *paddr)
{
    unsigned int adr;
    Xpost_Stack *s;

    if (!xpost_memory_file_alloc(mem, sizeof(Xpost_Stack), &adr))
    {
        XPOST_LOG_ERR("cannot allocate a stack segment");
        return 0;
    }
    s = xpost_stack_at(mem, adr);
    s->nextseg = 0;
    s->prevseg = adr;
    s->top = 0;
    *paddr = adr;
    return 1;
}

void xpost_stack_clear(Xpost_Memory_File *mem,
                       unsigned int stackadr)
{
    Xpost_Stack *s = xpost_stack_at(mem, stackadr);
    s->top = 0;
    s->prevseg = stackadr;
}

void xpost_stack_dump(Xpost_Memory_File *mem,
                      unsigned int stackadr)
{
    Xpost_Stack *s = xpost_stack_at(mem, stackadr);
    unsigned int i;
    unsigned int a;

    a = 0;
    while (1)
    {
        for (i = 0; i < s->top; i++)
        {
            XPOST_LOG_DUMP("%d:", a++);
            xpost_object_dump(s->data[i]);
        }
        if (i != XPOST_STACK_SEGMENT_SIZE)
            break;
        if (s->nextseg == 0)
            break;
        s = xpost_stack_step(mem, s->nextseg);
    }
}

/* deallocate stack segment and any chained segments */
int xpost_stack_count(Xpost_Memory_File *mem,
                      unsigned int stackadr)
{
    Xpost_Stack *s = xpost_stack_at(mem, stackadr);
    unsigned int ct = 0;
    while (s->top == XPOST_STACK_SEGMENT_SIZE)
    {
        ct += XPOST_STACK_SEGMENT_SIZE;
        if (s->nextseg == 0)
            return ct; /* a full segment with no successor is a legal topmost state */
        s = xpost_stack_step(mem, s->nextseg);
    }
    return ct + s->top;
}

XPOST_TEST_VISIBLE int xpost_stack_push(Xpost_Memory_File *mem,
                                unsigned int stackadr,
                                Xpost_Object obj)
{
    Xpost_Stack *root = xpost_stack_at(mem, stackadr);
    Xpost_Stack *s = xpost_stack_at(mem, root->prevseg); /* load top segment */

    if (xpost_object_get_type(obj) == invalidtype)
        return 0;

    /* the segment is left in place when a push fills it, so the top
       segment never rests empty with values below it: direct segment
       accesses can rely on root->prevseg holding the topmost value.
       move to (or link) the next segment when pushing into a full one. */
    if (s->top == XPOST_STACK_SEGMENT_SIZE)
    {
        if (s->nextseg == 0)
        {
            size_t stadr;
            unsigned int newst;
            int ret;

            stadr = (unsigned char *)s - mem->base;
            ret = xpost_stack_init(mem, &newst);
            if (!ret)
            {
                /* the object is on no stack and the caller is several
                   hundred sites that do not carry the answer back; the
                   memory file holds the refusal until the dispatch or
                   the interpreter's safe point reads it. A declined
                   segment is the virtual memory machinery refusing, which
                   PLRM 8.2 gives VMerror for -- unlike a push of an
                   object that was never made, below, which says nothing
                   about memory and is left to say nothing here. */
                mem->push_refused = 1;
                return 0;
            }
            s = xpost_stack_at(mem, stadr);
            root = xpost_stack_at(mem, stackadr);
            s->nextseg = newst;
            (xpost_stack_at(mem, newst))->prevseg = stadr;
        }
        root->prevseg = s->nextseg;
        s = xpost_stack_step(mem, s->nextseg);
        s->top = 0;
    }

    s->data[s->top++] = obj; /* push value */

    return 1;
}

Xpost_Object xpost_stack_topdown_fetch(Xpost_Memory_File *mem,
                                       unsigned int stackadr,
                                       int idx)
{
    int i = idx;
    Xpost_Stack *s = xpost_stack_at(mem, stackadr);

    if (s->prevseg) s = xpost_stack_at(mem, s->prevseg); /* find top seg */

    while (i >= (signed)(s->top)){
        i -= s->top;
        if (s == xpost_stack_at(mem, stackadr)){
            XPOST_LOG_ERR("%d can't find stack segment for index -%d in stack of size %u",
                    unregistered, idx,
                    xpost_stack_count(mem, stackadr));
            return invalid;
        }
        s = xpost_stack_step(mem, s->prevseg);
    }
    return s->data[s->top - 1 - i];
}

int xpost_stack_topdown_replace(Xpost_Memory_File *mem,
                                unsigned int stackadr,
                                int idx,
                                Xpost_Object obj)
{
    int i = idx;
    Xpost_Stack *s = xpost_stack_at(mem, stackadr);
    if (s->prevseg) s = xpost_stack_at(mem, s->prevseg); /* find top seg */

    while (i >= (signed)(s->top)){
        i -= s->top;
        if (s == xpost_stack_at(mem, stackadr)){
            XPOST_LOG_ERR("%d can't find stack segment for index -%d in stack of size %u",
                    unregistered, idx,
                    xpost_stack_count(mem, stackadr));
            return 0;
        }
        s = xpost_stack_step(mem, s->prevseg);
    }
    s->data[s->top - 1 - i] = obj;
    return 1;
}

int xpost_stack_topdown_find_type(Xpost_Memory_File *mem,
                                  unsigned int stackadr,
                                  int type,
                                  Xpost_Object *out)
{
    unsigned char *base = mem->base;
    Xpost_Stack *root = (Xpost_Stack *)(base + stackadr);
    Xpost_Stack *seg = (Xpost_Stack *)(base + root->prevseg); /* top segment */
    int idx = 0;

    /* Walk the segment chain once from the top rather than calling
       topdown_fetch per index -- each of those re-walks the chain, so a scan
       of the whole stack was O(n^2). Here each element is visited once. */
    for (;;)
    {
        int k;
        for (k = (int)seg->top; k-- > 0; )
        {
            if ((int)xpost_object_get_type(seg->data[k]) == type)
            {
                if (out)
                    *out = seg->data[k];
                return idx;
            }
            idx++;
        }
        if (seg == root)
            break;
        seg = xpost_stack_step(mem, seg->prevseg);
    }
    return -1;
}

int xpost_stack_peek_top(Xpost_Memory_File *mem,
                         unsigned int stackadr,
                         int n,
                         Xpost_Object *out)
{
    unsigned char *base = mem->base;
    Xpost_Stack *root = (Xpost_Stack *)(base + stackadr);
    Xpost_Stack *seg = (Xpost_Stack *)(base + root->prevseg); /* top segment */
    int got = 0;

    /* One top-down pass: out[0] is the topmost element. Fetching each of the
       top n with xpost_stack_topdown_fetch would re-walk the segment chain per
       index and be O(n^2) on a multi-segment stack. */
    while (got < n)
    {
        int t = (int)seg->top;
        int take = (n - got < t) ? (n - got) : t;
        int m;
        for (m = 0; m < take; m++)
            out[got + m] = seg->data[t - 1 - m];
        got += take;
        if (got < n)
            seg = xpost_stack_step(mem, seg->prevseg);
    }
    return got;
}

Xpost_Object xpost_stack_bottomup_fetch(Xpost_Memory_File *mem,
                                        unsigned int stackadr,
                                        int idx)
{
    Xpost_Stack *root = xpost_stack_at(mem, stackadr);
    Xpost_Stack *s = root;
    int i = idx;

    /* find desired segment */
    while (i >= XPOST_STACK_SEGMENT_SIZE)
    {
        i -= XPOST_STACK_SEGMENT_SIZE;
        if (s->nextseg == 0)
        {
            XPOST_LOG_ERR("%d can't find stack segment for index %d in stack of size %u",
                    unregistered, idx,
                    xpost_stack_count(mem, stackadr));
            return invalid;
        }
        s = xpost_stack_step(mem, s->nextseg);
    }
    if (i >= (signed)s->top){
        return invalid;
    }
    return s->data[i];
}

int xpost_stack_bottomup_replace(Xpost_Memory_File *mem,
                                 unsigned int stackadr,
                                 int idx,
                                 Xpost_Object obj)
{
    Xpost_Stack *root = xpost_stack_at(mem, stackadr);
    Xpost_Stack *s = root;
    int i = idx;

    /* find desired segment */
    while (i >= XPOST_STACK_SEGMENT_SIZE)
    {
        i -= XPOST_STACK_SEGMENT_SIZE;
        if (s->nextseg == 0)
        {
            XPOST_LOG_ERR("%d can't find stack segment for index %d in stack of size %u",
                          unregistered, idx,
                          xpost_stack_count(mem, stackadr));
            return 0;
        }
        s = xpost_stack_step(mem, s->nextseg);
    }
    if (i >= (signed)s->top){
        return 0;
    }
    s->data[i] = obj;
    return 1;
}

XPOST_TEST_VISIBLE Xpost_Object xpost_stack_pop(Xpost_Memory_File *mem,
                                        unsigned int stackadr)
{
    Xpost_Stack *root = xpost_stack_at(mem, stackadr);
    Xpost_Stack *s = xpost_stack_at(mem, root->prevseg); /* load top seg */
    Xpost_Object val;

    if (s->top == 0) /* back up if top is empty */
    {
        if (s != root)
        {
            unsigned int soff = s->prevseg;
            s = xpost_stack_step(mem, soff);
            root->prevseg = soff; // update root->top
            if (s->top == 0)
                return invalid;
        }
        else /* can't back up if stack is empty */
        {
            return invalid;
        }
    }

    val = s->data[--s->top]; /* pop value */

    /* retreat eagerly when the segment empties, maintaining the
       invariant that the top segment holds the topmost value */
    if (s->top == 0 && s != root)
        root->prevseg = s->prevseg;

    return val;
}
