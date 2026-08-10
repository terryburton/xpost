/* The bound on a read or a write into an allocation, held from both
   sides.
 *
 * xpost_memory_get and xpost_memory_put address an allocation as a run
 * of equal-sized slots, and each answers a slot outside the allocation
 * by refusing rather than by touching the memory around it. Every
 * composite in the interpreter -- every string, array and dictionary --
 * reaches its storage through this pair, so the refusal is the last
 * thing standing between a mis-sized index and the neighbouring
 * allocation.
 *
 * A refusal is not observable as a return value alone: a caller that
 * ignores the answer is served by a function that refused and by one
 * that did not, and so is a test that reads only the answer. Each
 * refusal here is therefore asked of the memory as well -- the
 * neighbour's bytes are read back afterwards, and the destination of a
 * refused read is pre-filled and read back -- and every refused write
 * carries bytes that differ from the ones already where it would land,
 * so that a write let through is a change and not a repetition.
 *
 * The bound is computed in a width wider than the index, so that a slot
 * number large enough to wrap the product back inside the allocation is
 * still outside it. The wrapping pair is asked of both functions: the
 * product is taken twice in each, once for the comparison and once for
 * the address copied to, and a comparison made in the narrow width
 * agrees with the address and lets the copy through.
 *
 * The same width holds one allocation away from another, and it is asked
 * for here too. A memory file hands out its storage by rounding its
 * cursor up to an eight-byte boundary and taking the bytes above it, and
 * both of those numbers are addresses in the file, which are unsigned
 * ints. A cursor within eight bytes of the top of that range rounds up
 * past the end of it, and an address arrived at in the width it is
 * counted in comes back round to the bottom -- naming the storage the
 * file starts with, which is in use. Nothing in the file's own
 * bookkeeping says so afterwards: the allocation is recorded at the
 * address it was given.
 *
 * The size asked for there is small, so that an allocation which is not
 * refused writes inside the file and leaves a failure to be reported
 * rather than a fixture to be pieced back together.
 *
 * The fixture is a memory file with no interpreter over it. What is
 * under test sits below the object layer, so a PostScript-level test
 * cannot present a slot number that far outside a composite, and the
 * pair is reached directly. */

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

#define ENT_SZ 16u

