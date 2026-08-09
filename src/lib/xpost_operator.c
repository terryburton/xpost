/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * Copyright (C) 2013, Thorsten Behrens
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Xpost software product nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> /* NULL */
#include <string.h> /* memcpy */
#include <stdint.h> /* uintptr_t */

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"  // accesses mfile
#include "xpost_object.h"  // operators are objects
#include "xpost_stack.h"  // uses a stack for argument passing
#include "xpost_free.h"  // grow signatures using xpost_free_realloc
#include "xpost_context.h"
#include "xpost_error.h"  // operator functions may throw errors
#include "xpost_string.h"  // uses string function to dump operator name
#include "xpost_name.h"  // operator objects have associated names
#include "xpost_dict.h"  // install operators in systemdict, a dict
#include "xpost_array.h"  // a wrapped call's operands are saved in an array

//#include "xpost_interpreter.h"  // works with context struct
#include "xpost_operator.h"  // double-check prototypes


/* convert an integertype object to a realtype object */
static
Xpost_Object _promote_integer_to_real(Xpost_Object o)
{
    return xpost_real_cons((real)o.int_.val);
}

/* Record that operand at top-down index idx was coerced from the integer orig,
   so an error can restore the original the program pushed (PLRM 3.11). Called
   only when a coercion actually happens, so it is a no-op for the usual case. */
static void _op_restore_note(Xpost_Context *ctx, int idx, Xpost_Object orig)
{
    if (ctx->op_restore_n < (int)(sizeof ctx->op_restore_idx))
    {
        ctx->op_restore_idx[ctx->op_restore_n] = (unsigned char)idx;
        ctx->op_restore_val[ctx->op_restore_n] = orig;
        ctx->op_restore_n++;
    }
}

/* copied from the header file for reference:
   typedef struct Xpost_Signature {
   int (*fp)(Xpost_Context *ctx);
   int in;
   unsigned t;
   int (*checkstack)(Xpost_Context *ctx);
   } Xpost_Signature;

   typedef struct Xpost_Operator {
   unsigned name;
   int n; // number of sigs
   unsigned sigadr;
   } Xpost_Operator;

   enum typepat ( anytype = stringtype + 1,
   floattype, numbertype, proctype };

   #define MAXOPS 20
*/

/* the number of ops in the optab of the context being served; reset with
   the table itself, which each context allocates in its own global VM */
static
int _xpost_noops = 0;

static
int _stack_none(Xpost_Context *ctx)
{
    (void)ctx;
    return 0;
}

static
int _stack_int(Xpost_Context *ctx)
{
    Xpost_Object s0;
    s0 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    switch(xpost_object_get_type(s0))
    {
        case invalidtype:
            return stackunderflow;
        case integertype:
            return 0;
        default:
            return typecheck;
    }
}

static
int _stack_real(Xpost_Context *ctx)
{
    Xpost_Object s0;
    s0 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    switch(xpost_object_get_type(s0))
    {
        case invalidtype:
            return stackunderflow;
        case realtype:
            return 0;
        default:
            return typecheck;
    }
}

static
int _stack_float(Xpost_Context *ctx)
{
    Xpost_Object s0;
    s0 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    switch(xpost_object_get_type(s0))
    {
        case invalidtype:
            return stackunderflow;
        case integertype:
            _op_restore_note(ctx, 0, s0);
            /* the fetch just above reached this index, and nothing has
               touched the stack since, so the store reaches it too */
            XPOST_REFUSAL_IMPOSSIBLE(
                xpost_stack_topdown_replace(ctx->lo, ctx->os, 0,
                                            s0 = _promote_integer_to_real(s0)));
            /* fallthrough */
        case realtype:
            return 0;
        default:
            return typecheck;
    }
}

static
int _stack_any(Xpost_Context *ctx)
{
    Xpost_Stack *os_root = xpost_stack_at(ctx->lo, ctx->os);
    Xpost_Stack *os_top = xpost_stack_at(ctx->lo, os_root->prevseg);
    /* at least one operand without walking the whole stack: the top
       segment holds one, or a full segment sits below it (only the top
       segment is ever partial) -- counting is O(n) in the stack depth */
    if (os_top->top >= 1 || os_top != os_root)
        return 0;
    return stackunderflow;
}

static
int _stack_bool_bool(Xpost_Context *ctx)
{
    Xpost_Object s0, s1;
    s0 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    switch(xpost_object_get_type(s0))
    {
        case invalidtype:
            return stackunderflow;
        case booleantype:
            s1 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 1);
            switch(xpost_object_get_type(s1))
            {
                case invalidtype:
                    return stackunderflow;
                case booleantype:
                    return 0;
                default:
                    return typecheck;
            }
        default:
            return typecheck;
    }
}

