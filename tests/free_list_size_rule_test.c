/* What size a free-list entry has to be for an allocation to take it.
 *
 * The rule is that an entry holds the request: any entry of at least the
 * requested size is taken, and one smaller is not. Nothing scales the
 * comparison, so an entry many times the size of the request still
 * serves it -- which is what lets a job that releases one size of buffer
 * and then asks for another go on using the memory it already has
 * instead of growing the file. Within a bucket the closest of the
 * entries the walk examines is preferred, so the waste an oversized
 * entry leaves is taken only when nothing nearer is offered.
 *
 * A rule that scaled the comparison would pass every one of the other
 * tests here: an exact fit is served either way, and a job whose buffers
 * never change size never asks for the case that separates them. The
 * ratios below are therefore asked directly, and spread wide enough
 * either side of any plausible factor that a build which reintroduced
 * one would answer differently at some of them and not at others. The
 * entry a request must not be given -- one smaller than it asks for --
 * is asked alongside, so that a build that answered "taken" to
 * everything could not read as agreement.
 *
 * The fixture is a memory file with no interpreter over it, as in
 * tests/free_list_admission_test.c: the sizes are asked of the free list
 * directly rather than through the object layer, which rounds a
 * request up and would put the sizes under test out of the test's
 * hands.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_free.h"

#include "xpost_test.h"

/* The memory file asks its owner whether the interpreter is still
   starting up, to know whether an allocation may ask for a collection.
   This fixture has no interpreter and collects nothing, so it answers
   the question that leaves the allocator alone. */
static int fixture_initializing(void)
{
    return 1;
}

static void fixture_set_initializing(int initializing)
{
    (void)initializing;
}

static void bucket_head_set(Xpost_Memory_File *mem, unsigned int b,
                            unsigned int ent)
{
    memcpy(xpost_vm_ptr(mem, xpost_memory_free_lists_adr(mem)
                             + b * (unsigned int)sizeof(unsigned int)),
           &ent, sizeof ent);
}

/* Start each case with the lists as they were before the one before it.
   An entry left over from an earlier case sits in a bucket of its own
   size, which is where a later request would look for one, so a case
   read against a list still holding them would be reporting on whichever
   entry happened to be nearest rather than on the size rule. */
static void clear_buckets(Xpost_Memory_File *mem)
{
    unsigned int b;

    for (b = 0; b < XPOST_FREE_NBUCKETS; b++)
        bucket_head_set(mem, b, 0);
}

/* Offer the list one entry of freesz and ask it for reqsz. Answers
   whether that one entry is what came back. */
static int offered_then_asked(Xpost_Memory_File *mem,
                              unsigned int freesz, unsigned int reqsz)
{
    unsigned int held = 0;
    unsigned int got = 0;
    int taken;

    clear_buckets(mem);
    if (!xpost_memory_table_alloc(mem, freesz, 0, &held))
    {
        report_failure("could not allocate an entity of %u bytes", freesz);
        return 0;
    }
    if (xpost_free_memory_ent(mem, held) != (int)freesz)
    {
        report_failure("an entity of %u bytes was not admitted to the list",
                       freesz);
        return 0;
    }
    taken = xpost_free_alloc(mem, reqsz, 1, &got);
    if (taken == 1)
        return got == held;
    return 0;
}

int main(void)
{
    Xpost_Memory_File mem;
    unsigned int near = 0, far = 0, got = 0;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    memset(&mem, 0, sizeof mem);
    if (!xpost_memory_file_init(&mem, NULL, -1, NULL,
                                fixture_initializing,
                                fixture_set_initializing))
    {
        report_failure("xpost_memory_file_init");
        return verdict();
    }
    if (!xpost_memory_table_init(&mem,
                                 XPOST_MEMORY_TABLE_SPECIAL_FREE + 1))
    {
        report_failure("xpost_memory_table_init");
        return verdict();
    }
    if (!xpost_free_init(&mem))
    {
        report_failure("xpost_free_init");
        return verdict();
    }

    /* --- an entry that holds the request is taken, at any size --- */

    check(offered_then_asked(&mem, 100u, 100u),
          "an entry of exactly the requested size is taken");
    check(offered_then_asked(&mem, 140u, 100u),
          "an entry two fifths larger than the request is taken");
    check(offered_then_asked(&mem, 160u, 100u),
          "an entry three fifths larger than the request is taken");
    check(offered_then_asked(&mem, 200u, 100u),
          "an entry twice the request is taken");
    check(offered_then_asked(&mem, 400u, 100u),
          "an entry four times the request is taken");
    check(offered_then_asked(&mem, 6400u, 100u),
          "an entry sixty-four times the request is taken");

    /* --- and one that does not hold it is not --- */

    clear_buckets(&mem);
    if (!xpost_memory_table_alloc(&mem, 100u, 0, &near))
    {
        report_failure("could not allocate the undersized entry");
        return verdict();
    }
    if (xpost_free_memory_ent(&mem, near) != 100)
    {
        report_failure("the undersized entry was not admitted to the list");
        return verdict();
    }
    got = near;
    check(xpost_free_alloc(&mem, 101u, 1, &got) == 0,
          "an entry one byte short of the request is not taken");
    check(got == near,
          "a request nothing answers leaves the caller's entity number alone");

    /* --- the closest of the entries offered is preferred --- */

    /* Both hold the request and all three sizes fall in one bucket, so
       the walk sees the two of them and the answer is which it kept
       rather than which bucket it stopped at. The further entry is
       admitted last, which puts it at the head: an allocation that took
       the first entry holding the request would take that one, so the
       nearer entry coming back is the preference and not the order. */
    clear_buckets(&mem);
    if (!xpost_memory_table_alloc(&mem, 80u, 0, &near) ||
        !xpost_memory_table_alloc(&mem, 120u, 0, &far))
    {
        report_failure("could not allocate the two candidate entries");
        return verdict();
    }
    check(xpost_free_bucket_for_size(70u) == xpost_free_bucket_for_size(80u)
       && xpost_free_bucket_for_size(70u) == xpost_free_bucket_for_size(120u),
          "both entries are in the bucket the request looks in first");
    if (xpost_free_memory_ent(&mem, near) != 80 ||
        xpost_free_memory_ent(&mem, far) != 120)
    {
        report_failure("the two candidate entries were not admitted");
        return verdict();
    }
    got = 0;
    check(xpost_free_alloc(&mem, 70u, 1, &got) == 1,
          "a request two entries hold is answered");
    check(got == near,
          "the entry nearest the request is the one taken");

    xpost_memory_file_exit(&mem);
    xpost_quit();

    return verdict();
}
