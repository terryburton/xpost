/* How a dictionary's hash spreads its keys.

   A dictionary looks a key up by probing linearly from the slot its
   hash names, so the cost of every lookup is the distance entries sit
   from their home slots. A hash that reads a field of the object which
   carries no part of the key's value collapses every key of that type
   onto one slot, and the probe becomes a scan of the whole table --
   correct, and unboundedly slow. Nothing about the dictionary's
   contents shows that: it holds the right entries either way.

   So measure the spread directly, from the hash values the dictionary
   stored beside its entries and the distance each entry sits from the
   slot its hash names. Number keys are the interesting case -- they are
   canonicalised to a type that is not a composite, so a hash reading
   composite fields sees nothing of them -- but every key type is held
   to the same bound, in whichever build this is: the fields an object
   occupies differ between the two, and a hash that suits one layout can
   read only padding in the other. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_dict.h"
#include "xpost_name.h"
#include "xpost_string.h"
#include "xpost_error.h"

#include "xpost_test.h"

#define KEYS 20000

static int cmp_uint(const void *a, const void *b)
{
    unsigned int x = *(const unsigned int *)a;
    unsigned int y = *(const unsigned int *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* Read the spread of a filled dictionary and hold it to two bounds: the
   keys must take many distinct hash values, and each entry must sit
   near the slot its own hash names. The first bound catches a hash that
   sees nothing of the key; the second catches one that sees too little
   of it. */