static
int _stack_int_int(Xpost_Context *ctx)
{
    Xpost_Object s0, s1;
    s0 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    switch(xpost_object_get_type(s0))
    {
        case invalidtype:
            return stackunderflow;
        case integertype:
            s1 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 1);
            switch(xpost_object_get_type(s1))
            {
                case invalidtype:
                    return stackunderflow;
                case integertype:
                    return 0;
                default:
                    return typecheck;
            }
        default:
            return typecheck;
    }
}

static
int _stack_float_float(Xpost_Context *ctx)
{
    Xpost_Object s0, s1;
    s0 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    switch(xpost_object_get_type(s0))
    {
        case invalidtype:
            return stackunderflow;
        case integertype:
            _op_restore_note(ctx, 0, s0);
            /* the fetch just above reached this index, and nothing has
               touched the stack since, so the store reaches it too */
            XPOST_REFUSAL_IMPOSSIBLE(
                xpost_stack_topdown_replace(ctx->lo, ctx->os, 0,
                                            s0 = _promote_integer_to_real(s0)));
            /* fallthrough */
        case realtype:
            s1 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 1);
            switch(xpost_object_get_type(s1))
            {
                case invalidtype:
                    return stackunderflow;
                case integertype:
                    _op_restore_note(ctx, 1, s1);
                    /* as above: the index was just fetched from */
                    XPOST_REFUSAL_IMPOSSIBLE(
                        xpost_stack_topdown_replace(ctx->lo, ctx->os, 1,
                                                    s1 = _promote_integer_to_real(s1)));
                    /* fallthrough */
                case realtype:
                    return 0;
                default:
                    return typecheck;
            }
        default:
            return typecheck;
    }
}

static
int _stack_number_number(Xpost_Context *ctx)
{
    Xpost_Object s0, s1;
    s0 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 0);
    switch(xpost_object_get_type(s0))
    {
        case invalidtype:
            return stackunderflow;
        case integertype: /* fallthrough */
        case realtype:
            s1 = xpost_stack_topdown_fetch(ctx->lo, ctx->os, 1);
            switch(xpost_object_get_type(s1))
            {
                case invalidtype:
                    return stackunderflow;
                case integertype: /* fallthrough */
                case realtype:
                    return 0;
                default:
                    return typecheck;
            }
        default:
            return typecheck;
    }
}

static
int _stack_any_any(Xpost_Context *ctx)
{
    Xpost_Stack *os_root = xpost_stack_at(ctx->lo, ctx->os);
    Xpost_Stack *os_top = xpost_stack_at(ctx->lo, os_root->prevseg);
    /* at least two operands in O(1): two in the top segment, or a full
       segment (never partial) below it */
    if (os_top->top >= 2 || os_top != os_root)
        return 0;
    return stackunderflow;
}

typedef struct {
    int (*checkstack)(Xpost_Context *ctx);
    int n;
    int t[8];
} Xpost_Check_Stack;

static
Xpost_Check_Stack _check_stack_funcs[] = {
    { _stack_none, 0, { 0, 0, 0, 0, 0, 0, 0, 0} },
    { _stack_int, 1, { integertype } },
    { _stack_real, 1, { realtype } },
    { _stack_float, 1, { floattype } },
    { _stack_any, 1, { anytype } },
    { _stack_bool_bool, 2, { booleantype, booleantype } },
    { _stack_int_int, 2, { integertype, integertype } },
    { _stack_float_float, 2, { floattype, floattype } },
    { _stack_number_number, 2, { numbertype, numbertype } },
    { _stack_any_any, 2, { anytype, anytype } }
};


/* allocate the OPTAB structure in VM */
int xpost_operator_init_optab(Xpost_Context *ctx)
{
    unsigned ent;
    Xpost_Memory_Table *tab;
    int ret;

    ret = xpost_memory_table_alloc(ctx->gl, MAXOPS * sizeof(Xpost_Operator), 0, &ent);
    if (!ret)
    {
        return 0;
    }
    tab = &ctx->gl->table;
    assert(ent == XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE);
    tab->tab[ent].sz = 0; // so gc will ignore it
    _xpost_noops = 0;
    //printf("ent: %d\nOPTAB: %d\n", ent, (int)XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE);

    return 1;
}

/* print a dump of the operator struct given opcode */
void xpost_operator_dump(Xpost_Context *ctx,
                         int opcode)
{
    Xpost_Operator *optab;
    Xpost_Operator op;
    Xpost_Object o;
    Xpost_Object str;
    char *s;
    Xpost_Signature *sig;
    uintptr_t fp;

    optab = xpost_operator_table(ctx->gl);
    op = optab[opcode];
    o.mark_.tag = nametype | XPOST_OBJECT_TAG_DATA_FLAG_BANK;
    o.mark_.pad0 = 0;
    o.mark_.padw = op.name;
    str = xpost_name_get_string(ctx, o);
    s = xpost_string_get_pointer(ctx, str);
    sig = xpost_vm_ptr(ctx->gl, op.sigadr);
    memcpy(&fp, &sig[0].fp, sizeof fp);
    /*
    printf("<operator %d %d:%*s %p>",
           opcode,
           str.comp_.sz, str.comp_.sz, s,
           (void *)fp );
    */
    XPOST_LOG_DUMP("%*s ", str.comp_.sz, s);
}

