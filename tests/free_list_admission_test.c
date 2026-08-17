/* What the free list will take in, and what it will hand back out.
 *
 * The list is a chain of entity numbers, and the chain is held entirely
 * in the memory table: a bucket head per size class in the free list's
 * own data area, and each node's successor in the `nextfree` field of
 * its own table row. The storage a freed entity stands for holds
 * nothing the allocator reads. Three things follow, and none of them is
 * observable from PostScript.
 *
 * An entity of no size is still refused. Its address is the address the
 * next allocation begins at -- a zero-length allocation advances
 * nothing -- so admitting one and handing it back out would give two
 * entity numbers one address. The refusal has to be read off the list
 * rather than off the answer: the function answers a reclaimed size,
 * which for an entity of no size is zero whether it refused or not.
 *
 * A release writes nothing into what it releases. That is what lets the
 * storage of a freed entity be treated as meaningless -- closed to a
 * sanitizer in its whole extent, and returnable to the system a page at
 * a time. It is read here by filling an entity, releasing it, taking it
 * back from the list and finding the fill still there: an implementation
 * that kept its links in the freed storage would have overwritten the
 * first word of it.
 *
 * A link can still fail to name an entity, since the rows the chain runs
 * through are written from more places than the reclaimer. The walk
 * validates every node before it reads the table at that number, and
 * what it does on finding a bad one is more than decline: the lists are
 * unusable from that node on, so it empties every bucket and answers
 * that a collection is due, which is what refills them. Both halves are
 * read here -- the answer, and the buckets afterwards -- because a walk
 * that returned the same answer without emptying the buckets would leave
 * the next walk to reach the same bad link, and a walk that emptied them
 * without asking for a collection would leave the file with no free list
 * at all.
 *
 * Two numbers stand for the two places a spoiled link can point, and
 * they are asked in an order the second half of that explains. The first is
 * inside the table's allocation but past the entities that have been
 * handed out, and the slot there is made to look exactly like a free
 * node -- no tag, a plausible size -- so that nothing but the number's
 * naming no entity tells the walk to stop. A build that has lost that
 * test reads a slot it is entitled to read and gives a wrong answer,
 * which this test's own assertions report. The second is past the
 * table altogether, where the same lost test is a read outside the
 * allocation: a sanitizer build reports that and an ordinary one does
 * not, and nothing is left for an assertion to say. So the second is
 * planted only once the first has shown the test is there, rather than
 * performing a read outside the table to find out what is already
 * known.
 *
 * The fixture is a memory file with no interpreter over it: the free
 * list sits below the object layer, and a PostScript program can neither
 * ask for an entity of no size nor spoil a link. That the collection
 * this asks for then arrives and refills the lists is the interpreter's
 * half, and is held in tests/free_list_recovery_test.c. */

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

#define NEIGHBOUR_SZ 16u
#define NODE_SZ 64u

/* read a bucket head out of the free list's own data area. The special
   entity carrying it records no size, so it is read through the address
   the list is found at rather than through the bounded accessors. */
static unsigned int bucket_head(Xpost_Memory_File *mem, unsigned int b)
{
    unsigned int v;
    memcpy(&v, xpost_vm_ptr(mem, xpost_memory_free_lists_adr(mem)
                                 + b * (unsigned int)sizeof(unsigned int)),
           sizeof v);
    return v;
}

/* the successor of a node, in the row the chain runs through */
static void set_link(Xpost_Memory_File *mem, unsigned int ent, unsigned int v)
{
    mem->table.tab[ent].nextfree = v;
}

/* every bucket, so that a discard is read as the whole free list going
   away rather than as the one bucket the walk was in */
static int all_buckets_empty(Xpost_Memory_File *mem)
{
    unsigned int b;

    for (b = 0; b < XPOST_FREE_NBUCKETS; b++)
        if (bucket_head(mem, b) != 0)
            return 0;
    return 1;
}

/* Put a link that is not an entity number into the one node on the list,
   ask for an allocation the node itself does not answer so that the walk
   follows the link, and hold what the walk does with it. The node is on
   the list already, since a walk over an empty list reads no link at
   all. The link is named in each report, since the two numbers are
   refused by different tests and a report has to say which one went
   unrefused.

   Answers whether the walk did all three things, which is what the
   caller needs to know before planting a number whose refusal is the
   only thing standing between the walk and a read outside the table. */
