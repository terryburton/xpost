/* Dictionary growth bookkeeping: dicgrow exchanges the storage identity
   of the old and new allocations in the memory table, and an ent's
   (adr, sz, used) must travel together -- a swap that leaves `used`
   behind gives each ent a byte count belonging to the other's
   allocation. Grow a dict well past its birth size, then sweep the
   whole table for the invariant used <= sz, which the stranded half of
   a partial swap violates. Uses the internal headers deliberately: the
   table is below anything a PostScript-level test can observe. */

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

static int failures = 0;

static void check(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void sweep(Xpost_Memory_File *mem, const char *what)
{
    unsigned int ent;

    for (ent = 0; ent < mem->table.nextent; ent++)
    {
        /* a few special entities deliberately report sz 0 over a live
           data area (the free-list heads, the operator table); the
           invariant binds only real allocations */
        if (mem->table.tab[ent].sz > 0 &&
            mem->table.tab[ent].used > mem->table.tab[ent].sz)
        {
            printf("FAIL: %s: ent %u used %u > sz %u\n", what, ent,
                   mem->table.tab[ent].used, mem->table.tab[ent].sz);
            failures++;
        }
    }
}

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Object d;
    int i;

    if (!xpost_init())
    {
        printf("FAIL: xpost_init\n");
        return 1;
    }
    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_RETURN, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        printf("FAIL: xpost_create\n");
        return 1;
    }

    sweep(ctx->lo, "before growth (local)");
    sweep(ctx->gl, "before growth (global)");

    d = xpost_dict_cons(ctx, 1);
    check(xpost_object_get_type(d) == dicttype, "a fresh dict constructs");

    /* overfill far past the birth size, forcing repeated regrowth */
    for (i = 0; i < 100; i++)
        xpost_dict_put(ctx, d, xpost_int_cons(i), xpost_int_cons(i));

    for (i = 0; i < 100; i++)
    {
        Xpost_Object v = xpost_dict_get(ctx, d, xpost_int_cons(i));
        if (xpost_object_get_type(v) != integertype || v.int_.val != i)
        {
            printf("FAIL: grown dict lost key %d\n", i);
            failures++;
            break;
        }
    }

    sweep(ctx->lo, "after growth (local)");
    sweep(ctx->gl, "after growth (global)");

    xpost_destroy(ctx);
    xpost_quit();

    if (failures)
    {
        printf("FAILURES: %d\n", failures);
        return 1;
    }
    printf("SUCCESS\n");
    return 0;
}