int main(void)
{
    Xpost_Memory_File mem;
    unsigned int a = 0, b = 0;
    unsigned int aadr = 0, badr = 0;
    unsigned int held_used, held_max;
    unsigned int top = 0xdeadbeefu;
    unsigned char pattern[ENT_SZ];
    unsigned char readback[ENT_SZ];
    unsigned char intruder[ENT_SZ];
    unsigned char dest[8];
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

    /* two allocations, so that a write past the end of the first has
       somewhere identifiable to land */
    if (!xpost_memory_table_alloc(&mem, ENT_SZ, 0, &a) ||
        !xpost_memory_table_alloc(&mem, ENT_SZ, 0, &b))
    {
        report_failure("could not allocate the two entities");
        return verdict();
    }
    if (!xpost_memory_table_get_addr(&mem, a, &aadr) ||
        !xpost_memory_table_get_addr(&mem, b, &badr))
    {
        report_failure("could not read the entities' addresses");
        return verdict();
    }
    check(badr > aadr, "the second allocation lies above the first");

    for (i = 0; i < ENT_SZ; i++)
        pattern[i] = (unsigned char)(0xa0 + i);
    memset(intruder, 0x5a, sizeof intruder);

    if (!xpost_memory_put(&mem, b, 0, ENT_SZ, pattern) ||
        !xpost_memory_put(&mem, a, 0, ENT_SZ, pattern))
    {
        report_failure("could not fill the two entities");
        return verdict();
    }

    /* the ordinary case, so that the refusals below are refusals of
       something this pair otherwise does */
    memset(readback, 0, sizeof readback);
    check(xpost_memory_get(&mem, a, 0, ENT_SZ, readback) == 1,
          "a read of the whole allocation is accepted");
    check(memcmp(readback, pattern, ENT_SZ) == 0,
          "the whole allocation reads back what was written");

    /* the last slot inside the allocation: sixteen bytes hold four
       four-byte slots, numbered 0 to 3. It is written with the bytes
       already there, so the fill above still describes the allocation. */
    check(xpost_memory_put(&mem, a, 3, 4, pattern + 12) == 1,
          "a write to the last slot inside the allocation is accepted");
    memset(readback, 0, sizeof readback);
    check(xpost_memory_get(&mem, a, 3, 4, readback) == 1,
          "a read of the last slot inside the allocation is accepted");
    check(memcmp(readback, pattern + 12, 4) == 0,
          "the last slot reads back its own bytes");

    /* the first slot outside it */
    check(xpost_memory_put(&mem, a, ENT_SZ / 4u, 4, intruder) == 0,
          "a write to the first slot past the allocation is refused");
    check(xpost_memory_get(&mem, a, ENT_SZ / 4u, 4, readback) == 0,
          "a read of the first slot past the allocation is refused");

    /* the byte the second allocation begins at, counted from the first
       as single-byte slots: a write there that is not refused overwrites
       the neighbour, whatever the two allocations' spacing */
    check(xpost_memory_put(&mem, a, badr - aadr, 1, intruder) == 0,
          "a write reaching the next allocation is refused");
    memset(readback, 0, sizeof readback);
    check(xpost_memory_get(&mem, b, 0, ENT_SZ, readback) == 1,
          "the neighbour still reads");
    check(memcmp(readback, pattern, ENT_SZ) == 0,
          "the neighbour's bytes survive the refused writes");

    /* A slot number whose product with the slot size fills the width
       the index is counted in: 0x20000000 slots of 8 bytes come to
       exactly 2^32, which is zero in that width and so the base of the
       allocation as an address. Taken wider, it is far outside. */
    check(xpost_memory_put(&mem, a, 0x20000000u, 8, intruder) == 0,
          "a write whose slot product wraps the index width is refused");
    memset(readback, 0, sizeof readback);
    check(xpost_memory_get(&mem, a, 0, ENT_SZ, readback) == 1,
          "the allocation still reads");
    check(memcmp(readback, pattern, ENT_SZ) == 0,
          "the allocation's own bytes survive the wrapping write");

    memset(dest, 0x5a, sizeof dest);
    check(xpost_memory_get(&mem, a, 0x20000000u, 8, dest) == 0,
          "a read whose slot product wraps the index width is refused");
    for (i = 0; i < sizeof dest; i++)
    {
        if (dest[i] != 0x5a)
        {
            report_failure("a refused read copied byte %u", i);
            break;
        }
    }

    /* A cursor in the eight bytes below the top of the file's address
       range, and capacity as far as the range goes. The rounding up the
       allocator does to it reaches past the last address the file has,
       which is a place no growth can put storage. */
    held_used = mem.used;
    held_max = mem.max;
    mem.used = 0xfffffffbu;
    mem.max = 0xffffffffu;

    check(xpost_memory_file_alloc(&mem, 16, &top) == 0,
          "an allocation rounded past the top of the address range is refused");
    check(top == 0xdeadbeefu,
          "a refused allocation hands back no address");
    check(mem.used == 0xfffffffbu,
          "a refused allocation leaves the file's cursor where it was");

    mem.used = held_used;
    mem.max = held_max;

    /* the allocation the file starts with still holds what was put in
       it, which a refusal that handed out the bottom of the file as the
       top of it would have cleared */
    memset(readback, 0, sizeof readback);
    check(xpost_memory_get(&mem, a, 0, ENT_SZ, readback) == 1,
          "the allocation reads after the refusal");
    check(memcmp(readback, pattern, ENT_SZ) == 0,
          "the allocation's bytes survive the refused allocation");

    xpost_memory_file_exit(&mem);
    xpost_quit();

    return verdict();
}
