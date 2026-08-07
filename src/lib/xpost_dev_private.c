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

#include <stdlib.h>
#include <string.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_context.h"
#include "xpost_error.h"
#include "xpost_dict.h"
#include "xpost_string.h"
#include "xpost_dev_private.h"

/* One issued block: the entity carrying its handle, the instance
   dictionary it was issued to, and what it holds. */
typedef struct
{
    Xpost_Memory_File *mem;      /* memory file of the handle entity */
    unsigned int ent;            /* handle entity; zero marks a free slot */
    Xpost_Memory_File *ownermem; /* memory file of the instance dictionary */
    unsigned int owner;          /* entity of the instance dictionary */
    unsigned int size;           /* bytes the block holds */
    void *block;
} Xpost_Dev_Private_Slot;

/* The record of what has been issued. Slot zero is never issued, so the
   handle a string carries before anything is written into it -- and the
   handle in a string of the program's own making -- names nothing. */
static Xpost_Dev_Private_Slot *_slots;
static unsigned int _nslots;

/* Take a free slot, growing the record when none is left. Returns zero
   when there is no memory for one. */
static unsigned int _slot_alloc(void)
{
    unsigned int i;
    unsigned int max;
    void *tmp;

    for (i = 1; i < _nslots; i++)
        if (_slots[i].ent == 0)
            return i;

    max = _nslots ? _nslots * 2 : 8;
    tmp = realloc(_slots, max * sizeof(*_slots));
    if (!tmp)
        return 0;
    _slots = (Xpost_Dev_Private_Slot *)tmp;
    memset(&_slots[_nslots], 0, (max - _nslots) * sizeof(*_slots));
    i = _nslots ? _nslots : 1;
    _nslots = max;
    return i;
}

/* The slot an entity's handle names, or NULL. The handle is read from
   the entity rather than from the object naming it, so a substring and
   the string it came from answer the same, and it is checked back
   against the entity it was read from: a handle carrying the number of
   a slot issued elsewhere names that slot's entity, not this one. */
static Xpost_Dev_Private_Slot *_slot_of(Xpost_Memory_File *mem,
                                        unsigned int ent)
{
    unsigned int index;

    if (!xpost_memory_get(mem, ent, 0, sizeof(index), &index))
        return NULL;
    if ((index == 0) || (index >= _nslots))
        return NULL;
    if ((_slots[index].mem != mem) || (_slots[index].ent != ent))
        return NULL;
    return &_slots[index];
}

int xpost_dev_private_cons(Xpost_Context *ctx,
                           Xpost_Object devdic,
                           Xpost_Object key,
                           Xpost_Object *anchor,
                           size_t size)
{
    Xpost_Memory_File *mem;
    Xpost_Object o;
    unsigned int index;
    unsigned int ent;
    unsigned int tag;
    int owner;
    void *block;

    owner = xpost_object_get_ent(devdic);
    if (owner < 0)
        return unregistered;

    index = _slot_alloc();
    if (index == 0)
    {
        XPOST_LOG_ERR("cannot record a device's private state");
        return VMerror;
    }
    block = calloc(1, size);
    if (!block)
    {
        XPOST_LOG_ERR("cannot allocate a device's private state");
        return VMerror;
    }

    /* the handle is read-only to the program: what it names is checked
       either way, and a handle that cannot be overwritten in place is
       one fewer thing for the check to answer */
    o = xpost_object_cvlit(xpost_string_cons(ctx, sizeof(index), NULL));
    if (xpost_object_get_type(o) != stringtype)
    {
        free(block);
        XPOST_LOG_ERR("cannot allocate a device's private handle");
        return VMerror;
    }
    mem = xpost_context_select_memory(ctx, o);
    ent = (unsigned int)xpost_object_get_ent(o);
    if (!xpost_memory_put(mem, ent, 0, sizeof(index), &index) ||
        !xpost_memory_table_get_tag(mem, ent, &tag) ||
        !xpost_memory_table_set_tag(mem, ent,
                                    tag | XPOST_MEMORY_TABLE_TAG_DEVICE_PRIVATE))
    {
        free(block);
        XPOST_LOG_ERR("cannot store a device's private handle");
        return VMerror;
    }

    _slots[index].mem = mem;
    _slots[index].ent = ent;
    _slots[index].ownermem = xpost_context_select_memory(ctx, devdic);
    _slots[index].owner = (unsigned int)owner;
    _slots[index].size = (unsigned int)size;
    _slots[index].block = block;

    o = xpost_object_set_access(ctx, o, XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
    *anchor = o;
    return xpost_dict_put(ctx, devdic, key, o);
}

/* The slot a handle names, of the size asked for. */
static Xpost_Dev_Private_Slot *_slot_named(Xpost_Context *ctx,
                                           Xpost_Object anchor,
                                           size_t size)
{
    Xpost_Dev_Private_Slot *slot;
    int ent;

    if (xpost_object_get_type(anchor) != stringtype)
        return NULL;
    ent = xpost_object_get_ent(anchor);
    if (ent < 0)
        return NULL;
    slot = _slot_of(xpost_context_select_memory(ctx, anchor),
                    (unsigned int)ent);
    if (!slot || (slot->size != size))
        return NULL;
    return slot;
}

XPOST_NOINLINE
void *xpost_dev_private_block(Xpost_Context *ctx,
                              Xpost_Object anchor,
                              size_t size)
{
    Xpost_Dev_Private_Slot *slot = _slot_named(ctx, anchor, size);

    return slot ? slot->block : NULL;
}

XPOST_NOINLINE
void *xpost_dev_private_block_of(Xpost_Context *ctx,
                                 Xpost_Object anchor,
                                 Xpost_Object devdic,
                                 size_t size)
{
    Xpost_Dev_Private_Slot *slot = _slot_named(ctx, anchor, size);
    int ent;

    if (!slot)
        return NULL;
    ent = xpost_object_get_ent(devdic);
    if ((ent < 0) ||
        (slot->ownermem != xpost_context_select_memory(ctx, devdic)) ||
        (slot->owner != (unsigned int)ent))
        return NULL;
    return slot->block;
}

void xpost_dev_private_release_entity(Xpost_Memory_File *mem,
                                      unsigned int ent)
{
    Xpost_Dev_Private_Slot *slot = _slot_of(mem, ent);

    if (!slot)
        return;
    free(slot->block);
    memset(slot, 0, sizeof(*slot));
}

void xpost_dev_private_release_memory_file(Xpost_Memory_File *mem)
{
    unsigned int i;

    for (i = 1; i < _nslots; i++)
        if ((_slots[i].ent != 0) && (_slots[i].mem == mem))
        {
            free(_slots[i].block);
            memset(&_slots[i], 0, sizeof(_slots[i]));
        }
}
