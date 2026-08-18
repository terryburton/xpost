/* What a matrix operator does with a backup the memory file refused.
 *
 * The operators that fill a caller's matrix -- identmatrix,
 * defaultmatrix, currentmatrix, invertmatrix, concatmatrix, and
 * translate, scale and rotate given a matrix -- all store the six
 * numbers through one bulk write rather than element by element, so the
 * copy-on-write that xpost_array_put performs per element is performed
 * once, by hand, before the write. That backup is an allocation, and an
 * allocation can be declined.
 *
 * A write that goes in over a declined backup is a write the save it is
 * inside has no record of, so the matching restore does not revert it: a
 * matrix carries out of the save contents it was given inside it, and the
 * operator that put them there answered as though it had done nothing
 * unusual. PLRM 8.2 gives VMerror for an error in the virtual memory
 * machinery, which a backup with nowhere to go is, and that is what the
 * operator answers here.
 *
 * The matrix is one in global VM, with a save level taken over global VM,
 * so that declining allocation in that file reaches the backup and
 * nothing else the interpreter needs to keep running. The operator is
 * executed directly rather than through a program for the same reason:
 * what is under test is the store, not the interpreter's ability to make
 * progress while a memory file is full.
 *
 * Both the refusal and a sound store are asked for. A store that always
 * answered VMerror would satisfy the first alone; the second is what says
 * the operator still writes the matrix, and that the restore still puts
 * the previous contents back.
 *
 * The refusal is induced by putting the memory file at the far end of the
 * address range its offsets are unsigned ints of, which is where its
 * growth is declined. Nothing is written on that path, so the two fields
 * are the whole of the change and putting them back is the whole of
 * undoing it.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_save.h"
#include "xpost_array.h"
#include "xpost_operator.h"
#include "xpost_error.h"

#include "xpost_test.h"

static unsigned int held_used;
static unsigned int held_max;

/* Decline every allocation in mem until released. */
static void refuse_allocation(Xpost_Memory_File *mem)
{
    held_used = mem->high_water;
    held_max = mem->max;
    mem->high_water = 0xfffffff8u;
    mem->max = 0xfffffff8u;
}

static void allow_allocation(Xpost_Memory_File *mem)
{
    mem->high_water = held_used;
    mem->max = held_max;
}

/* A six-element array in global VM holding numbers no matrix operator
   produces, so that a store is told apart from a revert. */
static Xpost_Object born_marked(Xpost_Context *ctx)
{
    Xpost_Object m;
    unsigned int vmmode;
    int i;

    vmmode = ctx->vmmode;
    ctx->vmmode = GLOBAL;
    m = xpost_object_cvlit(xpost_array_cons(ctx, 6));
    ctx->vmmode = vmmode;
    if (xpost_object_get_type(m) != arraytype)
        return null;
    for (i = 0; i < 6; i++)
        if (xpost_array_put(ctx, m, i, xpost_real_cons((real)(10 + i))) != 0)
            return null;
    return m;
}

/* true iff m still reads as born_marked left it */
static int still_marked(Xpost_Context *ctx, Xpost_Object m)
{
    int i;

    for (i = 0; i < 6; i++)
    {
        Xpost_Object e = xpost_array_get(ctx, m, i);

        if (xpost_object_get_type(e) != realtype || e.real_.val != (real)(10 + i))
            return 0;
    }
    return 1;
}

static void matrix_store(int refused)
{
    Xpost_Context *ctx;
    Xpost_Object m;
    Xpost_Object op;
    int ret;

    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        return;
    }

    m = born_marked(ctx);
    if (xpost_object_get_type(m) != arraytype)
    {
        report_failure("could not make the matrix");
        xpost_destroy(ctx);
        return;
    }

    /* the save the store must be revertible within */
    if (xpost_object_get_type(xpost_save_create_snapshot_object(ctx->gl))
        != savetype)
    {
        report_failure("could not take the save level");
        xpost_destroy(ctx);
        return;
    }

    op = xpost_operator_cons(ctx, "identmatrix", NULL, 0);
    if (xpost_object_get_type(op) != operatortype)
    {
        report_failure("identmatrix is not an operator");
        xpost_destroy(ctx);
        return;
    }

    if (refused)
        refuse_allocation(ctx->gl);
    xpost_stack_push(ctx->lo, ctx->os, m);
    ret = xpost_operator_exec(ctx, op.mark_.padw);
    if (refused)
        allow_allocation(ctx->gl);

    if (refused)
    {
        if (ret != VMerror)
            report_failure("a matrix stored over a backup that could not be "
                           "made was answered %s", ret == 0 ? "noerror"
                           : errorname[ret]);
        if (!still_marked(ctx, m))
            report_failure("a matrix was written although its backup "
                           "could not be made");
    }
    else
    {
        if (ret != 0)
            report_failure("a sound matrix store was answered %s",
                           errorname[ret]);
        if (still_marked(ctx, m))
            report_failure("a sound matrix store did not write the matrix");
    }

    xpost_save_restore_snapshot(ctx->gl);
    if (!still_marked(ctx, m))
        report_failure("restore did not put back the matrix the save was "
                       "entered with%s", refused ? " (backup refused)" : "");

    xpost_destroy(ctx);
}

int main(void)
{
    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    matrix_store(0);
    matrix_store(1);

    xpost_quit();

    return verdict();
}
