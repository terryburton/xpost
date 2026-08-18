/* That rearranging the arena moves storage without changing what it holds.
 *
 * A compacting pass sorts the live entities by address and slides them
 * down over the blocks between them, so the free storage gathers at the
 * top where the pages under it can be handed back. It is writable at all
 * only because every block in the arena carries a row: a block with no
 * row could not be seen by a walk of the table, and an entity slid down
 * would land on top of it.
 *
 * What is asked here is the property the pass exists to keep, rather
 * than the bytes it saves. Every entity that survives must hold exactly
 * what it held, at whatever address it now sits: the pass rewrites the
 * one field that says where an entity's bytes are, and an entity whose
 * contents changed is one whose bytes were moved without that field
 * following, or one moved on top of another. The contents are taken as a
 * checksum before and compared after, which is a question the entity
 * layer cannot answer for itself -- an object reached through the table
 * would be read through the very field under test, and would agree with
 * itself however wrong it was.
 *
 * The saving is asked too, but loosely: what a given job leaves free
 * depends on the job, and a test that pinned it would be reporting on
 * the program rather than on the pass.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_context.h"
#include "xpost_interpreter.h"
#include "xpost_free.h"
#include "xpost_garbage.h"

#include "xpost_test.h"

/* Enough allocation, and enough of it dropped, that the free blocks lie
   between live ones rather than in one run at the top -- a pass that
   only trimmed the tail would answer this test correctly otherwise. */
static const char *churn =
    "/keep 300 array def\n"
    "0 1 299 { /i exch def keep i i 40 string cvs put } for\n"
    "0 1 2000 { pop 400 array 250 string pop pop } for\n";

typedef struct { unsigned int ent, sz, sum; } Snap;

/* Which entities the free lists hold.
 *
 * A freed entity keeps its address and its size until a pass reclaims
 * its row, and the table cannot tell it from a live one -- a freed
 * entity carries the same zero tag a live raw allocation carries, so the
 * lists are the only record of which is which. Its storage holds nothing
 * anybody promised anything about: the pass is free to slide a live
 * entity over it, and where the arena is described to a memory checker
 * the storage is closed. Summing it would be asserting over bytes with
 * no claim on them, so the snapshot leaves them out.
 */
static unsigned char *_free_set(Xpost_Memory_File *mem, unsigned int rows)
{
    unsigned char *isfree = calloc(rows ? rows : 1, 1);
    unsigned int b;

    if (!isfree || !xpost_memory_free_lists_ready(mem))
        return isfree;
    for (b = 0; b < XPOST_FREE_NBUCKETS; b++)
    {
        unsigned int e, seen = 0;

        memcpy(&e, xpost_vm_ptr(mem, xpost_memory_free_lists_adr(mem)
                                + b * (unsigned int)sizeof(unsigned int)),
               sizeof e);
        /* bounded by the table it indexes, so a spoiled link cannot spin
           this the way it cannot spin the allocator's own walk */
        while (e && e < rows && seen <= rows)
        {
            isfree[e] = 1;
            ++seen;
            e = mem->table.tab[e].nextfree;
        }
    }
    return isfree;
}

static unsigned int _sum(Xpost_Memory_File *mem, unsigned int adr,
                         unsigned int sz)
{
    const unsigned char *p = (const unsigned char *)xpost_vm_ptr(mem, adr);
    unsigned int h = 2166136261u, i;

    for (i = 0; i < sz; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

int main(void)
{
    Xpost_Context *ctx;
    Snap *snap;
    unsigned char *isfree;
    unsigned int n = 0, i, before, after, checked = 0;

    if (!xpost_init())
    {
        report_failure("the library would not start");
        return verdict();
    }
    ctx = xpost_create("pgm", XPOST_OUTPUT_FILENAME, "/dev/null",
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 612, 792);
    if (!ctx)
    {
        report_failure("no context could be built");
        xpost_quit();
        return verdict();
    }

    if (xpost_run(ctx, XPOST_INPUT_STRING, churn, 0) == XPOST_RUN_ERRORED)
        report_failure("the program that fills virtual memory errored");
    if (xpost_garbage_collect(ctx->lo, 1, 1) < 0)
        report_failure("the collection failed");

    snap = malloc(sizeof(*snap) * (ctx->lo->table.nextent + 1));
    if (!snap)
    {
        report_failure("out of memory taking the snapshot");
        return verdict();
    }
    isfree = _free_set(ctx->lo, ctx->lo->table.nextent);
    for (i = 0; i < ctx->lo->table.nextent; i++)
    {
        if (ctx->lo->table.tab[i].sz == 0) continue;
        if (isfree && isfree[i]) continue;
        snap[n].ent = i;
        snap[n].sz = ctx->lo->table.tab[i].sz;
        snap[n].sum = _sum(ctx->lo, ctx->lo->table.tab[i].adr,
                           ctx->lo->table.tab[i].sz);
        n++;
    }
    before = ctx->lo->high_water;

    if (!xpost_free_compact(ctx->lo, NULL))
        report_failure("the arena would not be rearranged");
    after = ctx->lo->high_water;

    for (i = 0; i < n; i++)
    {
        unsigned int e = snap[i].ent;

        /* an entity the pass reclaimed was one of the free blocks it
           slid over, and has nothing left to compare */
        if (ctx->lo->table.tab[e].sz == 0) continue;
        checked++;
        if (ctx->lo->table.tab[e].sz != snap[i].sz)
        {
            report_failure("entity %u is %u bytes and was %u: a live entity "
                           "changed size in a pass that only moves them",
                           e, ctx->lo->table.tab[e].sz, snap[i].sz);
            break;
        }
        if (_sum(ctx->lo, ctx->lo->table.tab[e].adr, snap[i].sz) != snap[i].sum)
        {
            report_failure("entity %u holds something else after the arena "
                           "was rearranged: its bytes moved without the row "
                           "that says where they are following them, or "
                           "another entity was slid on top of it", e);
            break;
        }
    }

    check(checked > 100,
          "the comparison reached the entities: a pass that had emptied the "
          "table would leave nothing to compare and read as agreement");
    check(after < before,
          "the arena's high-water mark falls, which is what makes the pages "
          "above it returnable");

    /* The rows of the blocks it slid over must be issuable again, or the
       job pays for every compaction in entity numbers it cannot recover. */
    {
        unsigned int rows = ctx->lo->table.nextent, ent = 0, j;

        for (j = 0; j < 50; j++)
            if (!xpost_memory_table_alloc(ctx->lo, 32, 0, &ent))
            {
                report_failure("could not allocate after the rearrangement");
                break;
            }
        check(ctx->lo->table.nextent == rows,
              "allocating after the pass spends no new slot: the rows it "
              "emptied went back where an allocation can find them");
    }

    free(snap);
    free(isfree);
    xpost_destroy(ctx);
    xpost_quit();
    return verdict();
}
