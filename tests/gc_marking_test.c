/* What a collection keeps and what it hands back.
 *
 * The collector's root set is the stacks: everything reachable from
 * them survives, and every allocation nothing reaches is returned to
 * the free list. Both halves are asserted here of the same allocation,
 * one after the other, because either half alone is held by a collector
 * that does nothing -- one that never reclaims keeps what is referred
 * to, and one that never marks reclaims what is not.
 *
 * A stack is read as far as its elements go, and the slots above them
 * keep the bytes of the elements that were there. So the allocation is
 * given up by popping it rather than by never pushing it: a stack read
 * one slot too far reaches an object the program has finished with and
 * marks it, and the allocation stays out of use for the rest of the
 * run. Nothing about the stack says so -- its length is right and its
 * elements are right -- and nothing about the allocation says so
 * either, unless something asks whether it came back.
 *
 * What answers that is the tag the memory table holds against the
 * allocation. A live allocation carries the type it was made for, and
 * one on the free list carries none; the sweep is what moves it from
 * the first to the second. Reading the tag is therefore reading the
 * collector's decision about that one allocation, rather than a total
 * that any other allocation's fate would also move.
 *
 * A constructor stashes what it built on the hold stack, so that a
 * collection provoked from the caller before the new object is anywhere
 * else does not take it from under them; the interpreter empties that
 * stack between operator executions. Both things are done here for the
 * same reason they are done there, and emptying it is what leaves the
 * operand stack the only thing referring to the object -- and, once
 * that too has given it up, leaves two stacks each holding it in the
 * first slot above their elements.
 *
 * Each composite type with an arm of its own in the marker is asked,
 * since each reaches its allocation by its own route. */

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
#include "xpost_array.h"
#include "xpost_dict.h"
#include "xpost_garbage.h"

#include "xpost_test.h"

static unsigned int tag_of(Xpost_Context *ctx, Xpost_Object o)
{
    Xpost_Memory_File *mem = xpost_context_select_memory(ctx, o);
    unsigned int tag = (unsigned int)-1;

    if (!mem || !xpost_memory_table_get_tag(mem, (unsigned int)
                                            xpost_object_get_ent(o), &tag))
        return (unsigned int)-1;
    return tag & XPOST_OBJECT_TAG_DATA_TYPE_MASK;
}

/* Collect the local memory, marking across both memory files, and hold
   the collection itself to having succeeded: a collector that gave up
   part way reclaims nothing, and a test reading only what came back
   would call that a survival. */
static void collect(Xpost_Context *ctx, const char *when)
{
    if (xpost_garbage_collect(ctx->lo, 1, 1) < 0)
        report_failure("the collection %s failed", when);
}

/* Put the object where the operand stack is the only thing referring to
   it and collect -- the collector must keep it; then take it off, leave
   nothing referring to it and collect again -- the collector must hand
   it back. The object is named by type so a failure says which arm of
   the marker answered it. */
static void keeps_then_reclaims(Xpost_Context *ctx, Xpost_Object o,
                                unsigned int type, const char *what)
{
    Xpost_Object popped;

    if (xpost_object_get_type(o) != (Xpost_Object_Type)type)
    {
        report_failure("could not construct the %s", what);
        return;
    }
    if (tag_of(ctx, o) != type)
    {
        report_failure("a fresh %s does not carry its type", what);
        return;
    }

    if (!xpost_stack_push(ctx->lo, ctx->os, o))
    {
        report_failure("could not put the %s on the operand stack", what);
        return;
    }
    xpost_stack_clear(ctx->lo, ctx->hold);
    collect(ctx, "of an allocation the operand stack refers to");
    if (tag_of(ctx, o) != type)
        report_failure("a %s on the operand stack was reclaimed", what);

    popped = xpost_stack_pop(ctx->lo, ctx->os);
    if (xpost_object_get_type(popped) != (Xpost_Object_Type)type)
    {
        report_failure("the %s did not come back off the stack", what);
        return;
    }

    xpost_stack_clear(ctx->lo, ctx->hold);
    collect(ctx, "of an allocation nothing refers to");
    if (tag_of(ctx, o) != 0)
        report_failure("a %s the stacks have given up was not reclaimed", what);
}

int main(void)
{
    Xpost_Context *ctx;
    char text[64];

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

    memset(text, 'x', sizeof text);
    keeps_then_reclaims(ctx, xpost_string_cons(ctx, sizeof text, text),
                        stringtype, "string");
    keeps_then_reclaims(ctx, xpost_array_cons(ctx, 16), arraytype, "array");
    keeps_then_reclaims(ctx, xpost_dict_cons(ctx, 8), dicttype, "dictionary");

    xpost_destroy(ctx);
    xpost_quit();

    return verdict();
}
