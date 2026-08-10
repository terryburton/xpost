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
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h" /* Xpost_Memory_File */
#include "xpost_object.h" /* Xpost_Object */
#include "xpost_file.h" /* Xpost_File: what a file entity holds */
#include "xpost_handle.h" /* what a handle entity holds */
#include "xpost_free.h"

/*
   initialize the free-list in the memory file.
   free list head is in slot zero
   sz is 0 so gc will ignore it */
/* collection threshold in allocated bytes; overridable for testing
   and for embedders that want more frequent collections */
static int _xpost_free_gc_threshold(void)
{
    static int v = -1;
    if (v < 0)
    {
        const char *e = getenv("XPOST_GC_THRESHOLD");
        v = e ? atoi(e) : 0;
        if (v <= 0)
            v = XPOST_GARBAGE_COLLECTION_THRESHOLD;
    }
    return v;
}

/* The free list is segregated into size-class buckets so that both
   freeing and allocation are near-constant-time: a single sorted list
   makes every operation walk the entities smaller than the request,
   which dominates once a large collection has populated the list.
   Bucket b holds entities with size in [2^(b+4), 2^(b+5)), clamped to
   the first and last buckets. The head words live in the FREE special
   entity's data area. */
/* the bucket map and count live in xpost_free.h, shared with the sweep */

/* Write a bucket head.

   The FREE entity records a size of zero rather than the size of the
   area it owns, so that a composite object left holding entity zero --
   which is what an object that was never constructed holds -- cannot
   write through it: every bounded accessor refuses an entity of no
   size, and the thousand bytes behind this one are there to be the
   thing such a write would otherwise land in.

   What that costs is this file's own access to the words it keeps
   there: the accessors refuse the free list its heads for exactly the
   reason they refuse everybody else. So a head is reached at the
   address the entity records instead, which is the route the push
   below, the walk in xpost_free_alloc and the collector's sweep all
   take. Bounds are met by construction rather than by asking -- b is
   below XPOST_FREE_NBUCKETS, and the area allocated for the heads is
   many times the sixteen words they come to. */
static void _xpost_free_bucket_head_set(Xpost_Memory_File *mem,
                                        unsigned int b,
                                        unsigned int ent)
{
    memcpy(xpost_vm_ptr(mem, xpost_memory_free_lists_adr(mem)
                             + b * (unsigned int)sizeof(unsigned int)),
           &ent, sizeof ent);
}

int xpost_free_init(Xpost_Memory_File *mem)
{
    unsigned int ent;
    int ret;

    /* allocate the free list head: 4 bytes in ent 0
       allocate additional 1k "scratch" space to protect
       interpreter data from NULL writes
     */
    ret = xpost_memory_table_alloc(mem, 1024, 0, &ent);
    if (!ret)
    {
        return 0;
    }

    /* make sure this is the correct ent */
    assert (ent == XPOST_MEMORY_TABLE_SPECIAL_FREE);

    /* set all bucket heads to zero (== NULL == end of list) */
    {
        unsigned int b;
        for (b = 0; b < XPOST_FREE_NBUCKETS; b++)
            _xpost_free_bucket_head_set(mem, b, 0);
    }

    /* record no size, so that a write through entity zero is refused
       rather than landing on interpreter data -- see the head writer
       above for what the guard is and what it costs */
    {
        Xpost_Memory_Table *tab = &mem->table;
        tab->tab[XPOST_MEMORY_TABLE_SPECIAL_FREE].sz = 0;
    }

    /* make free list available for general memory allocations */
    (void) xpost_memory_register_free_list_alloc_function(mem, xpost_free_alloc);
    mem->period = XPOST_GARBAGE_COLLECTION_PERIOD;
    mem->threshold = _xpost_free_gc_threshold();

    return 1;
}

#ifdef XPOST_VALGRIND_ARENA
/* Close every entity the free lists hold again.
 *
 * A grow reopens the whole extent, because the file copies it forward
 * and zeroes the part above the high-water mark, and the host allocator
 * hands back a block that is accessible throughout in any case. The
 * entities the collector has reclaimed are exactly the ones the free
 * lists chain -- which the table cannot say, a freed entity carrying
 * the same zero tag as a live raw allocation -- so they are read back
 * from the lists themselves and closed again here.
 */