static int walk_over_bad_link(Xpost_Memory_File *mem, unsigned int node,
                              unsigned int bogus, const char *what)
{
    unsigned int out = node;
    int refused = 1;

    if (bucket_head(mem, xpost_free_bucket_for_size(NODE_SZ)) != node)
    {
        report_failure("the node was not on the list before %s was planted",
                       what);
        return 0;
    }
    set_link(mem, node, bogus);

    /* the request is half the node's size: a size the node fits, so the
       walk enters its bucket, and not the size it is, so the walk does
       not stop at an exact fit before it reads the link */
    if (xpost_free_alloc(mem, NODE_SZ / 2u, 0, &out)
            != XPOST_FREE_WANT_COLLECTION)
    {
        report_failure("a walk that reaches %s does not ask for the "
                       "collection that would rebuild the list", what);
        refused = 0;
    }
    if (out != node)
    {
        report_failure("a walk that reaches %s did not leave the caller's "
                       "entity number alone", what);
        refused = 0;
    }
    if (!all_buckets_empty(mem))
    {
        report_failure("a walk that reaches %s left a bucket for a later "
                       "walk to reach it by again", what);
        refused = 0;
    }
    return refused;
}

int main(void)
{
    Xpost_Memory_File mem;
    unsigned int empty = 0, neighbour = 0, again = 0;
    unsigned int node = 0, retaken = 0;
    unsigned int eadr = 0, nadr = 0;
    unsigned char pattern[NEIGHBOUR_SZ];
    unsigned char readback[NEIGHBOUR_SZ];
    unsigned char nodefill[NODE_SZ];
    unsigned char nodeback[NODE_SZ];
    unsigned int b0;
    unsigned int i;
    int refuses_invalid;

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
    if (!xpost_memory_table_init(&mem))
    {
        report_failure("xpost_memory_table_init");
        return verdict();
    }
    if (!xpost_free_init(&mem))
    {
        report_failure("xpost_free_init");
        return verdict();
    }

    /* --- an entity of no size is not admitted --- */

    if (!xpost_memory_table_alloc(&mem, 0, 0, &empty) ||
        !xpost_memory_table_alloc(&mem, NEIGHBOUR_SZ, 0, &neighbour))
    {
        report_failure("could not allocate the empty entity and its neighbour");
        return verdict();
    }
    if (!xpost_memory_table_get_addr(&mem, empty, &eadr) ||
        !xpost_memory_table_get_addr(&mem, neighbour, &nadr))
    {
        report_failure("could not read the two entities' addresses");
        return verdict();
    }
    /* what makes the neighbour a witness: an entity of no size advances
       the file by nothing, so the next allocation starts where it does */
    check(eadr == nadr,
          "an entity of no size shares its address with the next allocation");

    /* bytes an admission that wrote anything at all would disturb: they
       are neither zero nor an entity number the file has issued */
    for (i = 0; i < NEIGHBOUR_SZ; i++)
        pattern[i] = (unsigned char)(0xa5 ^ i);
    if (!xpost_memory_put(&mem, neighbour, 0, NEIGHBOUR_SZ, pattern))
    {
        report_failure("could not fill the neighbour");
        return verdict();
    }
    b0 = xpost_free_bucket_for_size(0);
    check(bucket_head(&mem, b0) == 0,
          "the bucket an entity of no size would join is empty to begin with");

    /* the reclaimed size an entity of no size reports is zero whether it
       was admitted or not, so the answer is taken only as "not an error" */
    check(xpost_free_memory_ent(&mem, empty) == 0,
          "offering an entity of no size is not an error");

    memset(readback, 0, sizeof readback);
    check(xpost_memory_get(&mem, neighbour, 0, NEIGHBOUR_SZ, readback) == 1,
          "the neighbour still reads");
    check(memcmp(readback, pattern, NEIGHBOUR_SZ) == 0,
          "the neighbour's bytes survive the offer");
    check(bucket_head(&mem, b0) == 0,
          "the list did not take in an entity of no size");

    /* and the harm the refusal prevents: a second request of no size
       must not be answered with the first entity, whose address is the
       neighbour's */
    if (!xpost_memory_table_alloc(&mem, 0, 0, &again))
    {
        report_failure("could not allocate a second entity of no size");
        return verdict();
    }
    check(again != empty,
          "a second entity of no size is not the first handed back");

    /* --- a release writes nothing into what it releases --- */

    if (!xpost_memory_table_alloc(&mem, NODE_SZ, 0, &node))
    {
        report_failure("could not allocate the free-list node");
        return verdict();
    }

    /* filled before the release and read after taking it back: an
       implementation that held its links in the freed storage would have
       written the first word of this in between */
    for (i = 0; i < NODE_SZ; i++)
        nodefill[i] = (unsigned char)(0x5c ^ i);
    if (!xpost_memory_put(&mem, node, 0, NODE_SZ, nodefill))
    {
        report_failure("could not fill the free-list node");
        return verdict();
    }

    check(xpost_free_memory_ent(&mem, node) == (int)NODE_SZ,
          "an entity with data is admitted, and reports its size");
    check(bucket_head(&mem, xpost_free_bucket_for_size(NODE_SZ)) == node,
          "the admitted entity is the head of its bucket");

    /* the one entity on the list, asked for at exactly its size, so the
       walk stops at it and the bytes read back are the ones filled in */
    if (!xpost_memory_table_alloc(&mem, NODE_SZ, 0, &retaken))
    {
        report_failure("could not take the node back off the list");
        return verdict();
    }
    check(retaken == node,
          "a request of the node's own size is answered with the node");
    memset(nodeback, 0, sizeof nodeback);
    check(xpost_memory_get(&mem, node, 0, NODE_SZ, nodeback) == 1,
          "the node reads once it has been handed back out");
    check(memcmp(nodeback, nodefill, NODE_SZ) == 0,
          "a release leaves the storage it releases exactly as it was");

    /* --- a link that is not an entity number --- */

    check(xpost_free_memory_ent(&mem, node) == (int)NODE_SZ,
          "the node goes back on the list for the walk below");
    check(bucket_head(&mem, xpost_free_bucket_for_size(NODE_SZ)) == node,
          "and is its bucket's head again");

    /* A stale write turns the node's link into a number the table has
       room for but has never handed out. Reading the slot there is
       inside the table's allocation, so nothing outside this test
       reports it; the slot is filled in to look like a node of the list
       -- no tag, a size, an address that is a real one -- so that the
       only thing left saying the walk must stop is that the number names
       no entity. This is the case whose refusal can be read off the
       walk's own answer, so it is asked first. */
    {
        unsigned int unhanded = mem.table.max / 2u;

        check(unhanded >= mem.table.nextent,
              "the planted link names no entity that has been handed out");
        check(unhanded < mem.table.max,
              "the planted link is inside the table's allocation");
        check(unhanded <= XPOST_OBJECT_COMP_MAX_ENT,
              "the planted link is inside what an entity field carries");
        mem.table.tab[unhanded].tag = 0;
        mem.table.tab[unhanded].sz = NODE_SZ;
        mem.table.tab[unhanded].used = NODE_SZ;
        mem.table.tab[unhanded].mark = 0;
        mem.table.tab[unhanded].adr = mem.table.tab[node].adr;

        refuses_invalid = walk_over_bad_link(&mem, node, unhanded,
                                             "a link into a slot nothing "
                                             "has been given");
    }

    /* the walk stopped rather than following the planted link into the
       table, and the file around it is intact */
    memset(readback, 0, sizeof readback);
    check(xpost_memory_get(&mem, neighbour, 0, NEIGHBOUR_SZ, readback) == 1,
          "the neighbour still reads after the corrupt walk");
    check(memcmp(readback, pattern, NEIGHBOUR_SZ) == 0,
          "the neighbour's bytes survive the corrupt walk");

    /* --- a link past the table altogether --- */

    /* The same stale write can equally land on a number past every slot
       the table has. Nothing here can be read off the answer that the
       case above does not already say: the walk refuses this one by the
       same test, and the number's being outside the table as well is
       what a sanitizer build reports and an ordinary one does not.

       So it is only planted once that test has been seen to work. A
       build whose walk reads the table at a number naming no entity has
       already said so above, and planting this number there would put
       the read outside the table's allocation -- which is not a thing to
       do on purpose to find out something already known. */
    if (refuses_invalid)
    {
        unsigned int bogus = XPOST_OBJECT_COMP_MAX_ENT - 1u;

        check(bogus >= mem.table.max,
              "the planted link is past the table's allocation");
        check(bogus <= XPOST_OBJECT_COMP_MAX_ENT,
              "the planted link is inside what an entity field carries");
        check(xpost_free_memory_ent(&mem, node) == (int)NODE_SZ,
              "the discarded entity is admitted again");
        (void)walk_over_bad_link(&mem, node, bogus,
                                 "a link past the table's allocation");

        memset(readback, 0, sizeof readback);
        check(xpost_memory_get(&mem, neighbour, 0, NEIGHBOUR_SZ, readback) == 1,
              "the neighbour still reads after the second corrupt walk");
        check(memcmp(readback, pattern, NEIGHBOUR_SZ) == 0,
              "the neighbour's bytes survive the second corrupt walk");
    }

    xpost_memory_file_exit(&mem);
    xpost_quit();

    return verdict();
}