/* create operator object by opcode number */
Xpost_Object xpost_operator_cons_opcode(int opcode)
{
    Xpost_Object op;
    op.mark_.tag = operatortype;
    op.mark_.pad0 = 0;
    op.mark_.padw = opcode;
    if (opcode >= _xpost_noops)
    {
        XPOST_LOG_ERR("opcode does not index a valid operator");
        return null;
    }
    return op;
}

/* construct an operator object by name
   If function-pointer fp is not NULL, attempts to install a new operator
   in OPTAB, otherwise just perform a lookup.
   If installing a new operator, in specifies the number of input
   values whose presence and types should be checked.
   There should follow 'in' number of typenames passed after 'in'.
*/
Xpost_Object xpost_operator_cons(Xpost_Context *ctx,
                                 const char *name,
                                 /*@null@*/ Xpost_Op_Func fp,
                                 int in, ...)
{
    Xpost_Object nm;
    Xpost_Object o;
    int opcode;
    int i;
    unsigned si;
    unsigned t;
    unsigned vmmode;
    Xpost_Signature *sp;
    Xpost_Operator *optab;
    Xpost_Operator  op;

    //fprintf(stderr, "name: %s\n", name);
    assert(ctx->gl->base);

    optab = xpost_operator_table(ctx->gl);

    if (!(in < XPOST_STACK_SEGMENT_SIZE))
    {
        printf("!(in < XPOST_STACK_SEGMENT_SIZE) in xpost_operator_cons(%s, %d)\n", name, in);
        fprintf(stderr, "!(in < XPOST_STACK_SEGMENT_SIZE) in xpost_operator_cons(%s, %d)\n", name, in);
        exit(EXIT_FAILURE);
    }
    //assert(in < XPOST_STACK_SEGMENT_SIZE); // or else xpost_operator_exec can't call it using HOLD

    vmmode=ctx->vmmode;
    ctx->vmmode = GLOBAL;
    /* the optab records names by their global index: a locally
       interned name with the same numeric index would alias a
       different operator entirely */
    nm = xpost_name_cons_global(ctx, name);
    if (xpost_object_get_type(nm) == invalidtype)
        return invalid;
    ctx->vmmode = vmmode;

    optab = xpost_operator_table(ctx->gl);
    for (opcode = 0; optab[opcode].name != nm.mark_.padw; opcode++)
    {
        if (opcode == _xpost_noops) break;
    }

    /* install a new signature (prototype) */
    if (fp)
    {
        if (opcode == _xpost_noops)
        { /* a new operator */
            unsigned adr;
            if (_xpost_noops == MAXOPS-1)
            {
                XPOST_LOG_ERR("optab too small in xpost_operator.h");
                XPOST_LOG_ERR("operator %s NOT installed", name);
                return null;
            }
            if (!xpost_memory_file_alloc(ctx->gl, sizeof(Xpost_Signature), &adr))
            {
                XPOST_LOG_ERR("cannot allocate signature block");
                XPOST_LOG_ERR("operator %s NOT installed", name);
                return null;
            }
            optab = xpost_operator_table(ctx->gl); // recalc
            op.name = nm.mark_.padw;
            op.n = 1;
            op.sigadr = adr;
            op.proc = null;
            optab[opcode] = op;
            ++_xpost_noops;
            si = 0;
        }
        else
        { /* increase sig table by 1 */
            t = xpost_free_realloc(ctx->gl,
                                   optab[opcode].sigadr,
                                   optab[opcode].n * sizeof(Xpost_Signature),
                                   (optab[opcode].n + 1) * sizeof(Xpost_Signature));
            if (!t)
            {
                XPOST_LOG_ERR("cannot allocate new sig table");
                XPOST_LOG_ERR("operator %s NOT installed", name);
                return null;
            }
            optab = xpost_operator_table(ctx->gl); // recalc
            optab[opcode].sigadr = t;

            si = optab[opcode].n++; /* index of last sig */
        }

        sp = xpost_vm_ptr(ctx->gl, optab[opcode].sigadr);
        {
            unsigned int ad;
            if (!xpost_memory_file_alloc(ctx->gl, in, &ad))
            {
                XPOST_LOG_ERR("cannot allocate type block");
                XPOST_LOG_ERR("operator %s NOT installed", name);
                return null;
            }
            optab = xpost_operator_table(ctx->gl); // recalc
            sp = xpost_vm_ptr(ctx->gl, optab[opcode].sigadr); // recalc
            sp[si].t = ad;
        }
        {
            va_list args;
            byte *b = xpost_vm_ptr(ctx->gl, sp[si].t);
            va_start(args, in);
            for (i = in-1; i >= 0; i--) {
                b[i] = va_arg(args, int);
            }
            va_end(args);
            sp[si].in = in;
            sp[si].fp = (int(*)(Xpost_Context *))fp;
            sp[si].checkstack = NULL;
            {
                int j;
                int k;
                int pass;
                for (j = 0; j < (int)(sizeof _check_stack_funcs/sizeof*_check_stack_funcs); j++)
                {
                    if (_check_stack_funcs[j].n == sp[si].in)
                    {
                        pass = 1;
                        for (k=0; k < _check_stack_funcs[j].n; k++)
                        {
                            if (b[k] != _check_stack_funcs[j].t[k])
                            {
                                pass = 0;
                                break;
                            }
                        }
                        if (pass)
                        {
                            sp[si].checkstack = _check_stack_funcs[j].checkstack;
                            break;
                        }
                    }
                }
            }
            //sp[si].checkstack = NULL;
        }
    }
    else if (opcode == _xpost_noops)
    {
        XPOST_LOG_ERR("operator not found");
        return null;
    }

    /* Capture the opcode of any operator the interpreter itself reaches
       for. Doing it here, keyed by the name being registered, is what
       makes the reference table impossible to get wrong: a registration
       cannot forget its capture, and a capture cannot end up holding the
       operator registered on the line above. The cost is one pass of
       first-character comparisons per registration, at startup only. */
#define XPOST_OP_REF_CAPTURE(ref, refname) \
    if (name[0] == (refname)[0] && !strcmp(name, refname)) \
        XPOST_OP_CODE(ctx, ref) = opcode;
    XPOST_OP_REFS(XPOST_OP_REF_CAPTURE)
#undef XPOST_OP_REF_CAPTURE

    o.tag = operatortype;
    o.mark_.padw = opcode;
    return o;
}