void xpost_free_repoison(Xpost_Memory_File *mem)
{
    unsigned int headz;
    unsigned int b;
    unsigned int rows;

    if (!mem || !mem->base)
        return;
    headz = xpost_memory_free_lists_adr(mem);
    /* no chain can hold more entities than the table has rows, which is
       the bound the walk below is held to */
    rows = mem->table.nextent;
    for (b = 0; b < XPOST_FREE_NBUCKETS; b++)
    {
        unsigned int e;
        unsigned int seen = 0;

        memcpy(&e, xpost_vm_ptr(mem, headz + b * sizeof(unsigned int)),
               sizeof(unsigned int));
        /* the walk is bounded by the table it indexes, so a link spoiled
           by a stale write cannot spin it */
        while (e && xpost_ent_valid(mem, e) && seen <= rows)
        {
            unsigned int a = mem->table.tab[e].adr;
            unsigned int s = mem->table.tab[e].sz;

            ++seen;
            memcpy(&e, xpost_vm_ptr(mem, a), sizeof(unsigned int));
            XPOST_VG_POISON_ENT(mem->base, a, s);
        }
    }
}
#endif

/* free this ent! returns reclaimed size or -1 on error */
int xpost_free_memory_ent(Xpost_Memory_File *mem,
                          unsigned int ent)
{
    Xpost_Memory_Table *tab;
    unsigned int rent = ent; /* relative ent index */
    unsigned int z; /* free list pointer */
    unsigned int a; /* adr associated with ent */
    unsigned int sz; /* sz associated with adr */
    int ret;
    /* return; */

    if (ent < mem->start)
        return 0;

    if (!xpost_ent_valid(mem, ent))
    {
        XPOST_LOG_ERR("cannot free ent %u", ent);
        return -1;
    }
    tab = &mem->table;
    a = tab->tab[rent].adr;
    sz = tab->tab[rent].sz;
    if (sz == 0) return 0; /* do not add zero-size allocations to list */

    if (tab->tab[rent].tag == filetype)
    {
        Xpost_File *fp;

        /* retire this file from its birth-stamp bucket */
        {
            unsigned int b = (tab->tab[rent].mark
                              & XPOST_MEMORY_TABLE_MARK_DATA_LOWLEVEL_MASK)
                             >> XPOST_MEMORY_TABLE_MARK_DATA_LOWLEVEL_OFFSET;

            if (b < 256 && mem->file_births[b] > 0)
            {
                mem->file_births[b]--;
                while (mem->file_birth_max > 0
                    && mem->file_births[mem->file_birth_max] == 0)
                    mem->file_birth_max--;
            }
        }
        /* A file entity holds an Xpost_File *, the stream abstraction --
           not the stdio FILE * it once held. Reading it back as the type
           it is means the two cannot be confused; a stream is closed
           through its own method table, never by fclose on a pointer that
           merely happens to be the same width.

           Reaching here with a live stream would be a caller's mistake,
           not a case to handle: the entity is only offered for reclaim
           once its stream has been closed and its pointer cleared, which
           is why the one caller that can present a file entity tests for
           NULL before asking. Say so and decline, rather than guess at a
           close for a stream something else still believes it owns. */
        ret = xpost_memory_get(mem, ent, 0, sizeof fp, &fp);
        if (!ret)
        {
            XPOST_LOG_ERR("cannot load the stream of file ent %u", ent);
            return -1;
        }
        if (fp)
        {
            XPOST_LOG_ERR("refusing to reclaim file ent %u: its stream is "
                          "still open", ent);
            return -1;
        }
    }
    /* a handle is an entity in here and a block outside, and the
       entity is on its way to the free list */
    if (tab->tab[rent].tag & XPOST_MEMORY_TABLE_TAG_HANDLE)
        xpost_handle_release_entity(mem, ent);
    tab->tab[rent].tag = 0;

    z = xpost_memory_free_lists_adr(mem);
    z += xpost_free_bucket_for_size(sz) * sizeof(unsigned int);

    /* push onto the bucket: link word lives in the ent's data area */
    memcpy(xpost_vm_ptr(mem, a), xpost_vm_ptr(mem, z), sizeof(unsigned int));
    memcpy(xpost_vm_ptr(mem, z), &ent, sizeof(unsigned int));
    XPOST_VG_POISON_ENT(mem->base, a, sz);

    return sz;
}

