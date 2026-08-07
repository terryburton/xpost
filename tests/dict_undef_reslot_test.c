/* What a deletion does to the entries that follow it.
 *
 * The dictionary probes linearly from a key's home slot, so an entry
 * that collided with an earlier one sits past its home with no gap
 * between the two. Emptying a slot in the middle of such a run would cut
 * every entry beyond it off from its home, so the deletion walks the
 * rest of the run and moves back into the hole exactly those entries
 * whose home is not itself inside the stretch being closed up (Knuth
 * TAOCP vol.3, 6.4, algorithm R).
 *
 * Both halves of that rule are boundaries, and a dictionary is equally
 * willing to answer with the wrong one:
 *
 *   an entry whose home is the emptied slot must move into it -- left
 *   where it is, nothing reaches it, because a probe from its home stops
 *   at the hole;
 *
 *   an entry already sitting at its own home must not move -- moved back
 *   into the hole, nothing reaches it either, because a probe from its
 *   home now stops at the slot it left.
 *
 * and each of the two arises twice, once in a run that lies along the
 * table and once in a run that has wrapped past its end, which the
 * deletion decides with a different comparison.
 *
 * Neither shape can be asked for from PostScript: which slot a key takes
 * is the hash's business. So the keys here are chosen by the slot they
 * take -- found by putting each into an empty dictionary of the same
 * shape and seeing where it lands -- and every configuration is checked
 * to be the configuration intended before the deletion that tests it.
 * What is read afterwards is the dictionary's own answer for the
 * surviving key: an entry no probe can reach is an entry the dictionary
 * says it does not have, however plainly it sits in the table. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_dict.h"

#include "xpost_test.h"

/* the dictionary every configuration is built in: small enough that its
   table is a handful of slots, and never filled far enough to grow */
#define DICT_SIZE 8u

/* how many integer keys are sorted by the slot they take. A table of
   twenty-one slots takes far fewer than this to fill every slot several
   times over; the surplus is what makes a pair for any two homes
   available without searching for one. */
#define NCANDIDATE 2000

/* The dictionary's own table, and how many slots it has. A dictionary
   whose entity the memory table does not know is not read through: the
   configurations below are built on an interpreter that has to have
   started, and a wild pointer here would say so as a crash rather than
   as the failure it is. */
static dicrec *table_of(Xpost_Context *ctx, Xpost_Object d, unsigned int *n)
{
    unsigned int ent = xpost_object_get_ent(d);
    dichead *dp;

    if (xpost_object_get_type(d) != dicttype || !xpost_ent_valid(ctx->lo, ent))
        return NULL;
    dp = xpost_dict_head(ctx->lo, ent);
    *n = DICTABN(dp->sz);
    return xpost_dict_table_of(dp);
}

/* The slot a key takes in an empty table of this shape, which is its
   home: nothing is in the way of the first probe. The dictionary is
   emptied again afterwards, and a single entry leaves nothing for the
   deletion to re-slot, so the next key is asked the same question. */
static int home_of(Xpost_Context *ctx, Xpost_Object scratch, int key,
                   unsigned int *home)
{
    dicrec *tp;
    unsigned int n = 0;
    unsigned int i;
    int found = -1;

    if (xpost_dict_put(ctx, scratch, xpost_int_cons(key), xpost_int_cons(key)))
        return 0;
    /* the put may have moved the memory file: derive after it */
    tp = table_of(ctx, scratch, &n);
    if (!tp)
        return 0;
    for (i = 0; i < n; i++)
        if (xpost_object_get_type(tp[i].key) != nulltype)
        {
            found = (int)i;
            break;
        }
    if (xpost_dict_undef(ctx, scratch, xpost_int_cons(key)))
        return 0;
    if (found < 0)
        return 0;
    *home = (unsigned int)found;
    return 1;
}

