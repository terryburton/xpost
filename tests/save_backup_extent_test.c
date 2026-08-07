/* How much of an allocation a save's backup says is in use.
 *
 * An entity carries a capacity and an extent: the block it was given,
 * and how much of that block its owner asked for. The two come apart
 * whenever the free list serves a request out of a larger corpse, which
 * it does by design -- entity numbers are the scarcer budget, so a
 * request takes a roomier entry rather than a fresh number.
 *
 * The extent is what the collector reads an allocation's contents by:
 * an array holds as many objects as its extent spans, and the bytes
 * between the extent and the capacity are whatever the block's previous
 * owner left there. So a backup that records the capacity as its extent
 * does not merely waste room -- once restore swaps the backup's storage
 * identity into the object, the object claims an extent it never filled,
 * and the collector reads the previous owner's leavings as objects.
 *
 * What that costs is the collector itself. The stale bytes are not
 * objects; one of them names an entity that does not exist, marking
 * refuses it, and every collection from then on gives up before its
 * sweep. Nothing says so -- the safe points discard the collector's
 * answer -- and the run continues allocating until the entity numbers
 * run out, and reports that as a limitcheck against whatever operator
 * happened to be allocating.
 *
 * Both halves are asserted here of the same allocation. The extent is
 * read directly, because it is the thing that must not change; and an
 * object planted in the block beyond the extent, referred to by nothing
 * else, is asked to be reclaimed -- which is the whole of the harm, and
 * distinguishes an extent that is right from one that is merely
 * plausible.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_string.h"
#include "xpost_array.h"
#include "xpost_free.h"
#include "xpost_save.h"
#include "xpost_garbage.h"

#include "xpost_test.h"

/* The roomier block and the request served out of it. Both land in the
   free list's top size class, which nothing in a fresh interpreter
   reaches, so the request meets this block and no other; the test holds
   itself to that below rather than assuming it. */
#define ROOMY 40000
#define ASKED 33000
/* an index inside the block and outside the request's extent */
#define BEYOND 35000

static unsigned int tag_of(Xpost_Context *ctx, Xpost_Object o)
{
    Xpost_Memory_File *mem = xpost_context_select_memory(ctx, o);
    unsigned int tag = (unsigned int)-1;

    if (!mem || !xpost_memory_table_get_tag(mem, (unsigned int)
                                            xpost_object_get_ent(o), &tag))
        return (unsigned int)-1;
    return tag & XPOST_OBJECT_TAG_DATA_TYPE_MASK;
}

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Memory_File *mem;
    Xpost_Object roomy;
    Xpost_Object asked;
    Xpost_Object stale;
    Xpost_Object save;
    Xpost_Object reverted;
    unsigned int ent;
    unsigned int extent;
    char text[32];

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }
    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_RETURN, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return verdict();
    }
    mem = ctx->lo;

    /* the block, with an object planted in the part of it the request
       below will not reach */
    roomy = xpost_array_cons(ctx, ROOMY);
    memset(text, 'x', sizeof text);
    stale = xpost_string_cons(ctx, sizeof text, text);
    if (xpost_object_get_type(roomy) != arraytype
     || xpost_object_get_type(stale) != stringtype)
    {
        report_failure("could not construct the block and its contents");
        return verdict();
    }
    if (xpost_array_put(ctx, roomy, BEYOND, stale) != 0)
    {
        report_failure("could not plant an object in the block");
        return verdict();
    }
    ent = (unsigned int)xpost_object_get_ent(roomy);

    /* give the block up to the free list, and take a smaller request
       back out of it */
    xpost_stack_clear(ctx->lo, ctx->hold);
    if (xpost_free_memory_ent(mem, ent) < 0)
    {
        report_failure("could not release the block");
        return verdict();
    }
    asked = xpost_array_cons(ctx, ASKED);
    if (xpost_object_get_type(asked) != arraytype)
    {
        report_failure("could not construct the array");
        return verdict();
    }
    if ((unsigned int)xpost_object_get_ent(asked) != ent)
    {
        report_failure("the request was not served out of the released block");
        return verdict();
    }
    if (mem->table.tab[ent].sz <= mem->table.tab[ent].used)
    {
        report_failure("the block is no roomier than the request");
        return verdict();
    }
    extent = mem->table.tab[ent].used;

    /* the array is the only thing the collection below may reach the
       block through */
    if (!xpost_stack_push(ctx->lo, ctx->os, asked))
    {
        report_failure("could not root the array");
        return verdict();
    }

    /* save, write, restore: the write backs the array up, and the
       restore puts the backup's storage identity into it */
    save = xpost_save_create_snapshot_object(mem);
    if (xpost_object_get_type(save) != savetype)
    {
        report_failure("could not take a snapshot");
        return verdict();
    }
    if (xpost_array_put(ctx, asked, 0, xpost_int_cons(42)) != 0)
    {
        report_failure("could not write to the array");
        return verdict();
    }
    xpost_save_restore_snapshot(mem);

    reverted = xpost_array_get(ctx, asked, 0);
    if (xpost_object_get_type(reverted) != nulltype)
        report_failure("the restore did not revert the write");

    if (mem->table.tab[ent].used != extent)
        report_failure("the restored array claims %u bytes in use where it "
                       "filled %u", mem->table.tab[ent].used, extent);

    /* the planted object lies in the block beyond the array's extent,
       and nothing else names it */
    xpost_stack_clear(ctx->lo, ctx->hold);
    if (xpost_garbage_collect(ctx->lo, 1, 1) < 0)
        report_failure("the collection failed");
    if (tag_of(ctx, stale) != 0)
        report_failure("an object beyond the array's extent was kept");

    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
