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

#include <stdio.h>
#include <string.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_free.h" /* the arena description */
#include "xpost_context.h"
#include "xpost_file.h"
#include "xpost_vm_image.h"

/* The names, in the order the enumerations give. Held to that order by
   the count check in each accessor: a name list one shorter than the
   set it names would otherwise report every field under its
   neighbour's name. */
static const char *const _bank_field_names[] =
{
    "used",
    "start",
    "nextent",
    "free_substack",
    "free_scan",
    "period",
    "threshold",
    "gc_ent_budget",
    "file_birth_max",
    "gc_auto",
    "gc_pending",
    "ent_reserve_open",
    "ent_exhausted",
    "push_refused"
};

static const char *const _row_field_names[] =
{
    "adr",
    "used",
    "sz",
    "mark",
    "tag"
};

static const char *const _bank_names[] =
{
    "global",
    "local"
};

XPOST_TEST_VISIBLE const char *
xpost_vm_image_bank_field_name(unsigned int field)
{
    if (field >= sizeof _bank_field_names / sizeof *_bank_field_names)
        return "";
    return _bank_field_names[field];
}

XPOST_TEST_VISIBLE const char *
xpost_vm_image_row_field_name(unsigned int field)
{
    if (field >= sizeof _row_field_names / sizeof *_row_field_names)
        return "";
    return _row_field_names[field];
}

XPOST_TEST_VISIBLE const char *
xpost_vm_image_bank_name(unsigned int bank)
{
    if (bank >= sizeof _bank_names / sizeof *_bank_names)
        return "";
    return _bank_names[bank];
}

/* One value. Every number in an image is written this way, so the
   layout is a run of four-byte quantities with nothing between them:
   writing a structure whole would carry the padding the compiler left
   inside it, which is storage no one assigned and which would differ
   between two images of the same memory. */
static int _put(FILE *f, unsigned int v)
{
    return fwrite(&v, sizeof v, 1, f) == 1;
}

/* A bank's bookkeeping, in the order the field enumeration gives. The
   signed members go out as the bytes they occupy: nothing here
   interprets them, and a reader takes them back the way they were
   stored. */
static int _put_bank_fields(FILE *f, Xpost_Memory_File *mem)
{
    unsigned int field[XPOST_VM_IMAGE_BANK_FIELDS];
    unsigned int i;

    field[XPOST_VM_IMAGE_BANK_USED] = mem->used;
    field[XPOST_VM_IMAGE_BANK_START] = mem->start;
    field[XPOST_VM_IMAGE_BANK_NEXTENT] = mem->table.nextent;
    field[XPOST_VM_IMAGE_BANK_FREE_SUBSTACK] = mem->free_substack;
    field[XPOST_VM_IMAGE_BANK_FREE_SCAN] = mem->free_scan;
    field[XPOST_VM_IMAGE_BANK_PERIOD] = (unsigned int)mem->period;
    field[XPOST_VM_IMAGE_BANK_THRESHOLD] = (unsigned int)mem->threshold;
    field[XPOST_VM_IMAGE_BANK_GC_ENT_BUDGET] = mem->gc_ent_budget;
    field[XPOST_VM_IMAGE_BANK_FILE_BIRTH_MAX] = mem->file_birth_max;
    field[XPOST_VM_IMAGE_BANK_GC_AUTO] = (unsigned int)mem->garbage_collect_auto;
    field[XPOST_VM_IMAGE_BANK_GC_PENDING] = (unsigned int)mem->garbage_collect_pending;
    field[XPOST_VM_IMAGE_BANK_ENT_RESERVE_OPEN] = (unsigned int)mem->ent_reserve_open;
    field[XPOST_VM_IMAGE_BANK_ENT_EXHAUSTED] = (unsigned int)mem->ent_exhausted;
    field[XPOST_VM_IMAGE_BANK_PUSH_REFUSED] = (unsigned int)mem->push_refused;

    for (i = 0; i < XPOST_VM_IMAGE_BANK_FIELDS; i++)
        if (!_put(f, field[i]))
            return 0;
    return 1;
}