/* the slot an entry occupies, found by the value put with it */
static int slot_of(Xpost_Context *ctx, Xpost_Object d, int value)
{
    unsigned int n = 0;
    dicrec *tp = table_of(ctx, d, &n);
    unsigned int i;

    if (!tp)
        return -1;
    for (i = 0; i < n; i++)
        if (xpost_object_get_type(tp[i].key) != nulltype &&
            xpost_object_get_type(tp[i].value) == integertype &&
            tp[i].value.int_.val == value)
            return (int)i;
    return -1;
}

/* the value the dictionary answers for a key, or a number no
   configuration uses when it answers with nothing */
#define NO_VALUE (-1)

static int value_of(Xpost_Context *ctx, Xpost_Object d, int key)
{
    Xpost_Object v = xpost_dict_get(ctx, d, xpost_int_cons(key));

    if (xpost_object_get_type(v) != integertype)
        return NO_VALUE;
    return v.int_.val;
}

static unsigned int homes[NCANDIDATE];

/* the first candidate keyed to a given slot, other than one already
   taken */
static int key_homed_at(unsigned int home, int taken)
{
    int k;

    for (k = 0; k < NCANDIDATE; k++)
        if (homes[k] == home && k != taken)
            return k;
    return -1;
}

/* One configuration: two entries, the first at `first_home` and the
   second landing immediately after it, then the first deleted and the
   second asked for. Where the survivor is homed decides where it must
   end up -- in the hole if its home is the emptied slot, where it stands
   if it is already at its own home -- and both the slot and the
   dictionary's answer for the key are read, so that a rule that moves
   the right entry to the wrong place is not passed by a lookup that
   happens to walk onto it.
 */
static void one_run(Xpost_Context *ctx, unsigned int first_home,
                    unsigned int second_home, const char *what)
{
    Xpost_Object d;
    unsigned int n = 0;
    unsigned int want;
    int ka, kb;
    int sa, sb;

    ka = key_homed_at(first_home, -1);
    kb = key_homed_at(second_home, ka);
    if (ka < 0 || kb < 0)
    {
        report_failure("%s: no pair of keys homed at %u and %u", what,
                       first_home, second_home);
        return;
    }

    d = xpost_dict_cons(ctx, DICT_SIZE);
    if (!table_of(ctx, d, &n))
    {
        report_failure("%s: the dictionary did not construct", what);
        return;
    }

    if (xpost_dict_put(ctx, d, xpost_int_cons(ka), xpost_int_cons(11)) ||
        xpost_dict_put(ctx, d, xpost_int_cons(kb), xpost_int_cons(22)))
    {
        report_failure("%s: a put was refused", what);
        return;
    }

    /* the configuration is the one intended: the first entry at the slot
       it is homed to, the second in the slot immediately after it,
       counting round the end of the table */
    sa = slot_of(ctx, d, 11);
    sb = slot_of(ctx, d, 22);
    if (sa < 0 || sb < 0)
    {
        report_failure("%s: an entry is not in the table (%d, %d)", what,
                       sa, sb);
        return;
    }
    if ((unsigned int)sa != first_home)
    {
        report_failure("%s: the first entry sits at %d, not at its home %u",
                       what, sa, first_home);
        return;
    }
    if ((unsigned int)sb != (first_home + 1u) % n)
    {
        report_failure("%s: the second entry sits at %d, not at %u", what,
                       sb, (first_home + 1u) % n);
        return;
    }

    /* both are reachable before anything is deleted, so that a failure
       below is the deletion's and not the arrangement's */
    if (value_of(ctx, d, ka) != 11 || value_of(ctx, d, kb) != 22)
    {
        report_failure("%s: an entry is unreachable before the deletion",
                       what);
        return;
    }

    if (xpost_dict_undef(ctx, d, xpost_int_cons(ka)))
    {
        report_failure("%s: the deletion was refused", what);
        return;
    }

    /* the slot the rule owes the survivor: the emptied one if that is
       its home, the one it stands in if it is already at its home */
    want = (second_home == first_home) ? first_home : (unsigned int)sb;
    if (slot_of(ctx, d, 22) != (int)want)
        report_failure("%s: the surviving entry sits at %d, not at %u", what,
                       slot_of(ctx, d, 22), want);

    if (value_of(ctx, d, ka) != NO_VALUE)
        report_failure("%s: the deleted key is still answered", what);
    if (value_of(ctx, d, kb) != 22)
        report_failure("%s: the surviving entry is unreachable after the "
                       "deletion", what);
    if (xpost_dict_length_memory(ctx->lo, d) != 1)
        report_failure("%s: the dictionary holds %u entries, not one", what,
                       xpost_dict_length_memory(ctx->lo, d));
}