static void _dump_chain(Xpost_Memory_File *mem, unsigned int z)
{
    unsigned int e;
    memcpy(&e, xpost_vm_ptr(mem, z), sizeof(unsigned int));
    while (e)
    {
        unsigned int sz;
        if (!xpost_memory_table_get_size(mem, e, &sz)) return;
        printf("%u(%u) ", e, sz);
        if (!xpost_memory_table_get_addr(mem, e, &z)) return;
        memcpy(&e, xpost_vm_ptr(mem, z), sizeof(unsigned int));
    }
}

/* print a dump of the free list */
void xpost_free_dump(Xpost_Memory_File *mem)
{
    unsigned int e;
    unsigned int z;
    unsigned int b;
    unsigned int headz;

    headz = xpost_memory_free_lists_adr(mem);

    printf("freelist: ");
    for (b = 0; b < XPOST_FREE_NBUCKETS; b++)
    {
        z = headz + b * sizeof(unsigned int);
        memcpy(&e, xpost_vm_ptr(mem, z), sizeof(unsigned int));
        if (e) printf("[bucket %u] ", b);
        _dump_chain(mem, z);
    }
}

/* scan the free list for a suitably-sized bit of memory,

   if the allocator falls back to fresh memory XPOST_GARBAGE_COLLECTION_PERIOD times,
        it triggers a collection.
    Returns 1 on success, 0 on failure, 2 to request garbage collection and re-call.
 */
int xpost_free_alloc(Xpost_Memory_File *mem,
                     unsigned int sz,
                     unsigned int tag,
                     unsigned int *entity)
{
    unsigned int z;
    unsigned int e;                     /* working pointer */
    //static int period = XPOST_GARBAGE_COLLECTION_PERIOD;
    //static int threshold = XPOST_GARBAGE_COLLECTION_THRESHOLD;
    int ret;

    if (!mem->interpreter_get_initializing())
    {
#ifdef XPOST_USE_THRESHOLD
        if ((mem->threshold -= sz) <= 0)
        {
            mem->threshold = _xpost_free_gc_threshold();
            return XPOST_FREE_WANT_COLLECTION;
        }
#else
        //(void)threshold;
        if (--mem->period == 0) /* check garbage-collection control */
        {
            mem->period = XPOST_GARBAGE_COLLECTION_PERIOD;
            return XPOST_FREE_WANT_COLLECTION; /* not found; try again after collecting */
            /* collect(mem, 1, 0); */
            /* goto try_again; */
        }
#endif
    }

    z = xpost_memory_free_lists_adr(mem); /* free pointer */

    {
    unsigned int b;
    unsigned int headz = z;

    for (b = xpost_free_bucket_for_size(sz); b < XPOST_FREE_NBUCKETS; b++)
    {
        unsigned int best = 0, bestz = 0, bestsz = 0;
        unsigned int seen = 0;

        z = headz + b * sizeof(unsigned int);
        memcpy(&e, xpost_vm_ptr(mem, z), sizeof(unsigned int));
        while (e && seen < XPOST_FREE_SCAN_LIMIT) /* e is not zero */
        {
            unsigned int tsz;
            unsigned int ta;

            ++seen;
            /* saturating, so a count this large cannot present itself
               as a small one to whatever is reading it */
            if (mem->free_scan < (unsigned int)INT_MAX)
                ++mem->free_scan;

            /* The links live inside the freed entities' data, where a stale
               write can turn one into an arbitrary number. Handing out an
               entity that is not actually free aliases two owners onto one
               allocation, so validate every node: freed entities carry a
               zero tag. On any inconsistency discard the lists and request
               a collection to rebuild them. */
            if (e > XPOST_OBJECT_COMP_MAX_ENT ||
                !xpost_ent_valid(mem, e) ||
                mem->table.tab[e].tag != 0)
            {
                unsigned int bb;
                XPOST_LOG_ERR("free list corrupt at ent %u (tag %u): discarding",
                        e, xpost_ent_valid(mem, e) ? mem->table.tab[e].tag : 0);
                /* Every bucket, not just this one: a write that spoiled
                   one link says nothing about the others, and a bucket
                   left standing is a later walk back into the same
                   state. The heads are written at their address for the
                   reason given where that writer is defined. Nothing
                   here can fail, so the request for a collection is not
                   conditional on it -- and it must not be, because
                   returning a plain failure would leave the caller
                   allocating afresh with the lists never rebuilt. */
                for (bb = 0; bb < XPOST_FREE_NBUCKETS; bb++)
                    _xpost_free_bucket_head_set(mem, bb, 0);
                return XPOST_FREE_WANT_COLLECTION; /* refill the list first */
            }
            ret = xpost_memory_table_get_size(mem, e, &tsz);
            if (!ret)
            {
                XPOST_LOG_ERR("cannot retrieve size of ent %u", e);
                return 0;
            }

            /* Best fit among the entries this bucket is allowed to
               offer: entity numbers are a fixed budget, so near-exact
               recycling matters more than the byte waste an oversized
               entry leaves, which a later collection reclaims. An
               exact fit ends the search outright; otherwise the
               closest of the first XPOST_FREE_SCAN_LIMIT entries is
               taken, and the rest of the chain -- whose length is the
               job's release history rather than anything about this
               request -- is left unwalked. */
            if (tsz >= sz && (best == 0 || tsz < bestsz))
            {
                best = e;
                bestz = z;
                bestsz = tsz;
                if (tsz == sz)
                    break;
            }

            ret = xpost_memory_table_get_addr(mem, e, &ta);
            if (!ret)
            {
                XPOST_LOG_ERR("cannot retrieve address for ent %u", e);
                return 0;
            }
            z = ta;
            memcpy(&e, xpost_vm_ptr(mem, z), sizeof(unsigned int));
        }

        if (best)
        {
            Xpost_Memory_Table *tab = &mem->table;
            unsigned int ad;

            ret = xpost_memory_table_get_addr(mem, best, &ad);
            if (!ret)
            {
                XPOST_LOG_ERR("cannot retrieve address of ent %u", best);
                return 0;
            }
            /* unlink: the predecessor link slot was recorded when the
               node was reached */
            memcpy(xpost_vm_ptr(mem, bestz), xpost_vm_ptr(mem, ad), sizeof(unsigned int));
            /* the entity is being handed out again: its storage is
               readable once more, and holds nothing yet */
            XPOST_VG_UNPOISON_ENT(mem->base, ad, bestsz);
            tab->tab[best].tag = tag;
            *entity = best;
            return 1; /* found, return SUCCESS */
        }
    }
    }
    /* finished scanning free list */

    return 0; /* not found, fall-back to _new allocator */
}