Xpost_Object xpost_operator_cons_wrapped(Xpost_Context *ctx,
                                         Xpost_Object name,
                                         Xpost_Object proc,
                                         int nsig,
                                         const Xpost_Wrapped_Signature *sigs)
{
    Xpost_Operator *optab;
    Xpost_Operator op;
    Xpost_Object nm;
    Xpost_Object str;
    char buf[128];
    unsigned int len;
    unsigned vmmode;
    int opcode;

    if (xpost_object_get_type(proc) != arraytype)
        return null;

    str = xpost_name_get_string(ctx, name);
    if (xpost_object_get_type(str) != stringtype)
        return null;
    len = str.comp_.sz;
    if (len > sizeof buf - 1)
        len = sizeof buf - 1;
    memcpy(buf, xpost_string_get_pointer(ctx, str), len);
    buf[len] = '\0';

    if (_xpost_noops == MAXOPS-1)
    {
        XPOST_LOG_ERR("optab too small in xpost_operator.h");
        XPOST_LOG_ERR("operator %s NOT installed", buf);
        return null;
    }

    /* the optab records names by their global index: a locally
       interned name with the same numeric index would alias a
       different operator entirely */
    vmmode = ctx->vmmode;
    ctx->vmmode = GLOBAL;
    nm = xpost_name_cons_global(ctx, buf);
    ctx->vmmode = vmmode;
    if (xpost_object_get_type(nm) == invalidtype)
        return invalid;

    /* always a fresh entry: the name may already denote a C operator,
       which lookups by name must keep finding */
    opcode = _xpost_noops;
    optab = xpost_operator_table(ctx->gl);
    op.name = nm.mark_.padw;
    op.n = 0;
    op.sigadr = 0;
    op.proc = proc;
    optab[opcode] = op;
    ++_xpost_noops;

    if (nsig > 0)
    {
        /* the operator states the operands it takes, and the dispatcher
           enforces the statement exactly as it does for one written in
           C: signatures with no function of their own, whose procedure
           runs once one of them has matched. An operator taking more
           than one operand shape states each, and the dispatcher tries
           them in turn, as it does for a C operator installed under
           several prototypes. */
        unsigned int sigadr;
        int s;

        if (!xpost_memory_file_alloc(ctx->gl,
                                     nsig * sizeof(Xpost_Signature), &sigadr))
        {
            XPOST_LOG_ERR("cannot allocate signature block for %s", buf);
            return null;
        }
        for (s = 0; s < nsig; s++)
        {
            unsigned int tadr;
            Xpost_Signature *sig;
            byte *b;
            int in = sigs[s].in;
            int k;

            if (!xpost_memory_file_alloc(ctx->gl,
                                         (unsigned int)(in ? in : 1), &tadr))
            {
                XPOST_LOG_ERR("cannot allocate type block for %s", buf);
                return null;
            }
            /* an allocation moves the file, so every pointer into it is
               taken afresh from its address */
            sig = xpost_vm_ptr(ctx->gl, sigadr);
            b = xpost_vm_ptr(ctx->gl, tadr);
            for (k = 0; k < in; k++)
                b[k] = sigs[s].types[k];
            sig[s].in = in;
            sig[s].t = tadr;
            sig[s].fp = NULL;
            sig[s].checkstack = NULL;
        }
        /* the count goes in last: an allocation that failed part way
           leaves the operator stating nothing rather than stating a
           signature that was never filled in */
        optab = xpost_operator_table(ctx->gl);
        optab[opcode].sigadr = sigadr;
        optab[opcode].n = nsig;
    }

    return xpost_operator_cons_opcode(opcode);
}

