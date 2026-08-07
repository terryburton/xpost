/* Snapshotting the top of a segmented stack: the walk converges, and on
   what.
 *
 * xpost_stack_peek_top copies the topmost n elements in one pass down
 * the segment chain, taking as many from each segment as it holds and
 * moving to the next only while elements are still wanted. Every
 * iteration therefore has to take at least one element, and the
 * condition that ends the walk is the only thing that makes it: a walk
 * still running when nothing more is wanted takes nothing, moves
 * nowhere, and never ends.
 *
 * That is why this runs where it does. copy and roll snapshot their
 * operands through this function and the interpreter's own start-up
 * uses both, so a walk that does not converge is not something the
 * suite can observe from the outside -- every test in it, including the
 * ones with nothing to do with stacks, stops at the same place before
 * reporting anything, and what is left is a harness noticing hours
 * later that a run never finished. A test that says so has to be one
 * that ends.
 *
 * So the walk is run in a child process under a bound on the work it
 * may do -- a CPU budget, which a converging walk is nowhere near and a
 * non-converging one spends -- and the parent, which does no walking,
 * reads back what the child made of the stack and holds it to the
 * elements that were pushed. A child that spent its budget is ended by
 * it and the parent says so; the answer, and the failure, are both the
 * parent's, and both arrive.
 *
 * The stack is built with a second segment holding fewer elements than
 * the first, so the requests below span the boundary: one that fits
 * inside the top segment, one that must continue into the segment
 * beneath it, and one for the whole stack, which is the largest request
 * the caller's contract allows and the one that ends with the walk
 * having taken exactly everything.
 *
 * The fixture is a memory file with no interpreter over it: the child
 * inherits it whole, so the only work inside the bound is the walk. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

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

/* the request that spans the segment boundary */
#define SPAN 5

/* What the child made of the stack, and all the parent reads of it. */
typedef struct
{
    int got_one;
    int one_type;
    int one_val;
    int got_span;
    int span_type[SPAN];
    int span_val[SPAN];
    int got_all;
    int all_top_val;
    int all_bottom_val;
} Answer;

/* The child's whole job: walk the stack, say what it found, and stop.
   The budget is set here rather than by the parent because it bounds
   this process's own work, and it is set before the walk so that the
   walk is what spends it. */
static void run_bounded_walk(Xpost_Memory_File *mem,
                             unsigned int stackadr,
                             int count,
                             int outfd)
{
    struct rlimit budget;
    Xpost_Object one;
    Xpost_Object span[SPAN];
    Xpost_Object *all;
    Answer a;
    int i;

    budget.rlim_cur = 2;
    budget.rlim_max = 4;
    if (setrlimit(RLIMIT_CPU, &budget) != 0)
        _exit(3); /* the work cannot be bounded here; the parent says so */

    all = malloc((size_t)count * sizeof(Xpost_Object));
    if (!all)
        _exit(4);

    memset(&a, 0, sizeof a);
    memset(&one, 0, sizeof one);
    memset(span, 0, sizeof span);

    a.got_one = xpost_stack_peek_top(mem, stackadr, 1, &one);
    a.one_type = (int)xpost_object_get_type(one);
    a.one_val = one.int_.val;

    a.got_span = xpost_stack_peek_top(mem, stackadr, SPAN, span);
    for (i = 0; i < SPAN; i++)
    {
        a.span_type[i] = (int)xpost_object_get_type(span[i]);
        a.span_val[i] = span[i].int_.val;
    }

    a.got_all = xpost_stack_peek_top(mem, stackadr, count, all);
    a.all_top_val = all[0].int_.val;
    a.all_bottom_val = all[count - 1].int_.val;

    if (write(outfd, &a, sizeof a) != (ssize_t)sizeof a)
        _exit(5);
    _exit(0);
}

int main(void)
{
    Xpost_Memory_File mem;
    unsigned int stackadr = 0;
    const int seg = XPOST_STACK_SEGMENT_SIZE;
    Answer a;
    int pipefd[2];
    pid_t child;
    int status = 0;
    size_t have = 0;
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
    if (!xpost_stack_init(&mem, &stackadr))
    {
        report_failure("xpost_stack_init");
        return verdict();
    }

    /* a full first segment and a partial second, so that a request of
       SPAN spans the boundary between them */
    for (i = 0; i < seg + 3; i++)
    {
        if (!xpost_stack_push(&mem, stackadr, xpost_int_cons(ELEMENT_AT(i))))
        {
            report_failure("push %d refused", i);
            return verdict();
        }
    }
    count = xpost_stack_count(&mem, stackadr);
    check(count == seg + 3, "the stack counts every element pushed");
    check((int)xpost_stack_at(&mem, stackadr)->top == seg,
          "the elements past the first segment are in another one");

    if (pipe(pipefd) != 0)
    {
        report_failure("could not make a pipe for the bounded run");
        return verdict();
    }
    child = fork();
    if (child == -1)
    {
        report_failure("could not start the bounded run");
        return verdict();
    }
    if (child == 0)
    {
        close(pipefd[0]);
        run_bounded_walk(&mem, stackadr, count, pipefd[1]);
        _exit(6); /* not reached: run_bounded_walk exits */
    }

    close(pipefd[1]);
    memset(&a, 0, sizeof a);
    for (;;)
    {
        ssize_t n = read(pipefd[0], (unsigned char *)&a + have,
                         sizeof a - have);
        if (n <= 0)
            break;
        have += (size_t)n;
        if (have == sizeof a)
            break;
    }
    close(pipefd[0]);
    if (waitpid(child, &status, 0) != child)
    {
        report_failure("could not collect the bounded run");
        return verdict();
    }

    /* The child ended of its own accord, which is the whole of what a
       converging walk has to do that a non-converging one cannot. */
    if (WIFSIGNALED(status))
    {
        report_failure("the bounded walk was ended by signal %d "
                       "rather than converging", WTERMSIG(status));
    }
    else if (!WIFEXITED(status))
    {
        report_failure("the bounded walk neither exited nor was signalled");
    }
    else if (WEXITSTATUS(status) == 3)
    {
        report_failure("the work of the walk could not be bounded, "
                       "so nothing here was tested");
    }
    else if (WEXITSTATUS(status) != 0)
    {
        report_failure("the bounded walk exited %d", WEXITSTATUS(status));
    }

    if (have != sizeof a)
    {
        report_failure("the bounded walk reported %lu of %lu bytes",
                       (unsigned long)have, (unsigned long)sizeof a);
        xpost_memory_file_exit(&mem);
        xpost_quit();
        return verdict();
    }

    /* and it converged on the elements that are there */
    check(a.got_one == 1, "a request for one element takes one");
    check(a.one_type == integertype && a.one_val == ELEMENT_AT(count - 1),
          "and that one is the top of the stack");

    check(a.got_span == SPAN, "a request spanning the segment boundary "
                              "takes what it asked for");
    for (i = 0; i < SPAN; i++)
    {
        if (a.span_type[i] != integertype ||
            a.span_val[i] != ELEMENT_AT(count - 1 - i))
        {
            report_failure("the spanning request put %d at depth %d, "
                           "where the stack holds %d",
                           a.span_val[i], i, ELEMENT_AT(count - 1 - i));
            break;
        }
    }

    check(a.got_all == count, "a request for the whole stack takes all of it");
    check(a.all_top_val == ELEMENT_AT(count - 1),
          "whose first element is the top of the stack");
    check(a.all_bottom_val == ELEMENT_AT(0),
          "and whose last is the bottom");

    xpost_memory_file_exit(&mem);
    xpost_quit();

    return verdict();
}