/*
   use the free-list and tables to now provide a realloc for
   "raw" vm addresses (mem->base offsets rather than ents).

   Allocate new entry, copy data, steal its adr, stash old adr, free it.

   Currently this is only used to re-size signature blocks in the operator table.
 */
unsigned int xpost_free_realloc(Xpost_Memory_File *mem,
                                unsigned int oldadr,
                                unsigned int oldsize,
                                unsigned int newsize)
{
    Xpost_Memory_Table *tab = NULL;
    unsigned int newadr;
    unsigned int ent;
    unsigned int rent; /* relative ent */
    int ret;

#ifdef DEBUGFREE
    printf("xpost_free_realloc: ");
    printf("initial ");
    xpost_free_dump(mem);
#endif

    /* allocate new entry */
    ret = xpost_memory_table_alloc(mem, newsize, 0, &ent);
    if (!ret)
    {
        XPOST_LOG_ERR("cannot allocate new memory");
        return 0;
    }
    rent = ent;
    tab = &mem->table;
    if (!xpost_ent_valid(mem, ent))
    {
        XPOST_LOG_ERR("cannot find table for ent %u", ent);
        return 0;
    }

    /* steal its adr */
    newadr = tab->tab[rent].adr;

    /* copy data */
    memcpy(xpost_vm_ptr(mem, newadr), xpost_vm_ptr(mem, oldadr), oldsize);

    /* stash old adr */
    tab->tab[rent].adr = oldadr;
    tab->tab[rent].sz = oldsize;

    /* free it. The entity was allocated moments ago in this same
       function, so the list can take it back. */
    XPOST_REFUSAL_IMPOSSIBLE(xpost_free_memory_ent(mem, ent));

#ifdef DEBUGFREE
    printf("final ");
    xpost_free_dump(mem);
    printf("\n");
    dumpmtab(mem, 0);
    fflush(NULL);
#endif

    return newadr;
}