/* clear hold and pop n objects from opstack to hold stack.
   The hold stack is used as temporary storage to hold the
   arguments for an operator-function call.
   If the operator-function does not itself call xpost_operator_exec,
   the arguments may be restored by xpost_interpreter.c:_on_error().
   xpost_operator_exec checks its argument with ctx->currentobject
   and sets a flag indicating consistency which is then checked by
   on_error()
   Composite Object constructors also add their objects to the
   hold stack, in defense against garbage collection occurring
   from a subsequent allocation before the object is returned
   to the stack.
   on_error() also uses the number of args from ctx->currentobject.mark_.pad0
   instead of the stack count so these extra gc-defense stack objects
   will not be erroneously returned to postscript in response to an
   operator error.
*/
static
void _xpost_operator_push_args_to_hold(Xpost_Context *ctx,
                                       Xpost_Memory_File *mem,
                                       unsigned stacadr,
                                       int n)
{
    int j;
    Xpost_Stack *s;
    Xpost_Stack *hold;

    int k;

    /* the hold stack is cracked as a single segment below: an
       operator's declared arity fits one segment */
    assert(n < XPOST_STACK_SEGMENT_SIZE);

    /* when all args sit in the stack's top segment, copy them into the
       hold segment directly, sparing a segment walk per fetch/push/pop */
    s = xpost_stack_at(mem, stacadr);
    s = xpost_stack_at(mem, s->prevseg); /* load top segment */
    hold = xpost_stack_at(ctx->lo, ctx->hold);
    if ((int)s->top >= n)
    {
        hold->prevseg = ctx->hold;
        s->top -= n;
        for (k = 0; k < n; k++)
            hold->data[k] = s->data[s->top + k];
        hold->top = n;
        return;
    }

    xpost_stack_clear(ctx->lo, ctx->hold);

    for (j = n; j--;)
    {  /* copy */
        xpost_stack_push(ctx->lo, ctx->hold,
                         xpost_stack_topdown_fetch(mem, stacadr, j));
    }
    for (j = n; j--;)
    {  /* pop */
        (void)xpost_stack_pop(mem, stacadr);
    }
}

/* An operator's operands are its caller's to keep: an error leaving the
   operator puts them back (PLRM 3.11.1 step 1). For an operator written
   in C the dispatcher below already holds them -- it took them off the
   operand stack into the hold stack to pass them as arguments. An
   operator written in PostScript is passed nothing: its body reads the
   operand stack itself, and what it consumes there is gone. So the call
   copies them first.

   The copies go into one array per context, kept in privatedict, where
   the collector roots it and a program cannot name it: a composite
   operand stays reachable for as long as it is saved. Slot zero counts
   the slots in use; each live call owns a run above that, and the run is
   named by an array object in the call's frame -- its offset and size
   are the run, so the frame grows by one slot rather than by a base and
   a count. Releasing a run clears it, so nothing stays reachable through
   the array once the call it belongs to is over.

   A call saves the widest operand list an operator can state, and no
   more. That is a bound on the operands rather than a count of them: a
   statement may name fewer operands than its body takes, so the arity
   stated is not the measure. A body that consumes more than the bound
   keeps whatever the truncation leaves it, as every body did before
   there were any copies at all. */
#define XPOST_WRAPPED_SAVE_MAX XPOST_OPERATOR_MAX_SIG

/* The array is a fixed size rather than a growing one, because growth
   would move the runs live calls have already been handed. Nesting
   deeper than it holds saves nothing, which is a weaker guarantee for
   those calls and not a wrong one.

   It is small because it is allocated once and never dies, and local VM
   grows in steps: an array of this size is lost in the noise, while one
   eight times it moved the point at which a long job's VM grew and cost
   a graphics-heavy page a couple of megabytes of peak. The room it does
   hold is a hundred or so nested calls at the operand depths calls are
   really made at, and forty at the widest a call can save. */
#define XPOST_WRAPPED_SAVE_SLOTS 512

static Xpost_Object namewrapsave; /* cached xpost_name_cons(ctx, ".wrapsave") */