/* Breadth over the same rule: a dictionary filled to its capacity, with
   entries deleted and put back in several passes, must answer for every
   key it still holds. Any run long enough for the rule to matter arises
   here by collision rather than by construction. */
static void filled_and_emptied(Xpost_Context *ctx)
{
    Xpost_Object d;
    const int nkey = 40;
    int pass;
    int i;

    d = xpost_dict_cons(ctx, (unsigned int)nkey);
    if (xpost_object_get_type(d) != dicttype)
    {
        report_failure("the filled dictionary did not construct");
        return;
    }
    for (i = 0; i < nkey; i++)
        if (xpost_dict_put(ctx, d, xpost_int_cons(i), xpost_int_cons(i + 1)))
        {
            report_failure("filling: a put was refused at key %d", i);
            return;
        }

    /* each pass deletes a different residue class and puts it back, so
       every entry is at some point the one a hole is closed over */
    for (pass = 1; pass <= 5; pass++)
    {
        for (i = 0; i < nkey; i++)
            if (i % 6 == pass % 6)
                if (xpost_dict_undef(ctx, d, xpost_int_cons(i)))
                {
                    report_failure("pass %d: the deletion of %d was refused",
                                   pass, i);
                    return;
                }
        for (i = 0; i < nkey; i++)
        {
            int want = (i % 6 == pass % 6) ? NO_VALUE : i + 1;

            if (value_of(ctx, d, i) != want)
            {
                report_failure("pass %d: key %d answers %d, not %d", pass, i,
                               value_of(ctx, d, i), want);
                return;
            }
        }
        for (i = 0; i < nkey; i++)
            if (i % 6 == pass % 6)
                if (xpost_dict_put(ctx, d, xpost_int_cons(i),
                                   xpost_int_cons(i + 1)))
                {
                    report_failure("pass %d: the re-put of %d was refused",
                                   pass, i);
                    return;
                }
        for (i = 0; i < nkey; i++)
            if (value_of(ctx, d, i) != i + 1)
            {
                report_failure("pass %d: key %d is unreachable after being "
                               "put back", pass, i);
                return;
            }
    }
}

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Object scratch;
    unsigned int n = 0;
    unsigned int interior;
    int k;
    int mapped = 0;

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

    scratch = xpost_dict_cons(ctx, DICT_SIZE);
    if (!table_of(ctx, scratch, &n))
    {
        report_failure("the scratch dictionary did not construct");
        return verdict();
    }
    check(n >= 4, "the table has slots enough for a run and a wrap");

    for (k = 0; k < NCANDIDATE; k++)
    {
        if (!home_of(ctx, scratch, k, &homes[k]))
        {
            report_failure("could not find the home slot of key %d", k);
            return verdict();
        }
        mapped++;
    }
    check(mapped == NCANDIDATE,
          "every candidate key was sorted by the slot it takes");

    /* a slot with room for a follower after it, away from the ends */
    interior = n / 2u;

    /* along the table */
    one_run(ctx, interior, interior,
            "a follower homed to the emptied slot");
    one_run(ctx, interior, (interior + 1u) % n,
            "a follower already at its own home");

    /* and round the end of it, where the deletion compares the other way */
    one_run(ctx, n - 1u, n - 1u,
            "a wrapped follower homed to the emptied slot");
    one_run(ctx, n - 1u, 0u,
            "a wrapped follower already at its own home");

    filled_and_emptied(ctx);

    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
