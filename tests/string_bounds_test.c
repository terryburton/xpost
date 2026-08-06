/* xpost_string_get_pointer bounds discipline: a string object whose ent
   field is corrupt -- or holds the constructors' -1 failure sentinel,
   which wraps to UINT_MAX -- must yield NULL, not a wild pointer past
   the memory table. Uses the internal headers deliberately: the guard
   sits below the operator layer, where no PostScript-level test can
   place a corrupt object. */

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

#include "xpost_test.h"

int main(void)
{
    Xpost_Context *ctx;
    Xpost_Memory_File *mem;
    Xpost_Object s;
    char *p;

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

    s = xpost_string_cons(ctx, 5, "hello");
    check(xpost_object_get_type(s) == stringtype, "a fresh string constructs");

    p = xpost_string_get_pointer(ctx, s);
    check(p != NULL && memcmp(p, "hello", 5) == 0,
          "a valid string yields its bytes");

    mem = xpost_context_select_memory(ctx, s);

    /* one past the last allocated slot: the first invalid ent */
    p = xpost_string_get_pointer(ctx, xpost_object_set_ent(s, mem->table.nextent));
    check(p == NULL, "an ent one past the table yields NULL");

    /* far out of range, as a stray corruption would be */
    p = xpost_string_get_pointer(ctx, xpost_object_set_ent(s, 0x7fffffff));
    check(p == NULL, "a wildly corrupt ent yields NULL");

    /* the constructors' failure sentinel: get_ent reads it back as -1,
       which must not wrap into an index */
    p = xpost_string_get_pointer(ctx, xpost_object_set_ent(s, (unsigned int)-1));
    check(p == NULL, "the -1 sentinel ent yields NULL");

    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