/* the context's saved-operand array, made on first use */
static
Xpost_Object _wrapped_save_array(Xpost_Context *ctx)
{
    Xpost_Object arr;

    if (xpost_object_get_type(ctx->privatedict) != dicttype)
        return null;
    if (xpost_object_get_type(namewrapsave) != nametype)
    {
        namewrapsave = xpost_name_cons(ctx, ".wrapsave");
        if (xpost_object_get_type(namewrapsave) != nametype)
            return null;
    }
    arr = xpost_dict_get(ctx, ctx->privatedict, namewrapsave);
    /* The array must be local. It is dereferenced against local memory
       below and its declared size is the bound the copy is held to, so
       both hold only for a local array: a copy read out of a local
       array is read out of the array it was written for, and bounded by
       that array's own size. privatedict is reachable and a program
       stores under any key there, so what stands here is the program's
       to replace; a value that is not a local array -- a global array
       among them, whose entity number names a different object of a
       different size in the local table -- is treated as none and
       rebuilt, which is the same rebuild an absent one gets.

       That it must be local is also what it is for: what it holds are
       the operands of calls being made, which may be local objects, and
       a global array may hold none of those. */
    if (xpost_object_get_type(arr) == arraytype &&
        xpost_context_select_memory(ctx, arr) == ctx->lo)
        return arr;
    arr = xpost_array_cons_memory(ctx->lo, XPOST_WRAPPED_SAVE_SLOTS);
    if (xpost_object_get_type(arr) != arraytype)
        return null;
    if (xpost_array_put_memory(ctx->lo, arr, 0, xpost_int_cons(1)) != 0)
        return null;
    if (xpost_dict_put(ctx, ctx->privatedict, namewrapsave, arr) != 0)
        return null;
    return arr;
}

/* Copy the operands a wrapped call is about to run on, and answer the
   run holding them: a null object where there is nothing to save or
   nowhere to put it, which an unwind reads as no copies taken. */
static
Xpost_Object _wrapped_save_operands(Xpost_Context *ctx)
{
    Xpost_Object arr;
    Xpost_Object *data;
    Xpost_Stack *s;
    int d, n, top;

    d = xpost_stack_count(ctx->lo, ctx->os);
    if (d <= 0)
        return null;
    n = (d < XPOST_WRAPPED_SAVE_MAX) ? d : XPOST_WRAPPED_SAVE_MAX;
    arr = _wrapped_save_array(ctx);
    if (xpost_object_get_type(arr) != arraytype)
        return null;
    data = xpost_ent_ptr_checked(ctx->lo, xpost_object_get_ent(arr));
    if (!data)
        return null;
    if (xpost_object_get_type(data[0]) != integertype)
        return null;
    top = (int)data[0].int_.val;
    if ((top < 1) || (top + n > (int)arr.comp_.sz))
        return null;
    /* written straight into the entity: the copies are the
       interpreter's own bookkeeping and not program-visible VM state,
       so they neither stash for restore nor copy the array on write */
    s = xpost_stack_at(ctx->lo, ctx->os);
    s = xpost_stack_at(ctx->lo, s->prevseg); /* load top segment */
    if ((int)s->top >= n)
        memcpy(data + top, s->data + s->top - n, (size_t)n * sizeof(*data));
    else
    {
        int j;

        for (j = 0; j < n; j++)
            data[top + j] = xpost_stack_topdown_fetch(ctx->lo, ctx->os,
                                                      n - 1 - j);
    }
    data[0] = xpost_int_cons(top + n);
    arr.comp_.off = (word)top;
    arr.comp_.sz = (word)n;
    return arr;
}

void xpost_operator_wrapped_release(Xpost_Context *ctx, Xpost_Object run)
{
    Xpost_Object *data;
    unsigned int slots;
    int base, top, i;

    if (xpost_object_get_type(run) != arraytype)
        return;
    /* The mark this clears back to is element zero of the array the
       copies live in, which is an ordinary array in a dictionary and
       so holds whatever a program last put there. It is bounded by
       what the array behind this run actually holds, not by the size
       the interpreter's own array is built at: a shorter one would
       otherwise be cleared past its end, over whatever the memory
       file holds next. */
    if (!xpost_memory_table_get_size(ctx->lo, xpost_object_get_ent(run),
                                     &slots))
        return;
    slots /= (unsigned int)sizeof(Xpost_Object);
    if (slots < 1)
        return;
    data = xpost_ent_ptr_checked(ctx->lo, xpost_object_get_ent(run));
    if (!data)
        return;
    if (xpost_object_get_type(data[0]) != integertype)
        return;
    base = (int)run.comp_.off;
    top = (int)data[0].int_.val;
    if ((base < 1) || (top > (int)slots))
        return;
    /* everything from this run up belongs to the calls this one
       enclosed: they are over too, whether or not each got to release
       its own */
    for (i = base; i < top; i++)
        data[i] = null;
    data[0] = xpost_int_cons(base);
}

/* execute an operator function by opcode
   the opcode is the payload of an operator object
*/
/* Schedule a wrapped operator's procedure. The call's frame rides the
   exec stack beneath it -- the operator, the operand and dict depths at
   the call, and the operands themselves -- under a finish marker that
   carries them off when the procedure completes. An unwind that
   discards the marker discards the record with it, and the error path
   reads the live records straight off the stack to name this operator,
   put the stack depths back and hand the operands back (see _onerror).
   The operands stay on the operand stack as well: the procedure takes
   them from there itself. */