static void spread(Xpost_Context *ctx, Xpost_Object d, const char *what,
                   unsigned int n)
{
    Xpost_Memory_File *mem = xpost_context_select_memory(ctx, d);
    dichead *dp = xpost_dict_head(mem, xpost_object_get_ent(d));
    dicrec *tp = xpost_dict_table_of(dp);
    unsigned int tabn = DICTABN(dp->sz);
    unsigned int *hv;
    unsigned int i;
    unsigned int held = 0;
    unsigned int distinct = 0;
    unsigned long long displacement = 0;
    double mean;

    hv = malloc(tabn * sizeof *hv);
    if (!hv)
    {
        report_failure("%s: out of memory", what);
        return;
    }

    for (i = 0; i < tabn; i++)
    {
        unsigned int home;

        if (xpost_object_get_type(tp[i].key) == nulltype)
            continue;
        hv[held++] = tp[i].hash;
        home = tp[i].hash % tabn;
        displacement += (i >= home) ? (i - home) : (tabn - home + i);
    }

    if (held != n)
    {
        report_failure("%s: the table holds %u entries, not %u",
                       what, held, n);
        free(hv);
        return;
    }

    qsort(hv, held, sizeof *hv, cmp_uint);
    for (i = 0; i < held; i++)
        if (i == 0 || hv[i] != hv[i - 1])
            distinct++;

    mean = (double)displacement / (double)held;
    printf("%s: %u entries, %u distinct hashes, mean displacement %.2f\n",
           what, held, distinct, mean);

    /* Half the keys taking distinct hash values is far below what any
       spreading hash gives (the measure is well over nine tenths) and
       far above what a hash blind to the key gives (a handful, whatever
       the key count). */
    check(distinct * 2 >= held, "the keys take many distinct hash values");

    /* An entry a long way from its home slot is one every lookup that
       lands in the run walks past. Eight is generous for a table twice
       the size of the entry count; a collapsed hash gives thousands. */
    check(mean < 8.0, "entries sit near the slot their hash names");

    free(hv);
}

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Object d;
    unsigned int i;

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

    /* integer keys */
    d = xpost_dict_cons(ctx, KEYS);
    for (i = 0; i < KEYS; i++)
        if (xpost_dict_put(ctx, d, xpost_int_cons((integer)i),
                           xpost_int_cons((integer)i)) != 0)
        {
            report_failure("an integer key was refused");
            break;
        }
    spread(ctx, d, "integer keys", KEYS);

    /* real keys: the same canonical type, a different set of values */
    d = xpost_dict_cons(ctx, KEYS);
    for (i = 0; i < KEYS; i++)
        if (xpost_dict_put(ctx, d, xpost_real_cons((real)i + (real)0.5),
                           xpost_int_cons((integer)i)) != 0)
        {
            report_failure("a real key was refused");
            break;
        }
    spread(ctx, d, "real keys   ", KEYS);

    /* name keys, which is what string keys become */
    d = xpost_dict_cons(ctx, KEYS);
    for (i = 0; i < KEYS; i++)
    {
        char buf[32];
        sprintf(buf, "k%u", i);
        if (xpost_dict_put(ctx, d, xpost_name_cons(ctx, buf),
                           xpost_int_cons((integer)i)) != 0)
        {
            report_failure("a name key was refused");
            break;
        }
    }
    spread(ctx, d, "name keys   ", KEYS);

    /* every value put under a number key is retrievable under it: a
       spreading hash that disagreed with the equality test would lose
       entries rather than merely slow the search */
    d = xpost_dict_cons(ctx, KEYS);
    for (i = 0; i < KEYS; i++)
        check(xpost_dict_put(ctx, d, xpost_int_cons((integer)i),
                             xpost_int_cons((integer)i + 1)) == 0,
              "an integer key is accepted");
    for (i = 0; i < KEYS; i++)
    {
        Xpost_Object v = xpost_dict_get(ctx, d, xpost_int_cons((integer)i));
        if (xpost_object_get_type(v) != integertype ||
            v.int_.val != (integer)i + 1)
        {
            report_failure("integer key %u did not read back", i);
            break;
        }
    }

    /* an integer and the equal real name the same entry, and a string
       and the equal name do too (PLRM 3.3.5): the hash must agree with
       that, or the second of each pair opens a second entry */
    {
        Xpost_Object m = xpost_dict_cons(ctx, 8);
        Xpost_Object s = xpost_string_cons(ctx, 3, "abc");
        Xpost_Object v;

        check(xpost_dict_put(ctx, m, xpost_int_cons(7),
                             xpost_int_cons(70)) == 0, "an integer key is put");
        check(xpost_dict_put(ctx, m, xpost_real_cons((real)7.0),
                             xpost_int_cons(71)) == 0, "a real key is put");
        check(xpost_dict_length_memory(xpost_context_select_memory(ctx, m), m)
              == 1, "an integer key and the equal real are one entry");
        v = xpost_dict_get(ctx, m, xpost_int_cons(7));
        check(xpost_object_get_type(v) == integertype && v.int_.val == 71,
              "the equal real key overwrote the integer key's value");

        check(xpost_dict_put(ctx, m, s, xpost_int_cons(80)) == 0,
              "a string key is put");
        v = xpost_dict_get(ctx, m, xpost_name_cons(ctx, "abc"));
        check(xpost_object_get_type(v) == integertype && v.int_.val == 80,
              "a string key is found again under the equal name");
    }

    /* keys that are neither composite, name nor number still land where
       they can be found again */
    {
        Xpost_Object m = xpost_dict_cons(ctx, 8);
        Xpost_Object v;

        check(xpost_dict_put(ctx, m, xpost_bool_cons(1),
                             xpost_int_cons(1)) == 0, "a true key is put");
        check(xpost_dict_put(ctx, m, xpost_bool_cons(0),
                             xpost_int_cons(2)) == 0, "a false key is put");
        check(xpost_dict_put(ctx, m, mark, xpost_int_cons(3)) == 0,
              "a mark key is put");
        check(xpost_dict_length_memory(xpost_context_select_memory(ctx, m), m)
              == 3, "true, false and a mark are three distinct keys");
        v = xpost_dict_get(ctx, m, xpost_bool_cons(1));
        check(xpost_object_get_type(v) == integertype && v.int_.val == 1,
              "a true key reads back");
        v = xpost_dict_get(ctx, m, xpost_bool_cons(0));
        check(xpost_object_get_type(v) == integertype && v.int_.val == 2,
              "a false key reads back");
        v = xpost_dict_get(ctx, m, mark);
        check(xpost_object_get_type(v) == integertype && v.int_.val == 3,
              "a mark key reads back");
    }

    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
