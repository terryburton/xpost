/* Indexing a segmented stack from the bottom up: which index names an
   element, and which names nothing.
 *
 * A stack is a chain of fixed-length segments, so a bottom-up index is
 * answered in two steps -- choose the segment the index falls in, then
 * take the slot within it -- and each step has a boundary the other
 * cannot see. The segment step counts whole segments off the index, so
 * an index equal to the segment length is the first element of the
 * second segment and not one past the end of the first. The slot step
 * compares against how much of the chosen segment is occupied, so an
 * index equal to that is the first slot past the elements, and a segment
 * holds slots there whether or not any element does.
 *
 * That second boundary is why the stack is popped before it is asked.
 * A popped slot keeps the bytes of the element that was there, so an
 * index one past the top names a slot holding a whole, well-formed
 * object -- and a bound that admits it hands that object back as though
 * it were still on the stack, while one that admits it for writing
 * stores through a slot the stack has already given up. Asked of a
 * stack that was never popped, the slot reads as a zeroed object, which
 * is what a refusal answers with, and both bounds look alike.
 *
 * The fixture is a memory file with no interpreter over it: the two
 * boundaries are a segment length apart in a structure the operand
 * stack shares, and reaching them through the interpreter would mean
 * driving it to a precise depth to say anything about either. */

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

/* the element pushed at each index */
#define ELEMENT_AT(i) ((i) + 1)

int main(void)
{
    Xpost_Memory_File mem;
    unsigned int stackadr = 0;
    const int seg = XPOST_STACK_SEGMENT_SIZE;
    Xpost_Object o;
    int count;
    int i;

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
    if (!xpost_stack_init(&mem, &stackadr))
    {
        report_failure("xpost_stack_init");
        return verdict();
    }

    /* Fill the first segment exactly, and hold the segment length the
       indices below are written in to what the stack does with it: the
       root segment takes that many elements and no more. */
    for (i = 0; i < seg; i++)
    {
        if (!xpost_stack_push(&mem, stackadr, xpost_int_cons(ELEMENT_AT(i))))
        {
            report_failure("push %d refused while filling the first segment", i);
            return verdict();
        }
    }
    check((int)xpost_stack_at(&mem, stackadr)->top == seg,
          "the first segment holds a segment's worth of elements");

    /* three more, so the second segment is started and is not full */
    for (i = seg; i < seg + 3; i++)
    {
        if (!xpost_stack_push(&mem, stackadr, xpost_int_cons(ELEMENT_AT(i))))
        {
            report_failure("push %d refused", i);
            return verdict();
        }
    }
    check((int)xpost_stack_at(&mem, stackadr)->top == seg,
          "the first segment takes no more than that");
    count = xpost_stack_count(&mem, stackadr);
    check(count == seg + 3, "the stack counts every element pushed");

    /* inside the first segment */
    o = xpost_stack_bottomup_fetch(&mem, stackadr, 0);
    check(xpost_object_get_type(o) == integertype &&
          o.int_.val == ELEMENT_AT(0),
          "the bottom index names the first element pushed");
    o = xpost_stack_bottomup_fetch(&mem, stackadr, seg - 1);
    check(xpost_object_get_type(o) == integertype &&
          o.int_.val == ELEMENT_AT(seg - 1),
          "the last index of the first segment names its own element");

    /* the segment boundary: this index is the second segment's first
       element, not one past the first segment's last */
    o = xpost_stack_bottomup_fetch(&mem, stackadr, seg);
    check(xpost_object_get_type(o) == integertype &&
          o.int_.val == ELEMENT_AT(seg),
          "the index one segment long names the second segment's first element");

    o = xpost_stack_bottomup_fetch(&mem, stackadr, count - 1);
    check(xpost_object_get_type(o) == integertype &&
          o.int_.val == ELEMENT_AT(count - 1),
          "the top index names the last element pushed");

    /* the same boundary for writing */
    check(xpost_stack_bottomup_replace(&mem, stackadr, seg,
                                       xpost_int_cons(-7)) == 1,
          "a write at the index one segment long is accepted");
    o = xpost_stack_bottomup_fetch(&mem, stackadr, seg);
    check(xpost_object_get_type(o) == integertype && o.int_.val == -7,
          "and lands where the read of that index found its element");
    check(xpost_stack_bottomup_replace(&mem, stackadr, seg,
                                       xpost_int_cons(ELEMENT_AT(seg))) == 1,
          "so the element can be put back");

    /* Give up the top element. Its slot keeps its bytes, so the first
       index past the top now names a slot holding a well-formed object
       rather than a zeroed one. */
    o = xpost_stack_pop(&mem, stackadr);
    check(xpost_object_get_type(o) == integertype &&
          o.int_.val == ELEMENT_AT(count - 1),
          "the pop answers the element that was on top");
    count = xpost_stack_count(&mem, stackadr);
    check(count == seg + 2, "the stack is one shorter for it");

    o = xpost_stack_bottomup_fetch(&mem, stackadr, count);
    check(xpost_object_get_type(o) == invalidtype,
          "the first index past the top names nothing, not the slot's bytes");

    check(xpost_stack_bottomup_replace(&mem, stackadr, count,
                                       xpost_int_cons(-9)) == 0,
          "a write to the first index past the top is refused");
    check(xpost_stack_count(&mem, stackadr) == count,
          "and leaves the stack the length it was");
    o = xpost_stack_bottomup_fetch(&mem, stackadr, count - 1);
    check(xpost_object_get_type(o) == integertype &&
          o.int_.val == ELEMENT_AT(count - 1),
          "and leaves the top element alone");

    /* past the end of the chain entirely */
    o = xpost_stack_bottomup_fetch(&mem, stackadr, count + 5 * seg);
    check(xpost_object_get_type(o) == invalidtype,
          "an index past the last segment names nothing");
    check(xpost_stack_bottomup_replace(&mem, stackadr, count + 5 * seg,
                                       xpost_int_cons(-9)) == 0,
          "a write past the last segment is refused");

    xpost_memory_file_exit(&mem);
    xpost_quit();

    return verdict();
}