static
int _exec_wrapped_proc(Xpost_Context *ctx, unsigned opcode, Xpost_Object proc)
{
    Xpost_Object fr[6];
    int k;

    fr[0] = xpost_int_cons((integer)opcode);
    fr[1] = xpost_int_cons(xpost_stack_count(ctx->lo, ctx->os));
    fr[2] = xpost_int_cons(xpost_stack_count(ctx->lo, ctx->ds));
    fr[3] = _wrapped_save_operands(ctx);
    fr[4] = XPOST_OP(ctx, wrapdone);
    fr[5] = xpost_object_cvx(proc);
    for (k = 0; k < 6; k++)
    {
        if (!xpost_stack_push(ctx->lo, ctx->es, fr[k]))
        {
            xpost_operator_wrapped_release(ctx, fr[3]);
            while (k--)
                (void)xpost_stack_pop(ctx->lo, ctx->es);
            return execstackoverflow;
        }
    }
    return 0;
}

int xpost_operator_exec(Xpost_Context *ctx,
                        unsigned opcode)
{
    Xpost_Operator *optab;
    Xpost_Operator op;
    Xpost_Signature *sp;
    int i,j;
    int pass;
    int err = unregistered;
    Xpost_Stack *hold;
    Xpost_Stack *os_root;
    Xpost_Stack *os_top;
    int ct;
    int ret;

    ctx->op_restore_n = 0;

    optab = xpost_operator_table(ctx->gl);
    op = optab[opcode];
    sp = xpost_vm_ptr(ctx->gl, op.sigadr);

    /* a signature states at most XPOST_OPERATOR_MAX_SIG operands, so ct
       only needs to reach that many; no full segment walk is needed. The
       top segment settles it: that many or more in it, or -- when a full
       segment (never partial) sits below it -- at least SEGMENT_SIZE,
       likewise enough. Only a lone segment can hold fewer, and then its
       own top is the count. */
    os_root = xpost_stack_at(ctx->lo, ctx->os);
    os_top = xpost_stack_at(ctx->lo, os_root->prevseg);
    ct = (os_top->top >= XPOST_OPERATOR_MAX_SIG) ? XPOST_OPERATOR_MAX_SIG
        : (os_top == os_root) ? (int)os_top->top
        : XPOST_OPERATOR_MAX_SIG;
    if (op.n == 0)
    {
        /* a wrapped operator carries no C signatures: it runs its
           recorded procedure, which checks its own operands. The
           call's frame rides the exec stack beneath the procedure --
           the operator, the operand and dict depths at the call, and
           the operands themselves -- under a finish marker that
           carries them off when the procedure completes. An unwind
           that discards the marker discards the record with it, and
           the error path reads the live records straight off the
           stack to name this operator, put the stack depths back and
           hand the operands back (see _onerror) */
        if (xpost_object_get_type(op.proc) == arraytype)
            return _exec_wrapped_proc(ctx, opcode, op.proc);
        XPOST_LOG_ERR("operator has no signatures");
        return unregistered;
    }
    for (i =0 ; i < op.n; i++)
    { /* try each signature */
        byte *t;

        /* call signature's stack-checking proc, if available */
        if (sp[i].checkstack)
        {
            if ((ret = sp[i].checkstack(ctx)))
            {
                err = ret;
                continue;
            }
            goto call;
        }

        /* check stack size */
        if (ct < sp[i].in)
        {
            pass = 0;
            /* a higher-arity signature that lacks operands must not mask a
               type mismatch already found against a signature whose arity
               was satisfied: a wrong-typed operand is a typecheck, not a
               stackunderflow */
            if (err != typecheck)
                err = stackunderflow;
            continue;
        }

        /* check type-pattern against stack */
        pass = 1;
        t = xpost_vm_ptr(ctx->gl, sp[i].t);
        for (j=0; j < sp[i].in; j++)
        {
            Xpost_Object el = (j < (int)os_top->top)
                ? os_top->data[os_top->top - 1 - j]
                : xpost_stack_topdown_fetch(ctx->lo, ctx->os, j);
            if (t[j] == anytype)
                continue;
            if (t[j] == xpost_object_get_type(el))
                continue;
            if ((t[j] == numbertype) &&
                (((xpost_object_get_type(el) == integertype) ||
                  (xpost_object_get_type(el) == realtype))))
                continue;
            if (t[j] == floattype)
            {
                if (xpost_object_get_type(el) == integertype)
                {
                    _op_restore_note(ctx, j, el);
                    el = _promote_integer_to_real(el);
                    if (j < (int)os_top->top)
                        os_top->data[os_top->top - 1 - j] = el;
                    else if (!xpost_stack_topdown_replace(ctx->lo, ctx->os, j, el))
                        return unregistered;
                    continue;
                }
                if (xpost_object_get_type(el) == realtype)
                    continue;
            }
            if ((t[j] == proctype) &&
                (xpost_object_get_type(el) == arraytype) &&
                xpost_object_is_exe(el))
                continue;
            pass = 0;
            err = typecheck;
            break;
        }

        if (pass) goto call;
    }
    /* no signature matched: a rejected trial may have coerced an operand from
       integer to real before failing. The operator never ran, so the operands
       are still on the stack; put back the integers the program pushed. */
    for (i = 0; i < ctx->op_restore_n; i++)
        /* each index was reached when the note was taken, and the trial
           that failed left the stack as it found it */
        XPOST_REFUSAL_IMPOSSIBLE(
            xpost_stack_topdown_replace(ctx->lo, ctx->os,
                                        ctx->op_restore_idx[i],
                                        ctx->op_restore_val[i]));
    return err;

  call:
    /* a signature whose procedure is written in PostScript: the types
       have matched, and the body takes the operands from the stack */
    if (!sp[i].fp && (xpost_object_get_type(op.proc) == arraytype))
        return _exec_wrapped_proc(ctx, opcode, op.proc);

    /* If we're executing the context's "currentobject",
       set the number of arguments consumed in the pad0 of currentobject,
       and set a flag declaring that this has been done.
       This is so onerror() can reset the stack
       (if hold has not been clobbered by another call to xpost_operator_exec).
    */
    if ((ctx->currentobject.tag == operatortype) &&
        (ctx->currentobject.mark_.padw == opcode))
    {
        ctx->currentobject.mark_.pad0 = sp[i].in;
        ctx->currentobject.tag |= XPOST_OBJECT_TAG_DATA_FLAG_OPARGSINHOLD;
    }
    else
    {
        /* Not executing current op.
           HOLD may *not* be assumed to contain currentobject's arguments.
           clear the flag.
        */
        ctx->currentobject.tag &= ~XPOST_OBJECT_TAG_DATA_FLAG_OPARGSINHOLD;
    }

    _xpost_operator_push_args_to_hold(ctx, ctx->lo, ctx->os, sp[i].in);
    hold = xpost_vm_ptr(ctx->lo, ctx->hold);

    switch(sp[i].in)
    {
        case 0:
            ret = ((int(*)(Xpost_Context*))sp[i].fp)(ctx); break;
        case 1:
            ret = ((int(*)(Xpost_Context*,Xpost_Object))sp[i].fp)
                (ctx, hold->data[0]); break;
        case 2:
            ret = ((int(*)(Xpost_Context*,Xpost_Object,Xpost_Object))sp[i].fp)
                (ctx, hold->data[0], hold->data[1]); break;
        case 3:
            ret = ((int(*)(Xpost_Context*,Xpost_Object,Xpost_Object,Xpost_Object))sp[i].fp)
                (ctx, hold->data[0], hold->data[1], hold->data[2]); break;
        case 4:
            ret = ((int(*)(Xpost_Context*,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object))sp[i].fp)
                (ctx, hold->data[0], hold->data[1], hold->data[2], hold->data[3]); break;
        case 5:
            ret = ((int(*)(Xpost_Context*,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object))sp[i].fp)
                (ctx, hold->data[0], hold->data[1], hold->data[2], hold->data[3], hold->data[4]); break;
        case 6:
            ret = ((int(*)(Xpost_Context*,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object))sp[i].fp)
                (ctx, hold->data[0], hold->data[1], hold->data[2], hold->data[3], hold->data[4], hold->data[5]); break;
        case 7:
            ret = ((int(*)(Xpost_Context*,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object))sp[i].fp)
                (ctx, hold->data[0], hold->data[1], hold->data[2], hold->data[3], hold->data[4], hold->data[5], hold->data[6]); break;
        case 8:
            ret =
                ((int(*)(Xpost_Context*,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object,Xpost_Object))sp[i].fp)
                (ctx, hold->data[0], hold->data[1], hold->data[2], hold->data[3], hold->data[4], hold->data[5], hold->data[6], hold->data[7]); break;
        default:
            ret = unregistered;
    }
    if (ret)
        return ret;
    /* A stream backed by a procedure answers a read with end of data and
       a write with a refusal, and carries the reason on the context.
       The failure belongs to the operator that reached through the
       stream, which is this one: the procedure ran inside this call. */
    if (ctx->callback_error)
    {
        ret = (int)ctx->callback_error;
        ctx->callback_error = 0;
        return ret;
    }
    /* An operator that pushed its result onto a stack that would not take
       it has finished without producing what it answers for. The push
       sites do not carry that back -- there are several hundred of them
       -- so it is read here, once, for every operator alike, while the
       operator that did the pushing is still the one an error would be
       reported against. */
    if (ctx->lo->push_refused)
    {
        ctx->lo->push_refused = 0;
        return VMerror;
    }
    return 0;
}