/* One bank whole: its name, its bookkeeping, the birth-stamp counters,
   the entity table and the arena.

   The arena goes out from its start to the high-water mark, which is
   more than the live entities: the padding an aligned allocation skips
   and the storage a reclaimed entity left behind are both inside that
   range and both are written. That is deliberate. The range is what an
   image would have to reproduce for an entity's recorded address to
   name the same bytes, and storage no one has written since the arena
   was cleared is exactly where a difference between two runs would
   otherwise go unseen. */
static int _put_bank(FILE *f, unsigned int bank, Xpost_Memory_File *mem)
{
    char name[8];
    unsigned int ent;
    unsigned int i;

    memset(name, 0, sizeof name);
    strncpy(name, xpost_vm_image_bank_name(bank), sizeof name - 1);
    if (fwrite(name, sizeof name, 1, f) != 1)
        return 0;

    if (!_put_bank_fields(f, mem))
        return 0;

    for (i = 0; i < XPOST_VM_IMAGE_FILE_BIRTHS; i++)
        if (!_put(f, mem->file_births[i]))
            return 0;

    for (ent = 0; ent < mem->table.nextent; ent++)
    {
        if (!_put(f, mem->table.tab[ent].adr)) return 0;
        if (!_put(f, mem->table.tab[ent].used)) return 0;
        if (!_put(f, mem->table.tab[ent].sz)) return 0;
        if (!_put(f, mem->table.tab[ent].mark)) return 0;
        if (!_put(f, mem->table.tab[ent].tag)) return 0;
    }

    /* A build that describes its arena to a memory checker keeps the
       storage the file has not handed out closed, the padding between
       allocations among it, so that a read of any of it is reported
       against whoever read it. Taking an image is the one read of the
       extent whole, so the extent is opened for it -- as the file
       already opens it to grow itself -- and stays open afterwards, at
       the cost of what the description would have caught in the padding
       for the rest of the run. */
    XPOST_VG_REOPEN_RANGE(mem->base, 0, mem->used);
    if (mem->used &&
        fwrite(mem->base, 1, mem->used, f) != mem->used)
        return 0;

    return 1;
}

XPOST_TEST_VISIBLE int
xpost_vm_image_write(Xpost_Context *ctx, const char *path)
{
    FILE *f;
    Xpost_Memory_File *bank[XPOST_VM_IMAGE_BANKS];
    unsigned int i;
    int err = 0;

    if (!ctx || !path)
    {
        XPOST_LOG_ERR("no context or no path to write virtual memory to");
        return 0;
    }
    if (!ctx->gl || !ctx->lo)
    {
        XPOST_LOG_ERR("the context has no virtual memory to write");
        return 0;
    }

    bank[0] = ctx->gl;
    bank[1] = ctx->lo;

    /* Through the one opener, like every other disk file the
       interpreter creates. The path is the caller's and not a running
       program's, so it is an interpreter-managed open rather than one
       the sandbox stands between. */
    f = xpost_diskfile_fopen(path, "wb", 1, &err);
    if (!f)
    {
        XPOST_LOG_ERR("%d cannot open %s to write virtual memory to",
                      err, path);
        return 0;
    }

    if (fwrite(XPOST_VM_IMAGE_MAGIC, XPOST_VM_IMAGE_MAGIC_LEN, 1, f) != 1)
        goto refuse;
    if (!_put(f, XPOST_VM_IMAGE_VERSION)) goto refuse;
    if (!_put(f, (unsigned int)sizeof(Xpost_Object))) goto refuse;
    if (!_put(f, XPOST_OBJECT_COMP_MAX_ENT)) goto refuse;
    if (!_put(f, XPOST_VM_IMAGE_BANKS)) goto refuse;

    for (i = 0; i < XPOST_VM_IMAGE_BANKS; i++)
        if (!_put_bank(f, i, bank[i]))
            goto refuse;

    /* the image is only written where it reached storage: a short write
       discovered at the close is the same failure as one discovered
       above, and a caller told the write succeeded would go on to
       compare or load a truncated image */
    if (fclose(f) != 0)
    {
        XPOST_LOG_ERR("cannot finish writing virtual memory to %s", path);
        return 0;
    }
    return 1;

  refuse:
    XPOST_LOG_ERR("cannot write virtual memory to %s", path);
    (void)fclose(f);
    return 0;
}
