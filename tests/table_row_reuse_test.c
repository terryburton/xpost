/* That a row whose storage has gone can be issued again.
 *
 * The table's next-slot cursor only ever rises. That is right while
 * every row that stops being used keeps its storage: such a row goes on
 * a free list, and an allocation that takes it there gets the number and
 * the bytes together, so nothing is lost. A pass that rearranges the
 * arena breaks that pairing. It slides the live entities down over the
 * free blocks, so a free block's bytes are gone and its row describes
 * nothing -- and a row describing nothing is on no free list, which
 * means its number can never be handed out again. A job that fragments
 * and compacts repeatedly would spend the whole entity range without
 * spending memory, and reach the width limit with an arena that is
 * mostly empty.
 *
 * So the rows emptied that way need a list of their own, and this is
 * what asks for it. The two lists are disjoint by construction: a row is
 * either on a block free list with its storage, or on the row list with
 * none. Nothing here reads which list a row is on; the question asked is
 * only whether the cursor stops rising once numbers come back, since
 * that is the whole of what the limit depends on.
 *
 * The fixture is a memory file with no interpreter over it, as in
 * tests/free_list_size_rule_test.c, so that the sizes and the numbers
 * are asked of the table directly rather than through the object layer.
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

#define ROUND 20

/* This fixture has no interpreter and collects nothing, so it answers
   the question that leaves the allocator alone. */
static int fixture_initializing(void)
{
    return 1;
}

static void fixture_set_initializing(int initializing)
{
    (void)initializing;
}

int main(void)
{
    Xpost_Memory_File mem;
    unsigned int first[ROUND], second[ROUND];
    unsigned int i, j, before, after, reissued = 0;

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
    if (!xpost_memory_table_init(&mem, XPOST_MEMORY_TABLE_SPECIAL_FREE + 1))
    {
        report_failure("xpost_memory_table_init");
        return verdict();
    }
    if (!xpost_free_init(&mem))
    {
        report_failure("xpost_free_init");
        return verdict();
    }

    for (i = 0; i < ROUND; i++)
        if (!xpost_memory_table_alloc(&mem, 64, 0, &first[i]))
        {
            report_failure("could not allocate the %uth entity", i);
            return verdict();
        }
    before = mem.table.nextent;

    /* A row that still has its storage is not one of these: it belongs
       on a block free list, where the bytes are kept for reuse too.
       Asked here so that the release cannot quietly accept both and
       lose the storage of whatever it was given. */
    check(xpost_memory_table_release_row(&mem, first[0]) == 0,
          "a row that still holds storage is refused");

    for (i = 0; i < ROUND; i++)
    {
        /* what compaction leaves behind: the bytes are gone, and the
           row is handed back with them */
        mem.table.tab[first[i]].sz = 0;
        mem.table.tab[first[i]].adr = 0;
        if (!xpost_memory_table_release_row(&mem, first[i]))
        {
            report_failure("a row with no storage was refused");
            return verdict();
        }
    }

    check(mem.table.nextent == before,
          "releasing a row does not move the next-slot cursor");

    for (i = 0; i < ROUND; i++)
        if (!xpost_memory_table_alloc(&mem, 64, 0, &second[i]))
        {
            report_failure("could not allocate the %uth entity of the "
                           "second round", i);
            return verdict();
        }
    after = mem.table.nextent;

    check(after == before,
          "a round of allocations after the rows came back spends no new "
          "slot: the cursor is what the entity-width limit is measured "
          "against, and a compacting job that could not reissue would "
          "reach it with an arena that is mostly empty");

    for (i = 0; i < ROUND; i++)
        for (j = 0; j < ROUND; j++)
            if (second[i] == first[j])
                reissued++;
    check(reissued == ROUND,
          "every number issued in the second round is one of the released "
          "ones, rather than the cursor having been moved by something "
          "else while the released numbers went nowhere");

    /* The reissued rows must describe storage of their own. A row handed
       back with its address left as it was would name bytes another
       entity is now using, which is the fault this whole change is for. */
    for (i = 0; i < ROUND; i++)
    {
        if (mem.table.tab[second[i]].sz == 0)
        {
            report_failure("a reissued row describes no storage");
            break;
        }
        for (j = 0; j < ROUND; j++)
            if (i != j
                && mem.table.tab[second[i]].adr == mem.table.tab[second[j]].adr)
            {
                report_failure("two reissued rows name the same address");
                i = ROUND;
                break;
            }
    }

    xpost_memory_file_exit(&mem);
    xpost_quit();
    return verdict();
}
