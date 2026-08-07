/* What the free list will take in, and what it will hand back out.
 *
 * The list is a chain of entity numbers whose links live inside the
 * freed entities' own data. Two things follow from that, and neither is
 * observable from PostScript.
 *
 * An entity of no size has no data to hold a link. Its address is the
 * address the next allocation begins at -- a zero-length allocation
 * advances nothing -- so writing a link into it writes the neighbour's
 * first word, and handing it back out later gives two owners one
 * allocation. Refusing it is therefore a refusal that has to be read off
 * the memory and off the list: the function answers a reclaimed size,
 * which for an entity of no size is zero whether it refused or not.
 *
 * The neighbour is filled with bytes that differ from the link an
 * admission would write, so that a write let through is a change and not
 * a repetition.
 *
 * The other is a link that is not an entity number at all, which a stale
 * write into a freed entity's data produces. The walk validates every
 * node before it reads the table at that number. What the refusal is
 * worth is that no entity is handed out and nothing else in the file is
 * disturbed; the read the check stands in front of is of a table slot
 * this fixture's number is far outside, which a sanitizer build reports
 * and an ordinary one does not.
 *
 * The fixture is a memory file with no interpreter over it: the free
 * list sits below the object layer, and a PostScript program can neither
 * ask for an entity of no size nor write a link into a freed one. */

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

static void set_link(Xpost_Memory_File *mem, unsigned int ent, unsigned int v)
{
    memcpy(xpost_vm_ptr(mem, mem->table.tab[ent].adr), &v, sizeof v);
}

int main(void)
{
    Xpost_Memory_File mem;
    unsigned int empty = 0, neighbour = 0, again = 0;
    unsigned int node = 0, out = 0;
    unsigned int eadr = 0, nadr = 0;
    unsigned char pattern[NEIGHBOUR_SZ];
    unsigned char readback[NEIGHBOUR_SZ];
    unsigned int b0;
    unsigned int i;

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

    /* bytes that differ from the link an admission would write there:
       the bucket head is zero while the bucket is empty */
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

    /* --- a link that is not an entity number --- */

    if (!xpost_memory_table_alloc(&mem, NODE_SZ, 0, &node))
    {
        report_failure("could not allocate the free-list node");
        return verdict();
    }
    check(xpost_free_memory_ent(&mem, node) == (int)NODE_SZ,
          "an entity with data is admitted, and reports its size");
    check(bucket_head(&mem, xpost_free_bucket_for_size(NODE_SZ)) == node,
          "the admitted entity is the head of its bucket");

    /* a stale write turns the node's link into an arbitrary number. It
       is inside what an object's entity field can carry -- so the walk's
       first test passes it -- and far outside the table, so reading the
       tag at it reads past the table's allocation. */
    {
        unsigned int bogus = XPOST_OBJECT_COMP_MAX_ENT - 1u;

        check(bogus >= mem.table.max,
              "the planted link is past the table's allocation");
        check(bogus <= XPOST_OBJECT_COMP_MAX_ENT,
              "the planted link is inside what an entity field carries");
        set_link(&mem, node, bogus);

        /* a request the node does not fit exactly, so the walk follows
           the node's link rather than stopping at the node */
        out = node;
        check(xpost_free_alloc(&mem, NODE_SZ / 2u, 0, &out) != 1,
              "a walk that reaches a link that is not an entity hands out "
              "no entity");
        check(out == node,
              "and leaves the caller's entity number alone");
    }

    /* the walk stopped rather than following the planted link into the
       table, and the file around it is intact */
    memset(readback, 0, sizeof readback);
    check(xpost_memory_get(&mem, neighbour, 0, NEIGHBOUR_SZ, readback) == 1,
          "the neighbour still reads after the corrupt walk");
    check(memcmp(readback, pattern, NEIGHBOUR_SZ) == 0,
          "the neighbour's bytes survive the corrupt walk");

    xpost_memory_file_exit(&mem);
    xpost_quit();

    return verdict();
}
