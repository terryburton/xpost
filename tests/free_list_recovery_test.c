/* What the allocator's answer to a corrupt free list is worth.
 *
 * A free list runs through entity numbers -- a bucket head in the free
 * list's own data area, and each node's `nextfree` field in the memory
 * table -- and a spoiled row or head can turn one into a number that
 * names no entity. The walk that meets one cannot go on, and cannot
 * leave the lists standing either, since the next walk would arrive at
 * the same place;
 * so it empties every bucket and answers that a collection is due. That
 * answer is the whole of the recovery: the entities are still there,
 * unmarked and untagged, and the sweep is what gathers them back.
 *
 * Which means the answer is only worth what happens after it. An
 * allocator that emptied the buckets and reported a plain failure would
 * satisfy every assertion made about the walk itself -- no entity handed
 * out, no bad link followed, nothing else disturbed -- and would leave
 * the interpreter with no free list at all for the rest of the run,
 * every allocation taking fresh memory and fresh entity numbers, with
 * nothing anywhere saying so. That is the shape this holds against:
 *
 *   the request is still answered, out of fresh memory;
 *   a collection is recorded as due, which is how one comes to run --
 *     the allocator cannot run one where it stands, because an object
 *     the current operator holds only in a C variable is in no root set;
 *   the collection refills the lists;
 *   and an allocation is served out of them again.
 *
 * The first three of those need an interpreter over the memory file --
 * a collection has stacks for its roots -- which is why they are here
 * and not beside the walk's own assertions in
 * tests/free_list_admission_test.c.
 *
 * The corruption is planted rather than provoked. Nothing a PostScript
 * program can do puts a number that names no entity into the list:
 * reaching that state takes a defect elsewhere in the interpreter,
 * which is what the recovery is for and what a test of it cannot wait
 * for. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_string.h"
#include "xpost_free.h"
#include "xpost_garbage.h"

#include "xpost_test.h"

#define GARBAGE_SZ 96u
#define GARBAGE_N 8

/* The bucket heads live in the free list's own entity, which records no
   size so that a write through a null object reference is refused; the
   library reaches them at the address that entity records, and so does
   this. */
static unsigned int bucket_head(Xpost_Memory_File *mem, unsigned int b)
{
    unsigned int v;

    memcpy(&v, xpost_vm_ptr(mem, xpost_memory_free_lists_adr(mem)
                                 + b * (unsigned int)sizeof(unsigned int)),
           sizeof v);
    return v;
}

static void set_bucket_head(Xpost_Memory_File *mem, unsigned int b,
                            unsigned int v)
{
    memcpy(xpost_vm_ptr(mem, xpost_memory_free_lists_adr(mem)
                             + b * (unsigned int)sizeof(unsigned int)),
           &v, sizeof v);
}

/* the first bucket holding anything, or XPOST_FREE_NBUCKETS if the list
   is empty */
static unsigned int first_occupied_bucket(Xpost_Memory_File *mem)
{
    unsigned int b;

    for (b = 0; b < XPOST_FREE_NBUCKETS; b++)
        if (bucket_head(mem, b) != 0)
            return b;
    return XPOST_FREE_NBUCKETS;
}

/* The pacing the allocator does before it looks at the list at all: a
   collection is asked for once the bytes allocated since the last one
   reach the count in force. Left where it is, that would answer for the
   request below without the corrupt list being reached, so the count is
   put out of reach of this one request. */
static void defer_the_pacing(Xpost_Memory_File *mem)
{
    mem->threshold = XPOST_GARBAGE_COLLECTION_THRESHOLD;
}

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Memory_File *mem;
    char text[GARBAGE_SZ];
    unsigned int b, head, want;
    unsigned int served = 0;
    int i;

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

    /* Something to lose: allocations nothing refers to once the hold
       stack the constructor stashed them on is cleared. The interpreter
       clears that stack between operator executions for the same reason
       it is cleared here. */
    memset(text, 'x', sizeof text);
    for (i = 0; i < GARBAGE_N; i++)
    {
        if (xpost_object_get_type(xpost_string_cons(ctx, sizeof text, text))
                != stringtype)
        {
            report_failure("could not make the garbage the list is filled from");
            xpost_destroy(ctx);
            xpost_quit();
            return verdict();
        }
    }
    xpost_stack_clear(ctx->lo, ctx->hold);

    if (xpost_garbage_collect(mem, 1, 1) < 0)
        report_failure("the collection that fills the list failed");
    b = first_occupied_bucket(mem);
    if (b == XPOST_FREE_NBUCKETS)
    {
        report_failure("a collection over discarded allocations left the free "
                       "list empty, so there is no list here to corrupt");
        xpost_destroy(ctx);
        xpost_quit();
        return verdict();
    }

    /* The size to ask for is the size of the entity at the head of that
       bucket, so that the walk enters the bucket the corruption is in
       and reads the corrupt link as its first node. */
    head = bucket_head(mem, b);
    if (!xpost_ent_valid(mem, head) ||
        !xpost_memory_table_get_size(mem, head, &want) || want == 0)
    {
        report_failure("could not read the entity at the head of bucket %u", b);
        xpost_destroy(ctx);
        xpost_quit();
        return verdict();
    }
    check(xpost_free_bucket_for_size(want) == b,
          "a request of the head entity's size enters the head entity's bucket");

    /* A number no entity has: inside what an object's entity field
       carries, so the walk's first test passes it, and past every
       entity the table has handed out, so the walk's second refuses it
       before anything reads the table there. */
    set_bucket_head(mem, b, XPOST_OBJECT_COMP_MAX_ENT);
    check(!xpost_ent_valid(mem, XPOST_OBJECT_COMP_MAX_ENT),
          "the planted link names no entity");

    mem->garbage_collect_pending = 0;
    defer_the_pacing(mem);
    check(xpost_memory_table_alloc(mem, want, stringtype, &served) == 1,
          "a request that meets the corrupt list is still answered");
    check(mem->garbage_collect_pending == 1,
          "and the collection that would rebuild the list is recorded as due");
    for (b = 0; b < XPOST_FREE_NBUCKETS; b++)
        if (bucket_head(mem, b) != 0)
        {
            report_failure("bucket %u survived the discard, for a later walk "
                           "to reach the corrupt link by again", b);
            break;
        }

    /* the recorded request, made good: this is what the interpreter does
       at its next safe point */
    if (xpost_garbage_collect(mem, 1, 1) < 0)
        report_failure("the collection the allocator asked for failed");
    b = first_occupied_bucket(mem);
    if (b == XPOST_FREE_NBUCKETS)
    {
        report_failure("the collection did not refill the free list, so the "
                       "discard cost the run its free list for good");
        xpost_destroy(ctx);
        xpost_quit();
        return verdict();
    }

    /* and reuse has resumed: an exact fit at the head of a bucket is
       what the walk stops at, so the entity handed out is that one */
    head = bucket_head(mem, b);
    if (!xpost_memory_table_get_size(mem, head, &want) || want == 0)
    {
        report_failure("could not read the entity at the head of bucket %u "
                       "after the collection", b);
        xpost_destroy(ctx);
        xpost_quit();
        return verdict();
    }
    served = 0;
    defer_the_pacing(mem);
    check(xpost_free_alloc(mem, want, stringtype, &served) == 1,
          "the refilled list serves an allocation");
    check(served == head,
          "and serves it out of the entity the request exactly fits");

    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
