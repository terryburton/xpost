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
#include <signal.h> /* sig_atomic_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_compat.h" /* xpost_isatty */
#include "xpost_memory.h"  /* itp contexts contain mfiles and mtabs */
#include "xpost_object.h"  /* eval functions examine objects */
#include "xpost_stack.h"  /* eval functions manipulate stacks */
#include "xpost_error.h"
#include "xpost_context.h"
#include "xpost_save.h"  /* save/restore vm */
#include "xpost_string.h"  /* eval functions examine strings */
#include "xpost_array.h"  /* eval functions examine arrays */
#include "xpost_name.h"  /* eval functions examine names */
#include "xpost_dict.h"  /* eval functions examine dicts */
#include "xpost_file.h"  /* eval functions examine files */

#include "xpost_interpreter.h" /* uses: context itp MAXCONTEXT MAXMFILE */
#include "xpost_garbage.h"  /*  test gc, install collect() in context's memory files */
#include "xpost_operator.h"  /* eval functions call operators */
#include "xpost_op_dict.h"  /* the shared def fast path */
#include "xpost_op_math.h"  /* the shared range-preserving arithmetic */
#include "xpost_op_control.h"  /* record the run outcome when a job ends */
#include "xpost_op_type.h"  /* the shared type naming */
#include "xpost_op_array.h"  /* the shared array element access */
#include "xpost_op_boolean.h"  /* the shared relations */
#include "xpost_op_stack.h"  /* the shared index and roll rules */
#include "xpost_oplib.h"
#include "xpost_handle.h"  /* the release a device's block was issued to be given up by */

static
Xpost_Object namedollarerror; /* cached result of xpost_name_cons(ctx, "$error")
                                 to reduce time in error handler */
static Xpost_Object nameerrordict;

int _xpost_interpreter_is_tracing = 0;             /* output trace log */
Xpost_Interpreter *itpdata;  /* the global interpreter instance, containing all contexts and memory files */

/* an external interrupt request: raised from a signal handler,
   consumed between evaluation steps */
static volatile sig_atomic_t _interrupt_pending = 0;

void xpost_interrupt(void)
{
    _interrupt_pending = 1;
}
static int _initializing = 1;  /* garbage collect does not run while _initializing is true.
                                  a getter function is exported in the memory file struct
                                  for the gc to access this global without #include'ing interpreter.h
                                  which would create a circular dependency. */

int eval(Xpost_Context *ctx);

/* What mainloop answers, named so that no two of them are the same
   number. A yield is a program's own doing -- the returntocaller
   operator, which showpage reaches under the returning semantics -- and
   a context that did not validate is the opposite of that: nothing ran
   and nothing can. Reported as a yield it hands the caller a context to
   resume, which validates no better on the next call and answers the
   same way, so an embedder driving a run to its end never reaches one.
   The failure is negative because every error number the interpreter
   carries is positive, and one of them stands where a yield stands. */
#define XPOST_MAINLOOP_DONE     0
#define XPOST_MAINLOOP_YIELDED  1
#define XPOST_MAINLOOP_INVALID (-1)

int mainloop(Xpost_Context *ctx);
void init(void);
void xit(void);

/*
   global shortcut for a single-threaded interpreter
FIXME: "static context pointer". s.b. changed to a returned
   value from xpost_create()
   value now returned. this variable should be removed */
Xpost_Context *xpost_ctx;

/* getter function for _initializing, for export */
int xpost_interpreter_get_initializing(void)
{
    return _initializing;
}

/* setter function for _initializing, for consistency */
void xpost_interpreter_set_initializing(int i)
{
    _initializing = i;
}

/*  allocate a global memory file
    find the next unused mfile in the global memory table */
static Xpost_Memory_File *xpost_interpreter_alloc_global_memory(void)
{
    int i;

    for (i = 0; i < MAXMFILE; i++)
    {
        if (itpdata->gtab[i].base == NULL)
        {
            return &itpdata->gtab[i];
        }
    }
    XPOST_LOG_ERR("cannot allocate Xpost_Memory_File, gtab exhausted");
    return NULL;
}

/* allocate a local memory file
   find the next unused mfile in the local memory table */
static Xpost_Memory_File *xpost_interpreter_alloc_local_memory(void)
{
    int i;
    for (i = 0; i < MAXMFILE; i++)
    {
        if (itpdata->ltab[i].base == NULL)
        {
            return &itpdata->ltab[i];
        }
    }
    XPOST_LOG_ERR("cannot allocate Xpost_Memory_File, ltab exhausted");
    return NULL;
}


/* cursor to next cid number to try to allocate */
static
unsigned int nextid = 0;

/* allocate a context-id and associated context struct
   returns cid;
   a context in state zero is considered available for allocation,
   this corresponds to the C_FREE enumeration constant.
 */
static int xpost_interpreter_cid_init(unsigned int *cid)
{
    unsigned int startid = nextid;
    /*printf("cid_init\n"); */
    while ( xpost_interpreter_cid_get_context(++nextid)->state != 0 )
    {
        if (nextid == startid + MAXCONTEXT)
        {
            XPOST_LOG_ERR("ctab full. cannot create new process");
            return 0;
        }
    }
    *cid = nextid;
    return 1;
}

/* adapter:
           ctx <- cid
   yield pointer to context struct given cid
   this function is exported via function-pointer in the memory file struct
   so the garbage collector can discover relevant contexts given only a memory file.
 */
Xpost_Context *xpost_interpreter_cid_get_context(unsigned int cid)
{
    /*TODO reject cid 0 */
    return &itpdata->ctab[ (cid - 1) % MAXCONTEXT ];
}


/* initialize the name string stacks and name search trees (per memory file).
   seed the search trees.
   initialize and populate the optab and systemdict (global memory file).
   push systemdict on dict stack.
   allocate and push globaldict on dict stack.
   allocate and push userdict on dict stack.
   return 1 on success, 0 on failure
 */
static
int _xpost_interpreter_extra_context_init(Xpost_Context *ctx, const char *device)
{
    int ret;
    ret = xpost_name_init(ctx); /* NAMES NAMET */
    if (!ret)
    {
        xpost_memory_file_exit(ctx->lo);
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }
    ctx->vmmode = GLOBAL;

    ret = xpost_operator_init_optab(ctx); /* allocate and zero the optab structure */
    if (!ret)
    {
        xpost_memory_file_exit(ctx->lo);
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }

    /* seed the tree with a word from the middle of the alphabet */
    /* middle of the start */
    /* middle of the end */
    if (xpost_object_get_type(xpost_name_cons(ctx, "maxlength")) == invalidtype)
        return 0;
    if (xpost_object_get_type(xpost_name_cons(ctx, "getinterval")) == invalidtype)
        return 0;
    if (xpost_object_get_type(xpost_name_cons(ctx, "setmiterlimit")) == invalidtype)
        return 0;
    if (xpost_object_get_type((namedollarerror = xpost_name_cons(ctx, "$error"))) == invalidtype)
        return 0;
    if (xpost_object_get_type((nameerrordict = xpost_name_cons(ctx, "errordict"))) == invalidtype)
        return 0;

    /* populate the optab (and systemdict) with operators */
    if (!xpost_oplib_init_ops(ctx))
    {
        xpost_memory_file_exit(ctx->lo);
        xpost_memory_file_exit(ctx->gl);
        return 0;
    }

    {
        Xpost_Object gd; /*globaldict */
        gd = xpost_dict_cons (ctx, 100);
        if (xpost_object_get_type(gd) == nulltype)
        {
            XPOST_LOG_ERR("cannot allocate globaldict");
            return 0;
        }
        ret = xpost_dict_put(ctx, xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0), xpost_name_cons(ctx, "globaldict"), gd);
        if (ret)
            return 0;
        xpost_stack_push(ctx->lo, ctx->ds, gd);
    }

    ctx->vmmode = LOCAL;
    /* seed the tree with a word from the middle of the alphabet */
    /* middle of the start */
    /* middle of the end */
    if (xpost_object_get_type(xpost_name_cons(ctx, "minimal")) == invalidtype)
        return 0;
    if (xpost_object_get_type(xpost_name_cons(ctx, "interest")) == invalidtype)
        return 0;
    if (xpost_object_get_type(xpost_name_cons(ctx, "solitaire")) == invalidtype)
        return 0;
    {
        Xpost_Object ud; /*userdict */
        ud = xpost_dict_cons (ctx, 100);
        if (xpost_object_get_type(ud) == nulltype)
        {
            XPOST_LOG_ERR("cannot allocate userdict");
            return 0;
        }
        ret = xpost_dict_put(ctx, ud, xpost_name_cons(ctx, "userdict"), ud);
        if (ret)
            return 0;
        xpost_stack_push(ctx->lo, ctx->ds, ud);
    }

    ctx->device_str = device;

    return 1;
}


/* initialize itpdata.
   create and initialize a single context in ctab[0]
 */
int xpost_interpreter_init(Xpost_Interpreter *itpptr, const char *device)
{
    int ret;

    ret = xpost_context_init(&itpptr->ctab[0],
                             xpost_interpreter_cid_init,
                             xpost_interpreter_cid_get_context,
                             xpost_interpreter_get_initializing,
                             xpost_interpreter_set_initializing,
                             xpost_interpreter_alloc_local_memory,
                             xpost_interpreter_alloc_global_memory,
                             xpost_garbage_collect);
    if (!ret)
    {
        return 0;
    }
    ret = _xpost_interpreter_extra_context_init(&itpptr->ctab[0], device);
    if (!ret)
    {
        return 0;
    }

    itpptr->cid = itpptr->ctab[0].id;

    return 1;
}

/* destroy context in ctab[0] */
void xpost_interpreter_exit(Xpost_Interpreter *itpptr)
{
    xpost_context_exit(&itpptr->ctab[0]);
}


/*
 *  Interpreter eval##type() actions.
 *
 */

/* function type for interpreter action pointers.
   eval() has already popped the object from the execution stack. */
typedef
int evalfunc(Xpost_Context *ctx, Xpost_Object t);

/* The stacks grow by VM segments without any structural bound, so a
   runaway loop or recursion would grind through memory rather than
   fail. Execution past these depths raises the stack's overflow
   error, checked at the two places depth accumulates: evalarray's
   internal procedure call and the interpreter loop. A latch per
   stack raises once per crossing, so the error machinery runs (and
   the program recovers) above the ceiling without retriggering it,
   and rearms when the depth recedes. The ceilings sit far beyond any
   legitimate job's depth while keeping the error path's walk over
   the stacks cheap. The exec ceiling leaves room for the
   deferred-paint queues the devices stage there: a vector device
   decomposes a large fill into very many queued spans. */
#define XPOST_EXEC_STACK_LIMIT 1000000
#define XPOST_OPER_STACK_LIMIT 1000000
#define XPOST_DICT_STACK_LIMIT 5000

/* Ceiling on errors handled back-to-back without the run reaching `stop`.
   A well-formed program recovers from every error through the error
   machinery, which ends in `stop`; that resets the count (see
   xpost_op_stop). Only a runaway cascade -- an error raised from inside
   the error machinery itself, before it can reach `stop` -- accumulates
   without bound. Left unchecked it spins until VM exhaustion; this makes
   it abort the job cleanly instead. The bound is far above any legitimate
   volume of caught-and-recovered errors, which never advance this count. */
#define XPOST_ERROR_CASCADE_LIMIT 4096

/* raise a stack's overflow error on crossing its ceiling; 0 if every
   stack is within bounds or already reported */
static int _stack_ceilings(Xpost_Context *ctx)
{
    if (ctx->es_over == 0)
    {
        if (xpost_stack_count(ctx->lo, ctx->es) > XPOST_EXEC_STACK_LIMIT)
        {
            ctx->es_over = 1;
            return execstackoverflow;
        }
    }
    else if (xpost_stack_count(ctx->lo, ctx->es) <= XPOST_EXEC_STACK_LIMIT)
        ctx->es_over = 0;
    if (ctx->os_over == 0)
    {
        if (xpost_stack_count(ctx->lo, ctx->os) > XPOST_OPER_STACK_LIMIT)
        {
            ctx->os_over = 1;
            return stackoverflow;
        }
    }
    else if (xpost_stack_count(ctx->lo, ctx->os) <= XPOST_OPER_STACK_LIMIT)
        ctx->os_over = 0;
    if (ctx->ds_over == 0)
    {
        if (xpost_stack_count(ctx->lo, ctx->ds) > XPOST_DICT_STACK_LIMIT)
        {
            ctx->ds_over = 1;
            return dictstackoverflow;
        }
    }
    else if (xpost_stack_count(ctx->lo, ctx->ds) <= XPOST_DICT_STACK_LIMIT)
        ctx->ds_over = 0;
    return 0;
}

/* quit the interpreter */
static
int evalquit(Xpost_Context *ctx, Xpost_Object t)
{
    (void)t;
    ++ctx->quit;
    return 0;
}

/* discard the object */
static
int evalpop(Xpost_Context *ctx, Xpost_Object t)
{
    (void)ctx;
    (void)t;
    return 0;
}

/* push the object on the operand stack */
static
int evalpush(Xpost_Context *ctx, Xpost_Object t)
{
    if (!xpost_stack_push(ctx->lo, ctx->os, t))
        return stackoverflow;
    return 0;
}

/* load executable name:
   search the dictionary stack for the topmost definition,
   as per the load operator, then push the value on the
   execution stack (if executable) or the operand stack (if literal) */
static
int evalload(Xpost_Context *ctx, Xpost_Object n)
{
    int i;

    if (_xpost_interpreter_is_tracing)
    {
        Xpost_Object s = xpost_name_get_string(ctx, n);
        XPOST_LOG_DUMP("evalload <name \"%*s\">", s.comp_.sz, xpost_string_get_pointer(ctx, s));
    }

    { /* consult the cache of resolutions against the dict stack */
        unsigned int key = ((unsigned int)n.mark_.padw << 1) |
            ((n.mark_.tag & XPOST_OBJECT_TAG_DATA_FLAG_BANK) ? 1 : 0);

        if (key < ctx->namecache_size &&
            ctx->namecache_gen[key] == ctx->namebind_gen)
        {
            Xpost_Object x = ctx->namecache_val[key];
            if (xpost_object_is_exe(x))
            {
                if (!xpost_stack_push(ctx->lo, ctx->es, x))
                    return execstackoverflow;
            }
            else
            {
                if (!xpost_stack_push(ctx->lo, ctx->os, x))
                    return stackoverflow;
            }
            return 0;
        }

        /* walk the dictionary stack segments directly, topmost first */
        {
        Xpost_Stack *ds_root = xpost_stack_at(ctx->lo, ctx->ds);
        Xpost_Stack *seg = xpost_stack_at(ctx->lo, ds_root->prevseg);

        for (;;)
        {
            for (i = seg->top; i--; )
            {
                Xpost_Object x = xpost_dict_get_name(ctx, seg->data[i], n);

                if (xpost_object_get_type(x) == invalidtype)
                    continue;

                if (key >= ctx->namecache_size)
                {
                    unsigned int nsz = ctx->namecache_size ? ctx->namecache_size : 4096;
                    unsigned int *ngen;
                    Xpost_Object *nval;
                    while (nsz <= key) nsz *= 2;
                    ngen = realloc(ctx->namecache_gen, nsz * sizeof(unsigned int));
                    nval = realloc(ctx->namecache_val, nsz * sizeof(Xpost_Object));
                    if (ngen)
                        ctx->namecache_gen = ngen;
                    if (nval)
                        ctx->namecache_val = nval;
                    if (ngen && nval)
                    {
                        memset(ctx->namecache_gen + ctx->namecache_size, 0,
                               (nsz - ctx->namecache_size) * sizeof(unsigned int));
                        ctx->namecache_size = nsz;
                    }
                }
                if (key < ctx->namecache_size)
                {
                    ctx->namecache_gen[key] = ctx->namebind_gen;
                    ctx->namecache_val[key] = x;
                }

                if (xpost_object_is_exe(x))
                {
                    if (!xpost_stack_push(ctx->lo, ctx->es, x))
                        return execstackoverflow;
                }
                else
                {
                    if (!xpost_stack_push(ctx->lo, ctx->os, x))
                        return stackoverflow;
                }
                return 0;
            }
            if (seg == ds_root)
                break;
            seg = xpost_stack_at(ctx->lo, seg->prevseg);
        }
        }
    }
    return undefined;
}

/* execute operator */
static
int evaloperator(Xpost_Context *ctx, Xpost_Object op)
{
    if (_xpost_interpreter_is_tracing)
        xpost_operator_dump(ctx, op.mark_.padw);
    return xpost_operator_exec(ctx, op.mark_.padw);
}

/* extract head (&tail) of array.
   steps successive elements of the procedure without re-entering the
   interpreter loop. the remaining interval is kept in the top slot of
   the execution stack, written lazily: literal elements cannot observe
   it, so it is brought up to date only before an element executes or
   the function returns. the loop returns to the interpreter whenever
   an element changes the execution stack, since anything it pushed
   must execute before the remaining interval. */
static
int evalarray(Xpost_Context *ctx, Xpost_Object a)
{
    Xpost_Object b;
    const Xpost_Object *abase;
    Xpost_Stack *es_root;
    Xpost_Stack *es_top;
    Xpost_Stack *os_root;
    Xpost_Stack *os_top;
    unsigned char *seen_lo_base = ctx->lo->base;
    unsigned char *seen_gl_base = ctx->gl->base;
    unsigned int off = a.comp_.off;
    unsigned int remaining = a.comp_.sz;
    int have_tail = 0;      /* a slot for the interval exists on es */
    unsigned int slot_off = 0; /* the interval offset currently in the slot */

    /* resolve the array's storage once; elements are then direct reads.
       re-derived after any fused call, which may move the memory file. */
#define EVALARRAY_RESOLVE_ABASE() \
    do { \
        Xpost_Memory_File *amem_ = xpost_context_select_memory(ctx, a); \
        unsigned int aent_ = xpost_object_get_ent(a); \
        if (xpost_ent_valid(amem_, aent_) && \
            (a.comp_.off + (unsigned int)a.comp_.sz) * sizeof(Xpost_Object) \
                <= amem_->table.tab[aent_].sz) \
            abase = (const Xpost_Object *)(amem_->base \
                    + amem_->table.tab[aent_].adr); \
        else \
            abase = NULL; \
    } while (0)

#define EVALARRAY_RESOLVE_STACKS() \
    do { \
        es_root = xpost_stack_at(ctx->lo, ctx->es); \
        es_top = xpost_stack_at(ctx->lo, es_root->prevseg); \
        os_root = xpost_stack_at(ctx->lo, ctx->os); \
        os_top = xpost_stack_at(ctx->lo, os_root->prevseg); \
    } while (0)

    /* a stack push can allocate a fresh segment, growing (and so
       relocating) the memory file: re-derive every cached pointer,
       abase included, whenever a base has moved */
#define EVALARRAY_RECHECK_BASES() \
    do { \
        if (ctx->lo->base != seen_lo_base || ctx->gl->base != seen_gl_base) \
        { \
            seen_lo_base = ctx->lo->base; \
            seen_gl_base = ctx->gl->base; \
            EVALARRAY_RESOLVE_ABASE(); \
        } \
        EVALARRAY_RESOLVE_STACKS(); \
    } while (0)

    /* write the remaining interval (elements from off+1) into the es
       slot, or drop the slot when this is the last element */
#define EVALARRAY_SYNC_SLOT() \
    do { \
        if (remaining > 1) \
        { \
            if (!have_tail || slot_off != off + 1) \
            { \
                Xpost_Object tail_ = a; \
                tail_.comp_.off = off + 1; \
                tail_.comp_.sz = remaining - 1; \
                if (have_tail && es_top->top > 0) \
                    es_top->data[es_top->top - 1] = tail_; \
                else if (es_top->top < XPOST_STACK_SEGMENT_SIZE - 1) \
                { \
                    es_top->data[es_top->top++] = tail_; \
                    have_tail = 1; \
                } \
                else \
                { \
                    if (!xpost_stack_push(ctx->lo, ctx->es, tail_)) \
                        return execstackoverflow; \
                    EVALARRAY_RECHECK_BASES(); \
                    have_tail = 1; \
                } \
                slot_off = off + 1; \
            } \
        } \
        else if (have_tail) \
        { \
            if (es_top->top > 0) \
            { \
                --es_top->top; \
                if (es_top->top == 0 && \
                    es_top != xpost_stack_at(ctx->lo, ctx->es)) \
                { \
                    /* the drop can retreat the top segment: the cached \
                       pointer must follow, or a later slot write lands \
                       above the live top and is silently lost */ \
                    es_root->prevseg = es_top->prevseg; \
                    es_top = xpost_stack_at(ctx->lo, es_root->prevseg); \
                } \
            } \
            else \
            { \
                (void)xpost_stack_pop(ctx->lo, ctx->es); \
                es_top = xpost_stack_at(ctx->lo, es_root->prevseg); \
            } \
            have_tail = 0; \
        } \
    } while (0)

    /* like SYNC_SLOT but roots the current element together with the rest, so
       the array's storage stays anchored across a collection even when this is
       the last element (SYNC_SLOT would drop the slot there). The normal slot
       sync corrects it to the tail, or drops it, before the element executes,
       so tail-call flattening is preserved. */
#define EVALARRAY_ROOT_CURRENT() \
    do { \
        Xpost_Object cur_ = a; \
        cur_.comp_.off = off; \
        cur_.comp_.sz = remaining; \
        if (have_tail && es_top->top > 0) \
            es_top->data[es_top->top - 1] = cur_; \
        else if (es_top->top < XPOST_STACK_SEGMENT_SIZE - 1) \
        { \
            es_top->data[es_top->top++] = cur_; \
            have_tail = 1; \
        } \
        else \
        { \
            if (!xpost_stack_push(ctx->lo, ctx->es, cur_)) \
                return execstackoverflow; \
            EVALARRAY_RECHECK_BASES(); \
            have_tail = 1; \
        } \
        slot_off = off; \
    } while (0)

    if (remaining == 0)
        return 0;

    EVALARRAY_RESOLVE_ABASE();
    EVALARRAY_RESOLVE_STACKS();

    for (;;)
    {
        Xpost_Object_Type btype;

        if (ctx->quit)
        {
            EVALARRAY_SYNC_SLOT();
            return 0;
        }

        /* between elements is a safe point just like the interpreter
           loop: a requested collection must not starve while a fused
           procedure runs through a long allocation-heavy stretch */
        if (ctx->lo->garbage_collect_pending)
        {
            ctx->lo->garbage_collect_pending = 0;
            /* anchor the current element (not just the tail) so an unrooted
               anonymous procedure is not swept while its last element runs */
            EVALARRAY_ROOT_CURRENT();
            if (ctx->lo->garbage_collect_is_installed
                && ctx->lo->garbage_collect(ctx->lo, 1, 1) < 0)
                return VMerror;
            EVALARRAY_RECHECK_BASES();
        }

        /* likewise a push the stack would not take: a fused procedure
           runs its elements without returning to the interpreter loop,
           so the refusal is read here too rather than waiting for the
           procedure to finish */
        if (ctx->lo->push_refused)
        {
            ctx->lo->push_refused = 0;
            XPOST_LOG_ERR("a stack would not take a pushed object");
            return VMerror;
        }

        if (abase)
            b = abase[off];
        else
        {
            Xpost_Object cur_ = a;
            cur_.comp_.off = off;
            cur_.comp_.sz = remaining;
            b = xpost_array_get(ctx, cur_, 0);
        }
        btype = xpost_object_get_type(b);
        if (btype == invalidtype || btype >= XPOST_OBJECT_NTYPES)
        {
            EVALARRAY_SYNC_SLOT();
            return unregistered;
        }

        if (btype == arraytype || !xpost_object_is_exe(b))
        {
            /* the interpreter cycle would only move it to the operand
               stack; do so directly */
            if (os_top->top < XPOST_STACK_SEGMENT_SIZE - 1)
                os_top->data[os_top->top++] = b;
            else
            {
                EVALARRAY_SYNC_SLOT();
                if (!xpost_stack_push(ctx->lo, ctx->os, b))
                    return stackoverflow;
                EVALARRAY_RECHECK_BASES();
            }
        }
        else if (btype == operatortype || btype == nametype)
        {
            unsigned int seen_seg;
            unsigned int seen_top;
            int ret;

            /* the hottest operators inline when their operands sit in
               the top segment; any precondition failure falls through
               to the generic invocation, keeping error behaviour
               identical */
            if (btype == operatortype)
            {
                unsigned int w = b.mark_.padw;
                unsigned int ot = os_top->top;

                ctx->currentobject = b;
                if (w == (unsigned int)XPOST_OP_CODE(ctx, oppop) && ot >= 1)
                {
                    --os_top->top;
                    goto next_element;
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, opexch) && ot >= 2)
                {
                    Xpost_Object t_ = os_top->data[ot - 1];
                    os_top->data[ot - 1] = os_top->data[ot - 2];
                    os_top->data[ot - 2] = t_;
                    goto next_element;
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, opdup) && ot >= 1 &&
                    ot < XPOST_STACK_SEGMENT_SIZE - 1)
                {
                    os_top->data[ot] = os_top->data[ot - 1];
                    ++os_top->top;
                    goto next_element;
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, opindex) && ot >= 2)
                {
                    Xpost_Object n_ = os_top->data[ot - 1];
                    /* the operator's own selection rule, applied to the
                       operands below n in this segment (see
                       xpost_op_stack.h) */
                    if (xpost_object_get_type(n_) == integertype &&
                        xpost_op_index_check(n_.int_.val, (int)ot - 1) == 0)
                    {
                        os_top->data[ot - 1] = os_top->data[ot - 2 - n_.int_.val];
                        goto next_element;
                    }
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, opget) && ot >= 2)
                {
                    Xpost_Object a_ = os_top->data[ot - 2];
                    Xpost_Object i_ = os_top->data[ot - 1];
                    if (xpost_object_get_type(a_) == arraytype &&
                        xpost_object_get_type(i_) == integertype)
                    {
                        /* the operator's own get, access checks and all
                           (see xpost_op_array.h) */
                        Xpost_Object t_;
                        if (xpost_op_array_get_checked(ctx, a_, i_.int_.val,
                                                       &t_) == 0)
                        {
                            --os_top->top;
                            os_top->data[ot - 2] = t_;
                            goto next_element;
                        }
                        /* on failure fall through: the generic path
                           re-executes the get for the exact protocol */
                    }
                }
                if (ot >= 2 &&
                    (w == (unsigned int)XPOST_OP_CODE(ctx, opadd) ||
                     w == (unsigned int)XPOST_OP_CODE(ctx, opsub) ||
                     w == (unsigned int)XPOST_OP_CODE(ctx, opmul)))
                {
                    Xpost_Object x_ = os_top->data[ot - 2];
                    Xpost_Object y_ = os_top->data[ot - 1];
                    if (xpost_object_get_type(x_) == integertype &&
                        xpost_object_get_type(y_) == integertype)
                    {
                        /* the operators' own range-preserving arithmetic,
                           so an out-of-range result becomes the same real
                           here as it does there (see xpost_op_math.h) */
                        Xpost_Object r_ =
                            w == (unsigned int)XPOST_OP_CODE(ctx, opadd)
                                ? xpost_int_add(x_.int_.val, y_.int_.val)
                            : w == (unsigned int)XPOST_OP_CODE(ctx, opsub)
                                ? xpost_int_sub(x_.int_.val, y_.int_.val)
                                : xpost_int_mul(x_.int_.val, y_.int_.val);
                        --os_top->top;
                        os_top->data[ot - 2] = r_;
                        goto next_element;
                    }
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, optype) && ot >= 1)
                {
                    /* the operator's own naming, so a packed array is
                       reported as its own type here as it is there
                       (see xpost_op_type.h) */
                    unsigned int k_ = xpost_op_type_index(os_top->data[ot - 1]);
                    if (xpost_object_get_type(ctx->typenames[k_]) != nametype)
                    {
                        ctx->typenames[k_] = xpost_object_cvx(
                            xpost_name_cons(ctx, xpost_op_type_name(k_)));
                        /* interning the name may grow (and so move) the
                           memory file: re-derive the cached pointers */
                        EVALARRAY_RECHECK_BASES();
                        ot = os_top->top;
                    }
                    os_top->data[ot - 1] = ctx->typenames[k_];
                    goto next_element;
                }
                if (ot >= 2)
                {
                    /* the operators' own comparison and their own
                       reading of it serve all six relations, so a pair
                       the comparison settles without reading vm answers
                       the same here as it does there (see xpost_dict.h
                       and xpost_op_boolean.h) */
                    int rel_ =
                        w == (unsigned int)XPOST_OP_CODE(ctx, opeq) ? XPOST_OP_REL_EQ :
                        w == (unsigned int)XPOST_OP_CODE(ctx, opne) ? XPOST_OP_REL_NE :
                        w == (unsigned int)XPOST_OP_CODE(ctx, oplt) ? XPOST_OP_REL_LT :
                        w == (unsigned int)XPOST_OP_CODE(ctx, ople) ? XPOST_OP_REL_LE :
                        w == (unsigned int)XPOST_OP_CODE(ctx, opgt) ? XPOST_OP_REL_GT :
                        w == (unsigned int)XPOST_OP_CODE(ctx, opge) ? XPOST_OP_REL_GE : -1;
                    int cmp_;

                    /* the ordered four are restricted to two numbers
                       or two strings, so the pair is asked the same
                       question the operators ask before either road
                       reaches the comparison */
                    if (rel_ >= 0 &&
                        xpost_op_relation_is_ordered((Xpost_Op_Relation)rel_) &&
                        !xpost_op_ordered_comparable(os_top->data[ot - 2],
                                                     os_top->data[ot - 1]))
                        rel_ = -1;
                    if (rel_ >= 0 &&
                        xpost_dict_compare_simple(os_top->data[ot - 2],
                                                  os_top->data[ot - 1], &cmp_))
                    {
                        --os_top->top;
                        os_top->data[ot - 2] = xpost_bool_cons(
                            xpost_op_relation((Xpost_Op_Relation)rel_, cmp_));
                        goto next_element;
                    }
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, oproll) && ot >= 2)
                {
                    Xpost_Object j_ = os_top->data[ot - 1];
                    Xpost_Object n_ = os_top->data[ot - 2];
                    if (xpost_object_get_type(n_) == integertype &&
                        xpost_object_get_type(j_) == integertype &&
                        n_.int_.val > 0 && n_.int_.val <= 32 &&
                        (unsigned int)n_.int_.val + 2 <= ot)
                    {
                        /* the operator's own shift and its own
                           placement rule, over the operands below n and
                           j: top_[-i] is position i counting down from
                           the top of the group (see xpost_op_stack.h) */
                        Xpost_Object tmp_[32];
                        integer n = n_.int_.val;
                        integer j = xpost_op_roll_shift(n, j_.int_.val);
                        integer k, held;
                        Xpost_Object *top_ = os_top->data + ot - 3;
                        /* the writeback covers exactly what the lift
                           put in the holder */
                        for (held = 0; held < n; held++)
                            tmp_[held] = top_[-xpost_op_roll_source(held, n, j)];
                        for (k = 0; k < held; k++)
                            top_[-k] = tmp_[k];
                        os_top->top -= 2;
                        goto next_element;
                    }
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, opdef) && ot >= 2)
                {
                    Xpost_Object k_ = os_top->data[ot - 2];
                    Xpost_Object v_ = os_top->data[ot - 1];
                    if (xpost_object_get_type(k_) == nametype)
                    {
                        Xpost_Stack *ds_root = xpost_stack_at(ctx->lo, ctx->ds);
                        Xpost_Stack *ds_top = xpost_stack_at(ctx->lo, ds_root->prevseg);
                        if (ds_top->top > 0)
                        {
                            Xpost_Object d_ = ds_top->data[ds_top->top - 1];
                            Xpost_Memory_File *dmem_ = xpost_context_select_memory(ctx, d_);
                            if (xpost_dict_def_fast_ok(ctx, dmem_, v_))
                            {
                                /* the operands stay on the stack through the
                                   put, keeping them visible to the collector
                                   if the dictionary grows; the shared def
                                   core carries the semantics (see
                                   xpost_op_dict.h) */
                                int ret_ = xpost_dict_def_cached(ctx, dmem_, d_, k_, v_);
                                if (ret_ == 0)
                                {
                                    if (ctx->lo->base != seen_lo_base ||
                                        ctx->gl->base != seen_gl_base)
                                    {
                                        seen_lo_base = ctx->lo->base;
                                        seen_gl_base = ctx->gl->base;
                                        EVALARRAY_RESOLVE_ABASE();
                                        EVALARRAY_RESOLVE_STACKS();
                                    }
                                    os_top->top -= 2;
                                    goto next_element;
                                }
                            }
                        }
                    }
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, opput) && ot >= 3)
                {
                    Xpost_Object a_ = os_top->data[ot - 3];
                    Xpost_Object i_ = os_top->data[ot - 2];
                    Xpost_Object v_ = os_top->data[ot - 1];
                    if (xpost_object_get_type(a_) == arraytype &&
                        xpost_object_get_type(i_) == integertype)
                    {
                        /* the operator's own put, access checks and all
                           (see xpost_op_array.h). The operands stay on
                           the stack through it, so a saved array that
                           copies on first write keeps them visible to
                           the collector */
                        int ret_ = xpost_op_array_put_checked(ctx, a_,
                                                              i_.int_.val, v_);
                        if (ret_ == 0)
                        {
                            if (ctx->lo->base != seen_lo_base ||
                                ctx->gl->base != seen_gl_base)
                            {
                                seen_lo_base = ctx->lo->base;
                                seen_gl_base = ctx->gl->base;
                                EVALARRAY_RESOLVE_ABASE();
                                EVALARRAY_RESOLVE_STACKS();
                            }
                            os_top->top -= 3;
                            goto next_element;
                        }
                        /* on failure fall through: the generic path
                           re-executes the put for the exact protocol */
                    }
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, opif) && ot >= 2)
                {
                    Xpost_Object p_ = os_top->data[ot - 1];
                    Xpost_Object b_ = os_top->data[ot - 2];
                    if (xpost_object_get_type(b_) == booleantype &&
                        xpost_object_get_type(p_) == arraytype &&
                        xpost_object_is_exe(p_) &&
                        /* the operator refuses a procedure it may not
                           read, whether or not the condition selects it
                           (see xpost_op_control.h) */
                        xpost_op_exec_access_ok(ctx, p_))
                    {
                        os_top->top -= 2;
                        if (!b_.int_.val)
                            goto next_element;
                        EVALARRAY_SYNC_SLOT();
                        have_tail = 0;
                        a = p_;
                        off = a.comp_.off;
                        remaining = a.comp_.sz;
                        if (remaining == 0)
                            return 0;
                        EVALARRAY_RESOLVE_ABASE();
                        continue;
                    }
                }
                if (w == (unsigned int)XPOST_OP_CODE(ctx, opifelse) && ot >= 3)
                {
                    Xpost_Object p2_ = os_top->data[ot - 1];
                    Xpost_Object p1_ = os_top->data[ot - 2];
                    Xpost_Object b_ = os_top->data[ot - 3];
                    if (xpost_object_get_type(b_) == booleantype &&
                        xpost_object_get_type(p1_) == arraytype &&
                        xpost_object_is_exe(p1_) &&
                        xpost_object_get_type(p2_) == arraytype &&
                        xpost_object_is_exe(p2_) &&
                        /* the operator refuses either procedure it may
                           not read, whichever the condition selects
                           (see xpost_op_control.h) */
                        xpost_op_exec_access_ok(ctx, p1_) &&
                        xpost_op_exec_access_ok(ctx, p2_))
                    {
                        os_top->top -= 3;
                        EVALARRAY_SYNC_SLOT();
                        have_tail = 0;
                        a = b_.int_.val ? p1_ : p2_;
                        off = a.comp_.off;
                        remaining = a.comp_.sz;
                        if (remaining == 0)
                            return 0;
                        EVALARRAY_RESOLVE_ABASE();
                        continue;
                    }
                }
            }

            if (btype == nametype)
            {
                /* resolve via the name cache without leaving the loop */
                unsigned int key = ((unsigned int)b.mark_.padw << 1) |
                    ((b.mark_.tag & XPOST_OBJECT_TAG_DATA_FLAG_BANK) ? 1 : 0);
                if (key < ctx->namecache_size &&
                    ctx->namecache_gen[key] == ctx->namebind_gen)
                {
                    Xpost_Object x = ctx->namecache_val[key];
                    if (!xpost_object_is_exe(x))
                    {
                        if (os_top->top < XPOST_STACK_SEGMENT_SIZE - 1)
                        {
                            os_top->data[os_top->top++] = x;
                            goto next_element;
                        }
                    }
                    else if (xpost_object_get_type(x) == operatortype)
                    {
                        /* execute the bound operator via the generic
                           machinery below, sparing the es round-trip */
                        b = x;
                        btype = operatortype;
                        ctx->currentobject = b;
                        goto generic_operator;
                    }
                    else if (xpost_object_get_type(x) == arraytype)
                    {
                        /* a procedure call: continue stepping it here,
                           leaving the current interval behind on es.
                           Recursion deepens the stacks through this
                           site without ever surfacing to the
                           interpreter loop, so the ceilings are kept
                           here */
                        int over = _stack_ceilings(ctx);
                        if (over)
                        {
                            ctx->currentobject = b;
                            EVALARRAY_SYNC_SLOT();
                            return over;
                        }
                        EVALARRAY_SYNC_SLOT();
                        have_tail = 0;
                        a = x;
                        off = a.comp_.off;
                        remaining = a.comp_.sz;
                        if (remaining == 0)
                            return 0;
                        EVALARRAY_RESOLVE_ABASE();
                        continue;
                    }
                }
            }

          generic_operator:
            EVALARRAY_SYNC_SLOT();

            /* remember the execution stack position of our interval */
            seen_seg = es_root->prevseg;
            es_top = xpost_stack_at(ctx->lo, seen_seg);
            seen_top = es_top->top;

            ctx->currentobject = b;
            if (btype == operatortype)
            {
                if (_xpost_interpreter_is_tracing)
                    xpost_operator_dump(ctx, b.mark_.padw);
                ret = xpost_operator_exec(ctx, b.mark_.padw);
            }
            else
                ret = evalload(ctx, b);
            if (ret)
                return ret;
            if (ctx->quit)
                return 0;

            /* if the execution stack changed, what was pushed (or the
               unwound state) takes precedence: resume via the loop */
            es_root = xpost_stack_at(ctx->lo, ctx->es);
            if (es_root->prevseg != seen_seg)
                return 0;
            es_top = xpost_stack_at(ctx->lo, seen_seg);
            if (es_top->top != seen_top)
                return 0;
            if (have_tail)
            {
                Xpost_Object slot = es_top->data[seen_top - 1];
                if (slot.tag != a.comp_.tag ||
                    slot.comp_.sz != remaining - 1 ||
                    slot.comp_.off != off + 1 ||
                    xpost_object_get_ent(slot) != xpost_object_get_ent(a))
                    return 0;
            }
            if (ctx->lo->base != seen_lo_base || ctx->gl->base != seen_gl_base)
            {
                seen_lo_base = ctx->lo->base;
                seen_gl_base = ctx->gl->base;
                EVALARRAY_RESOLVE_ABASE();
            }
            EVALARRAY_RESOLVE_STACKS();
        }
        else
        {
            /* rarer executable types resume via the interpreter loop */
            EVALARRAY_SYNC_SLOT();
            if (!xpost_stack_push(ctx->lo, ctx->es, b))
                return execstackoverflow;
            return 0;
        }

      next_element:
        if (remaining == 1)
        {
            /* the slot, if any, still holds off+1..; it must not
               survive: it was consumed by this call */
            EVALARRAY_SYNC_SLOT();
            return 0;
        }
        ++off;
        --remaining;
    }
#undef EVALARRAY_RESOLVE_ABASE
#undef EVALARRAY_RESOLVE_STACKS
#undef EVALARRAY_SYNC_SLOT
#undef EVALARRAY_ROOT_CURRENT
}

/* extract token from string */
static
int evalstring(Xpost_Context *ctx, Xpost_Object s)
{
    Xpost_Object b,t;
    int ret;

    if (!xpost_stack_push(ctx->lo, ctx->os, s))
        return stackoverflow;
    assert(ctx->gl->base);
    ret = xpost_operator_exec(ctx, XPOST_OP_CODE(ctx, token));
    if (ret)
        return ret;
    b = xpost_stack_pop(ctx->lo, ctx->os);
    if (xpost_object_get_type(b) == invalidtype)
        return stackunderflow;
    if (b.int_.val)
    {
        t = xpost_stack_pop(ctx->lo, ctx->os);
        if (xpost_object_get_type(t) == invalidtype)
            return stackunderflow;
        s = xpost_stack_pop(ctx->lo, ctx->os);
        if (xpost_object_get_type(s) == invalidtype)
            return stackunderflow;
        if (!xpost_stack_push(ctx->lo, ctx->es, s))
            return execstackoverflow;
        if (xpost_object_get_type(t)==arraytype && ctx->scanner_defer)
        {
            if (!xpost_stack_push(ctx->lo, ctx->os , t))
                return stackoverflow;
        }
        else
        {
            if (!xpost_stack_push(ctx->lo, ctx->es , t))
                return execstackoverflow;
        }
    }
    return 0;
}

/* extract token from file */
static
int evalfile(Xpost_Context *ctx, Xpost_Object f)
{
    Xpost_Object b,t;
    int ret;

    /* Executing a file reads it, a token at a time, so the access
       attribute the file object carries governs execution as it governs
       read: a file the attribute will not let be read is not executed
       either (PLRM 3.8.2). The attribute belongs to the object rather
       than to the stream, so it is asked before the stream's state is,
       and every route by which a file reaches the execution stack --
       exec, run, a file object stored in a procedure -- arrives here. */
    if (!xpost_object_is_readable(ctx, f))
        return invalidaccess;

    /* a program may close the file it is executing from -- the
       Type 1 font idiom mark currentfile closefile -- and a closed
       file simply has nothing further to run */
    if (!xpost_file_get_status(ctx->lo, f))
        return 0;

    if (!xpost_stack_push(ctx->lo, ctx->os, f))
        return stackoverflow;
    assert(ctx->gl->base);
    ret = xpost_operator_exec(ctx, XPOST_OP_CODE(ctx, token));
    if (ret)
        return ret;
    b = xpost_stack_pop(ctx->lo, ctx->os);
    if (b.int_.val)
    {
        t = xpost_stack_pop(ctx->lo, ctx->os);
        if (!xpost_stack_push(ctx->lo, ctx->es, f))
            return execstackoverflow;
        if (xpost_object_get_type(t)==arraytype && ctx->scanner_defer)
        {
            if (!xpost_stack_push(ctx->lo, ctx->os, t))
                return stackoverflow;
        }
        else
        {
            if (!xpost_stack_push(ctx->lo, ctx->es, t))
                return execstackoverflow;
        }
    }
    else
    {
        ret = xpost_file_object_close_at_eod(ctx->lo, f);
        if (ret)
            XPOST_LOG_ERR("%s error closing file", errorname[ret]);
    }
    return 0;
}

/* interpreter actions for executable types */
evalfunc *evalinvalid = evalquit;
evalfunc *evalmark = evalpush;
evalfunc *evalnull = evalpop;
evalfunc *evalinteger = evalpush;
evalfunc *evalboolean = evalpush;
evalfunc *evalreal = evalpush;
evalfunc *evalsave = evalpush;
evalfunc *evaldict = evalpush;
evalfunc *evalextended = evalquit;
evalfunc *evalglob = evalpush;
evalfunc *evalmagic = evalquit;

evalfunc *evalcontext = evalpush;
evalfunc *evalname = evalload;

/* install the evaltype functions (possibly via pointers) in the jump table */
evalfunc *evaltype[XPOST_OBJECT_NTYPES + 1];
#define AS_EVALINIT(_) evaltype[ _ ## type ] = eval ## _ ;

/* use above macro to initialize function table
   keyed by enum types;
 */
static
void initevaltype(void)
{
    XPOST_OBJECT_TYPES(AS_EVALINIT)
}


/*
   call window device's event_handler function
   which should check for Events or Messages from the
   underlying Window System, process one or more of them,
   and then return 0.
   it should leave all stacks undisturbed.
 */
int idleproc (Xpost_Context *ctx)
{
    int ret;

    if ((xpost_object_get_type(ctx->event_handler) == operatortype) &&
        (xpost_object_get_type(ctx->window_device) == dicttype))
    {
        if (!xpost_stack_push(ctx->lo, ctx->os, ctx->window_device))
        {
            return stackoverflow;
        }
        ret = xpost_operator_exec(ctx, ctx->event_handler.mark_.padw);
        if (ret)
        {
            XPOST_LOG_ERR("event_handler returned %d (%s)",
                    ret, errorname[ret]);
            XPOST_LOG_ERR("disabling event_handler");
            ctx->event_handler = null;
            return ret;
        }
    }
    return 0;
}

/*
   check basic pointers and addresses for sanity
 */
static
int validate_context(Xpost_Context *ctx)
{
    /*assert(ctx); */
    /*assert(ctx->lo); */
    /*assert(ctx->lo->base); */
    /*assert(ctx->gl); */
    /*assert(ctx->gl->base); */
    if (!ctx)
    {
        XPOST_LOG_ERR("ctx invalid");
        return 0;
    }
    if (!ctx->lo)
    {
        XPOST_LOG_ERR("ctx->lo invalid");
        return 0;
    }
    if (!ctx->lo->base)
    {
        XPOST_LOG_ERR("ctx->lo->base invalid");
        return 0;
    }
    if (!ctx->gl)
    {
        XPOST_LOG_ERR("ctx->gl invalid");
        return 0;
    }
    if (!ctx->gl->base)
    {
        XPOST_LOG_ERR("ctx->gl->base invalid");
        return 0;
    }
    return 1;
}

/*
   one iteration of the central loop
   called repeatedly by mainloop()
 */
int eval(Xpost_Context *ctx)
{
    int ret;
    Xpost_Object t;
    Xpost_Stack *es_root;
    Xpost_Stack *es_top;
    Xpost_Object_Type type;

    /* pop the next object, directly off the top segment when possible */
    es_root = xpost_stack_at(ctx->lo, ctx->es);
    es_top = xpost_stack_at(ctx->lo, es_root->prevseg);
    if (es_top->top > 0)
        t = es_top->data[--es_top->top];
    else
        t = xpost_stack_pop(ctx->lo, ctx->es);

    ctx->currentobject = t; /* for _onerror to determine if hold stack contents are restoreable.
                               if opexec(opcode) discovers opcode != ctx->currentobject.mark_.padw
                               it sets a flag indicating the hold stack does not contain
                               ctx->currentobject's arguments.
                               if an error is encountered, currentobject is reported as the
                               errant object since it is the "entry point" to the interpreter.
                             */

    if (_xpost_interpreter_is_tracing)
    {
        //XPOST_LOG_DUMP("eval(): Executing: ");
        xpost_object_dump(t);
        //XPOST_LOG_DUMP("Stack: ");
        //xpost_stack_dump(ctx->lo, ctx->os);
        //XPOST_LOG_DUMP("Dict Stack: ");
        //xpost_stack_dump(ctx->lo, ctx->ds);
        //XPOST_LOG_DUMP("Exec Stack: ");
        //xpost_stack_dump(ctx->lo, ctx->es);
    }

    if (xpost_object_get_type(ctx->event_handler) == operatortype)
    {
        ret = idleproc(ctx); /* periodically process asynchronous events */
        if (ret)
            return ret;
    }

    /* check object for sanity before using jump table */
    type = xpost_object_get_type(t);
    if (type == invalidtype || type >= XPOST_OBJECT_NTYPES)
        return unregistered;

    if ( xpost_object_is_exe(t) ) /* if executable */
    {
        /* dispatch the common types with predictable direct calls;
           the jump table's indirect branch mispredicts heavily */
        switch (type)
        {
            case operatortype: ret = evaloperator(ctx, t); break;
            case arraytype:    ret = evalarray(ctx, t);    break;
            case nametype:     ret = evalload(ctx, t);     break;
            case integertype:  /*@fallthrough@*/
            case realtype:     /*@fallthrough@*/
            case booleantype:  ret = evalpush(ctx, t);     break;
            default:           ret = evaltype[type](ctx, t);
        }
    }
    else
        ret = evalpush(ctx, t);

    return ret;
}

/* An error leaving a wrapped operator is the operator's error.
   Each live call left its frame on the exec stack -- the operator, the
   operand and dict depths at the call, and the operands it was called
   with, under the finish marker -- so the frames above the nearest
   stopped context are exactly the calls the coming stop will unwind
   out of: the innermost names the command, and the stacks go back to
   what the outermost found. A call whose frame sits below the stopped
   context is left alone: its procedure keeps running and its stacks
   are its own business.

   The walk ends at a callout boundary for the same reason. An operator
   that calls back into a procedure of the program's puts one on the
   exec stack beneath the procedure, and a failure above it is the
   procedure's: the object being executed is the program's own, so the
   stack goes back to what the procedure found and no further. The
   operator underneath consumed its operands doing the work that led to
   the call, and they stay consumed. The boundary is one-sided -- a call
   made from inside the procedure has its frame above it, and refuses on
   its own operands as any other call does.

   A failure leaves through the boundary in more than one step: the
   bracket around the procedure catches it, unwinds what it had open and
   raises it again, and so may a bracket further out, each re-raise
   arriving after the boundary has gone with the stopped context it sat
   under. So the walk marks the call beneath the boundary as it passes,
   and every later step reads the mark and leaves that call, and
   everything under it, alone.

   Two things stand between the stacks as they are and the state the
   outermost call found (PLRM 3.11.1 step 1): values the calls pushed
   and did not consume, dropped by going back to the shallowest depth
   any of them recorded, and operands they did consume, put back from
   the copies the outermost call took. Where a body consumed deeper
   than the copies reach, the stack keeps what the truncation leaves
   it, which is the whole of what an unwind could do before there were
   any copies.

   Every way an error is raised comes here: the interpreter's own, the
   one a PostScript body raises with signalerror, and stop, which is
   where all of them end and the only place a re-raise reaches. An
   operator leaves nothing behind however it failed, so no path may
   skip this. Several paths therefore run it over the same frames, and
   it is written to be read twice: it reads the copies and never spends
   them, and the frames are let go by whatever finally discards them.

   Returns 1 if a frame was found, having set ctx->currentobject to the
   operator it names. */
static int
_unwind_wrapped_calls(Xpost_Context *ctx)
{
    Xpost_Object fmark = xpost_bool_cons(0);
    Xpost_Object outrun = null; /* the outermost call's saved operands */
    int found = 0;
    int minos = 0, minds = 0, outos = 0;
    unsigned int cmdop = 0;
    /* Walk the exec stack top-down in a SINGLE pass over its
       segments -- O(depth), not the O(depth^2) that repeated
       topdown_fetch would cost. A deep stack at error time (a
       runaway or a cascading error handler) would otherwise make
       error handling itself the bottleneck. Stop at the nearer of
       the stopped context (a bool false) and the callout boundary;
       above it, each wrapped call's finish marker is followed,
       deeper, by its saved operands and its ds, os and opcode
       integers. */
    Xpost_Stack *esroot = xpost_stack_at(ctx->lo, ctx->es);
    Xpost_Stack *seg = esroot->prevseg
        ? xpost_stack_at(ctx->lo, esroot->prevseg) : esroot;
    int p = (int)seg->top - 1;
    int pending = 0; /* frame slots to read: 4->operands 3->ds 2->os 1->opcode */
    int sealing = 0; /* past a boundary, looking for the call under it */
    int fds = 0, fos = 0;
    Xpost_Object frun = null;

    for (;;)
    {
        Xpost_Object x;
        if (p < 0)
        {
            if (seg == esroot)
                break;
            seg = xpost_stack_at(ctx->lo, seg->prevseg);
            p = (int)seg->top - 1;
            continue;
        }
        x = seg->data[p];
        p--;
        if (sealing)
        {
            /* Past the boundary. The call under it is the one that
               handed control to the program, and it keeps what it
               consumed getting there -- not just for this walk but for
               every later step of the same failure, which arrives after
               the boundary itself has gone with the stopped context it
               sat under. Its marker is changed in place to say so, and
               the walk ends there: what is under it is under the
               program's procedure too. */
            if (xpost_object_get_type(x) == operatortype)
            {
                if (x.mark_.padw == (unsigned int)XPOST_OP_CODE(ctx, wrapsealed))
                    break; /* said already */
                if (x.mark_.padw == (unsigned int)XPOST_OP_CODE(ctx, wrapdone))
                {
                    seg->data[p + 1] = XPOST_OP(ctx, wrapsealed);
                    break;
                }
            }
            continue;
        }
        if (pending)
        {
            if (pending == 4)
                frun = x; /* an array of operands, or null for none */
            else if (xpost_object_get_type(x) != integertype)
            {
                pending = 0; /* malformed frame -- ignore it */
                continue;
            }
            else if (pending == 3)
                fds = (int)x.int_.val;
            else if (pending == 2)
                fos = (int)x.int_.val;
            else
            {
                if (!found)
                {
                    found = 1;
                    cmdop = (unsigned int)x.int_.val;
                    minos = fos;
                    minds = fds;
                }
                else
                {
                    if (fos < minos) minos = fos;
                    if (fds < minds) minds = fds;
                }
                /* the walk reads the innermost call first, so the last
                   frame it reads is the outermost: the call whose
                   caller is about to be handed the stack */
                outos = fos;
                outrun = frun;
            }
            --pending;
            continue;
        }
        if (xpost_dict_compare_objects(ctx, fmark, x) == 0)
            break; /* the coming stop unwinds to here */
        if (xpost_object_get_type(x) == operatortype)
        {
            if (x.mark_.padw == (unsigned int)XPOST_OP_CODE(ctx, wrapsealed))
                break; /* a call already known to be under a procedure */
            if (x.mark_.padw == (unsigned int)XPOST_OP_CODE(ctx, calloutdone))
            {
                sealing = 1; /* the failure is the program's procedure's */
                continue;
            }
            if (x.mark_.padw == (unsigned int)XPOST_OP_CODE(ctx, wrapdone))
                pending = 4;
        }
    }
    if (found)
    {
        int oscount, dscount;

        ctx->currentobject = xpost_operator_cons_opcode(cmdop);
        oscount = xpost_stack_count(ctx->lo, ctx->os);
        while (oscount > minos)
        {
            (void)xpost_stack_pop(ctx->lo, ctx->os);
            --oscount;
        }
        if (xpost_object_get_type(outrun) == arraytype)
        {
            /* The copies are the operands the call was made with, the
               deepest of them at the depth outos - size. All of them go
               back, not only as many as are missing: a body that
               consumed two and pushed two again stands at the depth it
               started from holding values of its own. */
            int base = outos - (int)outrun.comp_.sz;

            if (oscount >= base)
            {
                while (oscount > base)
                {
                    (void)xpost_stack_pop(ctx->lo, ctx->os);
                    --oscount;
                }
                while (oscount < outos)
                {
                    if (!xpost_stack_push(ctx->lo, ctx->os,
                                          xpost_array_get(ctx, outrun,
                                                          oscount - base)))
                        break;
                    ++oscount;
                }
            }
        }
        dscount = xpost_stack_count(ctx->lo, ctx->ds);
        if (dscount > minds && minds >= 3)
        {
            ++ctx->namebind_gen; /* visibility changes */
            while (dscount > minds)
            {
                (void)xpost_stack_pop(ctx->lo, ctx->ds);
                --dscount;
            }
        }
    }
    return found;
}

/* The same unwinding, asked for from PostScript. An error a body
   raises with signalerror never reaches _onerror, and the hook that
   handles it wants the stacks unwound before it records them in
   $error, so it asks here. stop asks too, for the calls it abandons. */
int xpost_op_errorunwind(Xpost_Context *ctx)
{
    (void)_unwind_wrapped_calls(ctx);
    return 0;
}

/* called by mainloop() after propagated error codes.
   pushes postscript-level error procedures
   and resumes normal execution.
 */
static
void _onerror(Xpost_Context *ctx,
        unsigned int err)
{
    Xpost_Object sd;
    Xpost_Object ed;
    Xpost_Object handler;
    Xpost_Object dollarerror;

    if (err > unknownerror) err = unknownerror;

    /* An allocation refused for want of entity numbers is an
       implementation limit reached, not memory spent: the object field
       that carries an entity number is only so wide, while the memory
       behind the table has room and vmstatus reports it. PLRM Appendix
       B has limitcheck for exactly this and keeps VMerror for VM
       resources exhausted. The composite that was refused is a
       constructor's, several call frames below, so the distinction
       arrives here on the memory file rather than in the returned
       code. */
    if (err == VMerror && (ctx->lo->ent_exhausted || ctx->gl->ent_exhausted))
        err = limitcheck;

    /* Whatever the error, reporting it starts here and wants entities
       of its own: the error's name, $error's stack snapshots, the text
       the handler prints, and whatever the program's own handler makes
       of them. Open the numbers held back for that, in both memory
       files -- an error over one is reported with names and snapshots
       drawn from either. They shut themselves again at the first
       allocation the run makes with room to spare. */
    ctx->lo->ent_reserve_open = 1;
    ctx->gl->ent_reserve_open = 1;

    strncpy(ctx->run_error_name, errorname[err], sizeof ctx->run_error_name - 1);
    ctx->run_error_name[sizeof ctx->run_error_name - 1] = '\0';

    if (!validate_context(ctx))
        XPOST_LOG_ERR("context not valid");

    /* if a fault interrupts loading the graphics language into systemdict,
       restore systemdict to read-only so the writeable window never outlives
       the load */
    if (ctx->sysdict_unlocked)
    {
        xpost_object_set_access(ctx,
                xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0),
                XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
        ctx->sysdict_unlocked = 0;
        ctx->sysdict_load_done = 1;
    }

    if (itpdata->in_onerror > 5)
    {
        fprintf(stderr, "LOOP in error handler\nabort\n");
        ++ctx->quit;
        /*exit(undefinedresult); */
    }

    /* A runaway error cascade re-enters here without ever reaching `stop`
       (the error machinery raises before it can recover), so the nested
       `in_onerror` guard above -- reset on every completed pass -- never
       trips. Count consecutive handled errors instead and abort the job
       when they run away, turning an otherwise unbounded spin into a
       clean errored exit. xpost_op_stop clears the count on recovery. */
    if (++ctx->onerr_run > XPOST_ERROR_CASCADE_LIMIT)
    {
        fprintf(stderr, "runaway error cascade (%s)\nabort\n",
                errorname[err]);
        ctx->run_uncaught = 1;
        ++ctx->quit;
        return;
    }

    ++itpdata->in_onerror;

#ifdef EMITONERROR
    fprintf(stderr, "err: %s\n", errorname[err]);
#endif

    /* reset stack */
    if ((xpost_object_get_type(ctx->currentobject) == operatortype) &&
        (ctx->currentobject.tag & XPOST_OBJECT_TAG_DATA_FLAG_OPARGSINHOLD))
    {
        int n = ctx->currentobject.mark_.pad0;
        int i;
        for (i = 0; i < n; i++)
        {
            xpost_stack_push(ctx->lo, ctx->os,
                    xpost_stack_bottomup_fetch(ctx->lo, ctx->hold, i));
        }
        /* the restored args carry the dispatcher's integer->real coercions;
           put back the integers the program actually pushed (PLRM 3.11) */
        for (i = 0; i < ctx->op_restore_n; i++)
        {
            int idx = ctx->op_restore_idx[i];
            /* the n arguments were just pushed back, so an index
               below n is one the stack now has */
            if (idx < n)
                XPOST_REFUSAL_IMPOSSIBLE(
                    xpost_stack_topdown_replace(ctx->lo, ctx->os, idx,
                                                ctx->op_restore_val[i]));
        }
    }

    (void)_unwind_wrapped_calls(ctx);

    /* printf("1\n"); */
    sd = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);

    /* printf("2\n"); */
    dollarerror = xpost_dict_get(ctx, sd, namedollarerror);
    if (xpost_object_get_type(dollarerror) == invalidtype)
    {
        XPOST_LOG_ERR("cannot load $error dict for error: %s",
                errorname[err]);
        xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, stop));
        /*itpdata->in_onerror = 0; */
        return;
    }

    /* printf("3\n"); */
    /* printf("4\n"); */
    /* printf("5\n"); */
    xpost_stack_push(ctx->lo, ctx->os, ctx->currentobject);

    ed = xpost_dict_get(ctx, sd, nameerrordict);
    handler = xpost_dict_get(ctx, ed, xpost_name_cons(ctx, errorname[err]));
    /* the handler runs when the interpreter schedules it, so it has to
       be executable: a literal is pushed on the operand stack and the
       error goes no further */
    if (xpost_object_get_type(handler) != invalidtype &&
        xpost_object_is_exe(handler) &&
        xpost_stack_push(ctx->lo, ctx->es, handler))
    {
        itpdata->in_onerror = 0;
        return;
    }

    /* errordict carries no handler for this error, or it cannot be
       scheduled. errordict is writable by design (PLRM 3.11.1: a program
       substitutes its own handlers there), and removing an entry is the
       program's own doing, but the error must still take effect: resuming
       as though the operator had succeeded is the one outcome the language
       does not allow.
       Raise it the way signalerror does instead -- the errorname beside
       the command already on the operand stack -- so $error records the
       error, its name and the stack snapshots exactly as a standard
       handler would, and the stop those handlers end with surfaces to any
       enclosing stopped context. */
    {
        Xpost_Object sig = xpost_dict_get(ctx, sd,
                                          xpost_name_cons(ctx, "signalerror"));

        XPOST_LOG_ERR("no errordict handler for /%s; raising it directly",
                      errorname[err]);
        if (xpost_object_get_type(sig) != invalidtype &&
            xpost_stack_push(ctx->lo, ctx->os,
                             xpost_object_cvlit(xpost_name_cons(ctx, errorname[err]))) &&
            xpost_stack_push(ctx->lo, ctx->es, xpost_object_cvx(sig)))
        {
            itpdata->in_onerror = 0;
            return;
        }
        /* the error machinery itself is unusable: end the job rather
           than continue past the error */
        (void)xpost_op_stop(ctx);
    }

    /* printf("8\n"); */
    itpdata->in_onerror = 0;
}


/*
   select a new context to execute and return it
   scan for the next context in the C_RUN state
   along the way, change C_WAIT contexts to C_RUN
   to retry wait conditions.
 */
static
Xpost_Context *_switch_context(Xpost_Context *ctx)
{
    int i;

    /* One context executes, so the next one to execute is this one. The
       rest of this function is the scan across the context table -- the
       next entry in C_RUN, wrapping at the end, moving the entries it
       passes out of C_WAIT and C_IOBLOCK so a wait condition is retried
       -- and nothing reaches it: the return is above it. It is parked
       here beside the choice it would make; a reader meeting it should
       read it as not yet enabled rather than as something left behind. */
    return ctx;

    /* return next context to execute */
    printf("--switching contexts--\n");
    /*putchar('.'); fflush(0); */
    for (i = (ctx - itpdata->ctab) + 1; i < MAXCONTEXT; i++)
    {
        /*printf("--%d-- %d\n", itpdata->ctab[i].id, itpdata->ctab[i].state); */
        if (itpdata->ctab[i].state == C_RUN)
        {
            return &itpdata->ctab[i];
        }
        if (itpdata->ctab[i].state == C_WAIT || itpdata->ctab[i].state == C_IOBLOCK)
        {
            itpdata->ctab[i].state = C_RUN;
        }
    }
    for (i = 0; i <= ctx-itpdata->ctab; i++)
    {
        /*printf("--%d-- %d\n", itpdata->ctab[i].id, itpdata->ctab[i].state); */
        if (itpdata->ctab[i].state == C_RUN)
        {
            return &itpdata->ctab[i];
        }
        if (itpdata->ctab[i].state == C_WAIT || itpdata->ctab[i].state == C_IOBLOCK)
        {
            itpdata->ctab[i].state = C_RUN;
        }
    }
    for (i = (ctx - itpdata->ctab) + 1; i < MAXCONTEXT; i++)
    {
        /*printf("--%d-- %d\n", itpdata->ctab[i].id, itpdata->ctab[i].state); */
        if (itpdata->ctab[i].state == C_RUN)
        {
            return &itpdata->ctab[i];
        }
    }
    for (i = 0; i <= ctx-itpdata->ctab; i++)
    {
        /*printf("--%d-- %d\n", itpdata->ctab[i].id, itpdata->ctab[i].state); */
        if (itpdata->ctab[i].state == C_RUN)
        {
            return &itpdata->ctab[i];
        }
    }

    return ctx;
}



/*
   the big main central interpreter loop.
   processes return codes from eval().
   The answer is one of XPOST_MAINLOOP_DONE, _YIELDED or _INVALID.
   yieldtocaller indicates `showpage` has been called using SHOWPAGE_RETURN semantics.
   ioblock indicates a blocked io operation.
   contextswitch indicates the `yield` operator has been called.
   all other values indicate an error condition to be returned to postscript.
 */
int mainloop(Xpost_Context *ctx)
{
    int ret;
    unsigned int evalcount = 0;

ctxswitch:
    xpost_ctx = ctx = _switch_context(ctx);
    itpdata->cid = ctx->id;

    /* the context's memory pointers are fixed for the life of a run;
       validate them once when a context becomes current rather than
       before every evaluation step */
    if (!validate_context(ctx))
        return XPOST_MAINLOOP_INVALID;

    while(!ctx->quit)
    {
        /* safe point: between evaluation steps every live object is
           reachable from the stacks, so a requested collection cannot
           sweep an operator's C-held intermediates.

           A collection that cannot mark its roots returns before its
           sweep, so it reclaims nothing, and the next one refuses in the
           same place: reclamation is over for the rest of the run. The
           run is told so here. PLRM 8.2 gives VMerror for an error in
           the virtual memory machinery, naming an internal error in the
           interpreter among its causes, and a marker that cannot read
           the root set is one. Carrying on instead spends fresh entity
           numbers until they run out, and reports that against whichever
           operator was allocating at the time. */
        if (ctx->lo->garbage_collect_pending)
        {
            ctx->lo->garbage_collect_pending = 0;
            if (ctx->lo->garbage_collect_is_installed
                && ctx->lo->garbage_collect(ctx->lo, 1, 1) < 0)
            {
                XPOST_LOG_ERR("collection abandoned before its sweep");
                _onerror(ctx, VMerror);
                continue;
            }
        }
        /* a push the stack would not take, made somewhere other than
           inside an operator -- the dispatch answers for those itself.
           The object is on no stack and the step that pushed it carried
           on as though it were, so the run is told before the next step
           reads a stack it is not the depth of. */
        if (ctx->lo->push_refused)
        {
            ctx->lo->push_refused = 0;
            XPOST_LOG_ERR("a stack would not take a pushed object");
            _onerror(ctx, VMerror);
            continue;
        }
        if (_interrupt_pending)
        {
            /* an external interrupt request lands between operations */
            _interrupt_pending = 0;
            _onerror(ctx, interrupt);
            continue;
        }
        if ((++evalcount & 1023) == 0)
        {
            int over = _stack_ceilings(ctx);
            if (over)
            {
                _onerror(ctx, over);
                continue;
            }
        }
        ret = eval(ctx);
        if (ret)
            switch (ret)
            {
            case yieldtocaller:
                return XPOST_MAINLOOP_YIELDED;
            case ioblock:
                ctx->state = C_IOBLOCK; /* fallthrough */
            case contextswitch:
                goto ctxswitch;
            default:
                _onerror(ctx, ret);
            }
    }

    return XPOST_MAINLOOP_DONE;
}

/* How deep a program may drive the nesting. Each level holds a C frame
   of this function and of whichever operator called in, so the bound is
   the C stack's, not the execution stack's, and it is set well below
   what the smallest stack the interpreter is built for can carry. */
#define XPOST_NEST_MAX 64

/* the error a procedure run from inside an operator failed with, named
   by the errorname the error machinery recorded */
static unsigned int _nested_error(Xpost_Context *ctx)
{
    Xpost_Object sd, ed, en;
    char *nm;
    unsigned int i;
    unsigned int ret = ioerror;

    sd = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);
    ed = xpost_dict_get(ctx, sd, xpost_name_cons(ctx, "$error"));
    if (xpost_object_get_type(ed) != dicttype)
        return ret;
    en = xpost_dict_get(ctx, ed, xpost_name_cons(ctx, "errorname"));
    if (xpost_object_get_type(en) != nametype)
        return ret;
    nm = xpost_string_allocate_cstring(ctx, xpost_name_get_string(ctx, en));
    if (!nm)
        return ret;
    for (i = 0; i <= (unsigned int)unknownerror; i++)
        if (strcmp(nm, errorname[i]) == 0)
        {
            ret = i;
            break;
        }
    free(nm);
    return ret;
}

int xpost_interpreter_run_nested(Xpost_Context *ctx, Xpost_Object P)
{
    int base;
    Xpost_Object saved;
    Xpost_Object caught;
    Xpost_Object held[XPOST_OPERATOR_MAX_SIG];
    int held_n;
    int i;
    int ret = 0;

    if (ctx->nest_depth >= XPOST_NEST_MAX)
        return execstackoverflow;

    base = xpost_stack_count(ctx->lo, ctx->es);

    /* The procedure runs under two frames the ordinary machinery reads.

       The boundary marks where a failure belongs: above it the failure
       is the procedure's, and an error restores the operand stack to
       what the procedure found (PLRM 3.11.1).

       The false beneath it is a stopped context, the same frame the
       stopped operator leaves. It bounds the unwinding of an error the
       procedure does not catch to this call, while the operator that
       made the call is still on the C stack with its own answer to
       push. The failure reaches the program as that operator's. */
    if (!xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, calloutdone)))
        return execstackoverflow;
    if (!xpost_stack_push(ctx->lo, ctx->es, xpost_bool_cons(0)))
    {
        (void)xpost_stack_pop(ctx->lo, ctx->es);
        return execstackoverflow;
    }
    if (!xpost_stack_push(ctx->lo, ctx->es, P))
    {
        (void)xpost_stack_pop(ctx->lo, ctx->es);
        (void)xpost_stack_pop(ctx->lo, ctx->es);
        return execstackoverflow;
    }

    /* The hold stack holds the operands of the operator that called in,
       for the error machinery to give back if that operator fails. The
       procedure about to run reaches operators of its own, and each of
       them loads the hold stack with its own operands, so the caller's
       would be gone by the time they were wanted. They are kept here
       and put back, rather than the caller being told they are lost:
       the operator that called in is exactly the one an error in the
       procedure is going to be raised against. */
    {
        Xpost_Stack *h = xpost_stack_at(ctx->lo, ctx->hold);
        held_n = (int)h->top;
        if (held_n > (int)(sizeof held / sizeof *held))
            held_n = (int)(sizeof held / sizeof *held);
        for (i = 0; i < held_n; i++)
            held[i] = h->data[i];
    }

    saved = ctx->currentobject;
    ++ctx->nest_depth;
    while (xpost_stack_count(ctx->lo, ctx->es) > base && !ctx->quit)
    {
        if (ctx->lo->garbage_collect_pending)
        {
            ctx->lo->garbage_collect_pending = 0;
            if (ctx->lo->garbage_collect_is_installed
                && ctx->lo->garbage_collect(ctx->lo, 1, 1) < 0)
            {
                XPOST_LOG_ERR("collection abandoned before its sweep");
                _onerror(ctx, VMerror);
                continue;
            }
        }
        ret = eval(ctx);
        if (ret)
        {
            /* the run itself is ending, or moving to another context:
               neither is this procedure's to answer, so it goes back to
               the interpreter that can */
            if (ret == yieldtocaller || ret == contextswitch
                || ret == ioblock)
                break;
            _onerror(ctx, ret);
            ret = 0;
        }
    }
    --ctx->nest_depth;
    ctx->currentobject = saved;
    {
        Xpost_Stack *h = xpost_stack_at(ctx->lo, ctx->hold);
        h->prevseg = ctx->hold;
        for (i = 0; i < held_n; i++)
            h->data[i] = held[i];
        h->top = (unsigned int)held_n;
    }

    if (ret || ctx->quit)
    {
        /* the run was cut short with the frames still standing */
        while (xpost_stack_count(ctx->lo, ctx->es) > base)
            (void)xpost_stack_pop(ctx->lo, ctx->es);
        return ret;
    }

    /* the stopped context has answered by now, whether the procedure
       ran to its end or an error unwound to it */
    caught = xpost_stack_pop(ctx->lo, ctx->os);
    if (xpost_object_get_type(caught) != booleantype)
        return ioerror;
    if (caught.int_.val)
        return (int)_nested_error(ctx);
    return 0;
}





/*
   string constructor helper for literals
   sizeof("") is 1, ie. it includes the terminating \0 byte.
   our ps strings are counted and do not need (and should not have)
   a nul byte, or this byte may produce garbage output when printed.
 */
#define CNT_STR(s) sizeof(s) - 1, s

/*
   set global pagesize,
   initialize eval's jump-tabl
   allocate global itpdata interpreter instance
   call xpost_interpreter_init
        which initializes the first context
 */
static
int initalldata(const char *device)
{
    int ret;

    initevaltype();
    xpost_object_install_dict_get_access(xpost_dict_get_access);
    xpost_object_install_dict_set_access(xpost_dict_set_access);

    /* allocate the top-level itpdata data structure. */
    null = xpost_object_cvlit(null);
    itpdata = malloc(sizeof*itpdata);
    if (!itpdata)
    {
        XPOST_LOG_ERR("itpdata=malloc failed");
        return 0;
    }
    memset(itpdata, 0, sizeof*itpdata);

    /* allocate and initialize the first context structure
       and associated memory structures.
       populate OPTAB and systemdict with operators.
       push systemdict, globaldict, and userdict on dict stack
     */
    ret = xpost_interpreter_init(itpdata, device);
    if (!ret)
    {
        return 0;
    }

    /* set global shortcut to context_0
       (the only context in a single-threaded interpreter)
       TODO remove this variable
     */
    xpost_ctx = &itpdata->ctab[0];

    return 1;
}

/* FIXME remove duplication of effort here and in bin/xpost_main.c
         (ie. there should be 1 table, not 2)

    Generates postscript code to initialize the selected device

    currentglobal false setglobal              % allocate in local memory
    device_requires_loading? { loadXXXdevice } if  % load if necessary
    userdict /DEVICE 612 792 newXXXdevice put  % instantiate the device
    setglobal                                  % reset previous allocation mode

    initialization of the device is deferred until the start procedure has
    initialized graphics (importantly, the ppmimage base class).
    the loadXXXdevice operators all specialize the ppmimage base class
    and so must wait until it is available.

    also creates the definitions PACKAGE_DATA_DIR PACKAGE_INSTALL_DIR and EXE_DIR
 */
static
int setlocalconfig(Xpost_Context *ctx,
                   Xpost_Object sd,
                   const char *device,
                   const char *outfile,
                   const char *bufferin,
                   char **bufferout,
                   Xpost_Showpage_Semantics semantics,
                   Xpost_Set_Size set_size,
                   int width,
                   int height)
{
    const char *device_strings[][3] =
    {
        { "pgm",     "",                 "newPGMIMAGEdevice" },
        { "ppm",     "",                 "newPPMIMAGEdevice" },
        { "pbm",     "",                 "newPBMIMAGEdevice" },
        { "tiff",    "",                 "newTIFFIMAGEdevice" },
        { "null",    "",                 "newnulldevice"     },
        { "bbox",    "",                 "newbboxdevice"     },
        { "xcb",     "loadxcbdevice",    "newxcbdevice"      },
        { "gdi",     "loadwin32device",  "newwin32device"    },
        { "gl",      "loadwin32device",  "newwin32device"    },
        { "bgr",     "loadbgrdevice",    "newbgrdevice"      },
        { "raster",  "loadrasterdevice", "newrasterdevice"   },
        { "pdfwrite","",                 "newPDFWRITEdevice" },
        { "svgwrite","",                 "newSVGWRITEdevice" },
        { "dscwrite", "",                 "newDSCWRITEdevice"  },
        { "png",     "loadpngdevice",    "newpngdevice"      },
        { "pngalpha", "loadpngalphadevice", "newpngalphadevice" },
        { "jpeg",    "loadjpegdevice",   "newjpegdevice"      },
        { NULL, NULL, NULL }
    };
    /* The maker is looked for in privatedict first, where the makers
       written in PostScript live, and in systemdict otherwise, where the
       ones a C driver registers live. That way neither has to be visible
       to a program for this to reach it. */
    const char *strtemplate = "currentglobal false setglobal "
                        "%s graphicsdict /currgstate get /device %s "
                        ".privatedict /%s 2 copy known "
                        "{ get }{ pop pop /%s load } ifelse exec put "
                        "graphicsdict /.outputdevice /%s put "
                        "setglobal";
    Xpost_Object namenewdev;
    Xpost_Object newdevstr;
    int i;
    char *devstr;
    char *subdevice;
    char *dimensions;
    char dimensions_buf[48]; /* holds "%d %d" for any int width/height */
    int ret;

    ctx->vmmode = GLOBAL;

#ifdef _WIN32
    ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "WIN32"), xpost_bool_cons(1));
    if (ret)
        return ret;
#endif

    devstr = strdup(device); /*  Parse device string for mode selector "dev:mode" */
    if ((subdevice=strchr(devstr, ':'))) {
        *subdevice++ = '\0';
        ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "SUBDEVICE"),
                             xpost_object_cvlit(xpost_string_cons(ctx,
                                     strlen(subdevice), subdevice)));
        if (ret)
            goto done;
    }

    /* define the /newdefaultdevice name called by /start */
    for (i = 0; device_strings[i][0]; i++)
    {
        if (strcmp(devstr, device_strings[i][0]) == 0)
        {
            break;
        }
    }
    if (!device_strings[i][0])
    {
        XPOST_LOG_ERR("unknown device %s", devstr);
        ret = undefined;
        goto done;
    }
    if (set_size == XPOST_USE_SIZE){
        snprintf(dimensions_buf, sizeof(dimensions_buf), "%d %d", width, height);
        dimensions = dimensions_buf;
    } else {
        static char x[] = "612 792";
        dimensions = x;
    }
    newdevstr = xpost_string_cons(ctx,
                                  strlen(strtemplate) - 10  /* five %s */
                                  + strlen(device_strings[i][1])
                                  + strlen(dimensions)
                                  + 2 * strlen(device_strings[i][2])
                                  + strlen(device_strings[i][0]) + 1,
                                  NULL);
    sprintf(xpost_string_get_pointer(ctx, newdevstr), strtemplate,
            device_strings[i][1], dimensions, device_strings[i][2],
            device_strings[i][2], device_strings[i][0]);
    --newdevstr.comp_.sz; /* trim the '\0' */

    namenewdev = xpost_name_cons(ctx, "newdefaultdevice");
    ret = xpost_dict_put(ctx, sd, namenewdev, xpost_object_cvx(newdevstr));
    if (ret)
        goto done;

    ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "ShowpageSemantics"),
                         xpost_int_cons(semantics));
    if (ret)
        goto done;

    if (outfile)
    {
        ret = xpost_dict_put(ctx, sd,
                             xpost_name_cons(ctx, "OutputFileName"),
                             xpost_object_cvlit(xpost_string_cons(ctx,
                                     strlen(outfile), outfile)));
        if (ret)
            goto done;
    }

    if (bufferin)
    {
        Xpost_Object s = xpost_object_cvlit(xpost_string_cons(ctx, sizeof(bufferin), NULL));
        xpost_object_set_access(ctx, s, XPOST_OBJECT_TAG_ACCESS_NONE);
        memcpy(xpost_string_get_pointer(ctx, s), &bufferin, sizeof(bufferin));
        ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "OutputBufferIn"), s);
        if (ret)
            goto done;
    }

    if (bufferout)
    {
        Xpost_Object s = xpost_object_cvlit(xpost_string_cons(ctx, sizeof(bufferout), NULL));
        xpost_object_set_access(ctx, s, XPOST_OBJECT_TAG_ACCESS_NONE);
        memcpy(xpost_string_get_pointer(ctx, s), &bufferout, sizeof(bufferout));
        ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "OutputBufferOut"), s);
        if (ret)
            goto done;
    }

    ret = 0;
done:
    ctx->vmmode = LOCAL;
    free(devstr);
    return ret;
}

/*
   load init.ps (which also loads err.ps) while systemdict is writeable
   ignore invalidaccess errors.
 */
static
void loadinitps(Xpost_Context *ctx)
{
    char buf[1024];
    char path_init_ps[XPOST_PATH_MAX];
    struct stat statbuf;
    char *path_init;
    char *path;
    int n;

    assert(ctx->gl->base);
    xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, quit));

#define XPOST_PATH_INIT \
    do \
    { \
        snprintf(path_init_ps, sizeof(path_init_ps), "%s/init.ps", path); \
        if (stat(path_init_ps, &statbuf) == 0) \
        { \
            path_init = path; \
            goto load_init_ps; \
        } \
        else \
            XPOST_LOG_DBG("init.ps not present in", path_init_ps); \
    } while (0)

    /* environment variable XPOST_DATA_DIR */
    if ((path = getenv("XPOST_DATA_DIR")))
        XPOST_PATH_INIT;

    /* directory of the shared library (absent for an uninstalled build) */
    path = (char *)xpost_data_dir_get();
    if (path)
        XPOST_PATH_INIT;

#ifdef PACKAGE_DATA_DIR
    {
        static char x[] = PACKAGE_DATA_DIR;
        path = x;
    }
    XPOST_PATH_INIT;
#endif

    {
        static char x[] = "data";
        path = x;
    }
    XPOST_PATH_INIT;

    {
        static char x[] = "../data";
        path = x;
    }
    XPOST_PATH_INIT;

    {
        static char x[] = "../../data";
        path = x;
    }
    XPOST_PATH_INIT;

    XPOST_LOG_ERR("init.ps can not be found");

    return;

  load_init_ps:
    /* init.ps loads now and graphics.ps loads lazily from this same directory;
       permit reading it so a later sandbox does not deny the interpreter its
       own start-up files */
    xpost_path_permit_read(path_init);

    /* backslashes are not supported in path because they are inserted in
    * PostScript files, and PostScript */
#ifdef _WIN32
    path = path_init_ps;
    while (*path++) if (*path == '\\') *path = '/';
    path = path_init;
    while (*path++) if (*path == '\\') *path = '/';
#endif
    n = snprintf(buf, sizeof(buf),
                 "(%s) (r) file cvx "
                 "/DATA_DIR (%s) def exec ", path_init_ps, path_init);
    xpost_stack_push(ctx->lo, ctx->es,
                     xpost_object_cvx(xpost_string_cons(ctx, n, buf)));

    ctx->quit = 0;
    /* mainloop answers that it is done, that it yielded, or that the
       context did not validate. The context was built and every part of
       it checked before this is reached, which is what the assertion
       above reads; and a yield is the returntocaller operator's answer,
       which only showpage under return semantics reaches and init.ps
       paints no page. Running the start-up file leaves done the only
       answer. */
    mainloop(ctx);
}


/* Name the standard local dictionaries in systemdict. systemdict is global, so
   holding a reference to a local dictionary would be an invalidaccess; the PLRM
   sanctions exactly this exception (section 3.7.2), naming userdict, errordict,
   $error and FontDirectory in systemdict so a program reaches each by name. The
   ignoreinvalidaccess window is isolated to these puts; the rest of the
   interpreter, initialisation included, obeys the local/global rule. */
static int copyudtosd(Xpost_Context *ctx, Xpost_Object ud, Xpost_Object sd)
{
    Xpost_Object ed, de, fd, st, sv;
    int ret;

    ctx->ignoreinvalidaccess = 1;
    ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "userdict"), ud);
    if (ret)
        goto done;
    ed = xpost_dict_get(ctx, ud, xpost_name_cons(ctx, "errordict"));
    if (xpost_object_get_type(ed) == invalidtype)
    {
        ret = undefined;
        goto done;
    }
    ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "errordict"), ed);
    if (ret)
        goto done;
    de = xpost_dict_get(ctx, ud, xpost_name_cons(ctx, "$error"));
    if (xpost_object_get_type(de) == invalidtype)
    {
        ret = undefined;
        goto done;
    }
    ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "$error"), de);
    if (ret)
        goto done;
    /* FontDirectory is likewise a name in systemdict for a local dictionary
       (PLRM). It exists in userdict by the time this runs. */
    fd = xpost_dict_get(ctx, ud, xpost_name_cons(ctx, "FontDirectory"));
    if (xpost_object_get_type(fd) == dicttype)
    {
        ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "FontDirectory"), fd);
        if (ret)
            goto done;
        /* GlobalFontDirectory and its older name SharedFontDirectory are a
           different, global dictionary holding only the fonts defined while
           the allocation mode was global; the boot file defines them into
           systemdict itself, being global and so permitted to. Keep both
           directories to hand: the name FontDirectory is rebound to one or
           the other as the allocation mode changes (PLRM), and setglobal
           does that without having to look either up. */
        ctx->localfontdir = fd;
        ctx->globalfontdir = xpost_dict_get(ctx, sd,
                                 xpost_name_cons(ctx, "GlobalFontDirectory"));
    }
    /* statusdict and serverdict are local dictionaries a program mutates, so
       save/restore isolates a job's changes; systemdict names them (PLRM). */
    st = xpost_dict_get(ctx, ud, xpost_name_cons(ctx, "statusdict"));
    if (xpost_object_get_type(st) == dicttype)
    {
        ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "statusdict"), st);
        if (ret)
            goto done;
    }
    sv = xpost_dict_get(ctx, ud, xpost_name_cons(ctx, "serverdict"));
    if (xpost_object_get_type(sv) == dicttype)
    {
        ret = xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "serverdict"), sv);
        if (ret)
            goto done;
    }
    ret = 0;
    /* the window in which a global dictionary may name a local one closes
       here, on every path out */
done:
    ctx->ignoreinvalidaccess = 0;
    return ret;
}


/*
   create an executable context using the given device,
   output configuration, and semantics.
 */
XPAPI Xpost_Context *xpost_create(const char *device,
                                  Xpost_Output_Type output_type,
                                  const void *outputptr,
                                  Xpost_Showpage_Semantics semantics,
                                  Xpost_Output_Message output_msg,
                                  Xpost_Set_Size set_size,
                                  int width,
                                  int height)
{
    Xpost_Object sd, ud;
    int ret;
    const char *outfile = NULL;
    const char *bufferin = NULL;
    char **bufferout = NULL;
    int quiet;

    /* One interpreter instance at a time. itpdata is the whole dynamic
       state -- the context table, both memory-file tables and the
       identity of the running context -- so a second instance would take
       the place of the live one, leaving every handle already handed out
       pointing into memory the interpreter no longer reaches. The
       multiple contexts the interpreter is built around live in this one
       instance's context table; they are not had by creating another. */
    if (itpdata)
    {
        XPOST_LOG_ERR("an interpreter instance is already live");
        return NULL;
    }

    switch (output_msg)
    {
        case XPOST_OUTPUT_MESSAGE_QUIET:
            quiet = 1;
            _xpost_interpreter_is_tracing = 0;
            break;
        case XPOST_OUTPUT_MESSAGE_VERBOSE:
            quiet = 0;
            _xpost_interpreter_is_tracing = 0;
            break;
        case XPOST_OUTPUT_MESSAGE_TRACING:
            quiet = 0;
            _xpost_interpreter_is_tracing = 1;
            break;
        default:
            XPOST_LOG_ERR("Wrong output message value");
            return NULL;;
    }


    switch (output_type)
    {
        case XPOST_OUTPUT_FILENAME:
            outfile = outputptr;
            break;
        case XPOST_OUTPUT_BUFFERIN:
            bufferin = outputptr;
            break;
        case XPOST_OUTPUT_BUFFEROUT:
            bufferout = (char **)outputptr;
            break;
        case XPOST_OUTPUT_DEFAULT:
            break;
    }

#if 0
    test_memory();
    if (!test_garbage_collect(xpost_interpreter_cid_init,
                              xpost_interpreter_cid_get_context,
                              xpost_interpreter_get_initializing,
                              xpost_interpreter_set_initializing,
                              xpost_interpreter_alloc_local_memory,
                              xpost_interpreter_alloc_global_memory))
        return NULL;
#endif

    nextid = 0; /*reset process counter */

    /* Allocate and initialize all interpreter data structures. */
    ret = initalldata(device);
    if (!ret)
    {
        return NULL;
    }

    /* extract systemdict and userdict for additional definitions */
    sd = xpost_stack_bottomup_fetch(xpost_ctx->lo, xpost_ctx->ds, 0);
    ud = xpost_stack_bottomup_fetch(xpost_ctx->lo, xpost_ctx->ds, 2);

    ret = setlocalconfig(xpost_ctx, sd,
                         device, outfile, bufferin, bufferout,
                         semantics, set_size, width, height);
    if (ret)
    {
        XPOST_LOG_ERR("%s recording the interpreter's configuration",
                      errorname[ret]);
        return NULL;
    }

    xpost_ctx->quiet = quiet;
    if (quiet)
    {
        /* Hand the quiet flag to the boot code through systemdict -- the only
           dictionary that exists this early. init.ps relocates QUIET into the
           private .internaldict as soon as that dictionary is built, so the
           load-time banner guards read it through a frozen reference and a
           program can neither see nor shadow it. */
        ret = xpost_dict_put(xpost_ctx,
                             sd /*xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0)*/ ,
                             xpost_name_cons(xpost_ctx, "QUIET"),
                             xpost_bool_cons(1));
        if (ret)
        {
            XPOST_LOG_ERR("%s naming QUIET in systemdict", errorname[ret]);
            return NULL;
        }
    }

    xpost_stack_clear(xpost_ctx->lo, xpost_ctx->hold);
    xpost_interpreter_set_initializing(0);
    loadinitps(xpost_ctx);

    ret = copyudtosd(xpost_ctx, ud, sd);
    if (ret)
    {
        XPOST_LOG_ERR("%s error in copyudtosd", errorname[ret]);
        return NULL;
    }

    /* make systemdict readonly FIXME: use new access semantics */
    ret = xpost_dict_put(xpost_ctx, sd, xpost_name_cons(xpost_ctx, "systemdict"), sd);
    if (ret)
    {
        XPOST_LOG_ERR("%s naming systemdict in itself", errorname[ret]);
        return NULL;
    }
    xpost_object_set_access(xpost_ctx, sd, XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
#if 0
    if (!xpost_stack_bottomup_replace(xpost_ctx->lo, xpost_ctx->ds, 0, xpost_object_set_access(xpost_ctx, sd, XPOST_OBJECT_TAG_ACCESS_READ_ONLY)))
    {
        XPOST_LOG_ERR("cannot replace systemdict in dict stack");
        return NULL;
    }
#endif

    xpost_interpreter_set_initializing(0);

    /* a push the stacks would not take, made while the interpreter was
       being brought up. The safe points that read this record are inside
       the loop a program runs in, and nothing has run one yet, so a
       refusal recorded here would keep until the first program and be
       reported against it. That program's stacks are short by an object
       it never pushed, and the context it was handed is one whose
       startup did not finish. Both memory files are read: names intern
       into global VM, so a refusal is recordable on either. */
    if (xpost_ctx->lo->push_refused || xpost_ctx->gl->push_refused)
    {
        XPOST_LOG_ERR("a stack would not take a pushed object while the "
                      "interpreter was starting");
        return NULL;
    }

    return xpost_ctx;
}

/* Scan one PostScript token out of str. The token operator answers a
   flag beneath its results, and reads that flag only where the operator
   ran to its end: a scan the operator refused -- text that stops inside
   a literal is the reachable one -- pushes nothing at all, and what lies
   under a refusal belongs to whatever put it there. Answers the scan's
   own refusal, 0 with the token in *out otherwise, and 0 with a null
   there for text that held no token. */
static
int get_token(Xpost_Context *ctx, char *str, Xpost_Object *out)
{
    Xpost_Object s = xpost_string_cons(ctx, strlen(str), str);
    int ret;

    if (xpost_object_get_type(s) != stringtype)
        return VMerror;
    if (!xpost_stack_push(ctx->lo, ctx->os, s))
        return stackoverflow;
    ret = xpost_operator_exec(ctx, XPOST_OP_CODE(ctx, token));
    if (ret)
        return ret;
    if (xpost_stack_pop(ctx->lo, ctx->os).int_.val){
        *out = xpost_stack_pop(ctx->lo, ctx->os);
        xpost_stack_pop(ctx->lo, ctx->os);
    } else {
        *out = null;
    }
    return 0;
}

XPAPI int xpost_add_definitions(Xpost_Context *ctx, int cnt, char *defs[])
{
    int i;
    Xpost_Object ud;

    if (!ctx) return 0;
    XPOST_LOG_INFO("adding %d defs", cnt);

    ud = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    for (i = 0; i < cnt; i++)
    {
        char *eq = strchr(defs[i], '=');

        XPOST_LOG_INFO("%s", defs[i]);
        if (eq)
        {
            Xpost_Object tok;
            int ret;

            *eq++ = '\0';
            ret = get_token(ctx, eq, &tok);
            if (ret)
            {
                XPOST_LOG_ERR("%s scanning the value of %s",
                              errorname[ret], defs[i]);
                eq[-1] = '=';
                return 0;
            }
            ret = xpost_dict_put(ctx, ud, xpost_name_cons(ctx, defs[i]), tok);
            eq[-1] = '=';
            if (ret)
                return 0;
        }
        else
        {
            if (xpost_dict_put(ctx, ud,
                        xpost_name_cons(ctx, defs[i]),
                        null))
                return 0;
        }
    }
    return 1;
}

XPAPI int xpost_add_resource_dir(Xpost_Context *ctx, const char *dir)
{
    Xpost_Object ud;
    Xpost_Object key;
    Xpost_Object rp;
    Xpost_Object newrp;
    Xpost_Object str;
    unsigned int n;
    unsigned int i;
    unsigned int vmmode;

    if (!ctx || !dir)
        return 0;

    key = xpost_name_cons(ctx, ".resourcepath");
    ud = ctx->privatedict;

    /* extend any array already in privatedict, else start empty */
    rp = xpost_dict_get(ctx, ud, key);
    n = (xpost_object_get_type(rp) == arraytype) ? rp.comp_.sz : 0;

    /* the resource path persists across restore, so its array and strings live
       in global VM even though privatedict is local (a local dictionary may
       hold a global object). They are data, not a procedure, so make them
       literal -- an executable array would be run, not read, when the path is
       evaluated. */
    vmmode = ctx->vmmode;
    ctx->vmmode = GLOBAL;
    str = xpost_object_cvlit(xpost_string_cons(ctx, (unsigned int)strlen(dir),
                                               (char *)dir));
    newrp = xpost_object_cvlit(xpost_array_cons(ctx, n + 1));
    if (xpost_object_get_type(str) == invalidtype ||
        xpost_object_get_type(newrp) == invalidtype)
    {
        ctx->vmmode = vmmode;
        return 0;
    }
    for (i = 0; i < n; i++)
        if (xpost_array_put(ctx, newrp, i, xpost_array_get(ctx, rp, i)) != 0)
        {
            ctx->vmmode = vmmode;
            return 0;
        }
    if (xpost_array_put(ctx, newrp, n, str) != 0)
    {
        ctx->vmmode = vmmode;
        return 0;
    }
    ctx->vmmode = vmmode;

    if (xpost_dict_put(ctx, ud, key, newrp))
        return 0;
    return 1;
}

XPAPI const char *xpost_error_name_get(Xpost_Context *ctx)
{
    return ctx->run_uncaught ? ctx->run_error_name : "";
}

XPAPI const char *xpost_error_info_get(Xpost_Context *ctx)
{
    return ctx->run_uncaught ? ctx->run_error_info : "";
}

/*
   execute ps program until quit, fall-through to quit,
   SHOWPAGE_RETURN semantic, or error (default action: message, purge and quit).
 */
/* The start procedures live in privatedict, off the dict stack, so a program
   cannot name them. Fetch the one named and push it, executable, onto the exec
   stack to prime the run. */
static void push_start_proc(Xpost_Context *ctx, const char *name)
{
    xpost_stack_push(ctx->lo, ctx->es,
        xpost_object_cvx(xpost_dict_get(ctx, ctx->privatedict,
                                        xpost_name_cons(ctx, name))));
}

/* What showpage does at the end of a page, as the context was created
   with. Read from systemdict, which is what device.ps reads it from and
   what the run is measured against when it ends. */
static int _showpage_semantic(Xpost_Context *ctx)
{
    Xpost_Object semantic = xpost_dict_get(ctx,
                  xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0),
                  xpost_name_cons(ctx, "ShowpageSemantics"));

    return semantic.int_.val;
}

/* Load the language into the context, so that a bracket taken after
   this point encloses a program and nothing else.

   The load runs once in the life of a context. What makes it idempotent
   -- the latch the start procedures read -- is virtual memory, and what
   it leaves behind is not: the operator table it fills is outside the
   memory files, and the window it opens on systemdict closes on a
   one-shot held in the context. A bracket taken over the load therefore
   rewinds the half that is in virtual memory and leaves the half that is
   not, and the job after it finds neither a language nor a context that
   can load one again.

   Whether the load succeeded is read from the context afterwards rather
   than from this run: the procedure reports nothing, and the start
   procedure the caller's own run begins with meets the same failure and
   reports it there. */
static void _load_language(Xpost_Context *ctx)
{
    unsigned int base = xpost_stack_count(ctx->lo, ctx->es);

    xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, quit));
    push_start_proc(ctx, ctx->skip_graphics ? "loadlanguagenographics"
                                            : "loadlanguage");
    ctx->quit = 0;
    ctx->state = C_RUN;
    /* whether the language stands is read from the context afterwards,
       which is the answer the caller acts on; this one says only how
       the load's own run ended */
    if (mainloop(ctx) != XPOST_MAINLOOP_DONE)
        XPOST_LOG_ERR("the language load did not run to its end");

    while (xpost_stack_count(ctx->lo, ctx->es) > (int)base)
        xpost_stack_pop(ctx->lo, ctx->es);
}

/* Rewind virtual memory to the snapshots this call took at its start.
   Only to a snapshot it took: a save level is the substack its records
   go on, and that substack is an allocation the memory file can refuse;
   the refusal is answered with a null, and a run that pushed no level
   and rewinds anyway pops whichever level it finds -- one belonging to
   whatever put it there, whose records are then played back over VM that
   has moved on since. The local snapshot is read the same way. */
static void _rewind_job_snapshots(Xpost_Context *ctx,
                                  Xpost_Object gsav,
                                  Xpost_Object lsav)
{
    if (xpost_object_get_type(gsav) == savetype)
        xpost_save_restore_snapshot(ctx->gl);
    if (xpost_object_get_type(lsav) == savetype)
    {
        unsigned int vs = xpost_memory_save_stack_adr(ctx->lo);
        integer llev;

        /* the depth is counted, the level recorded: comparing them in
           the wider signed type keeps a depth that came back short of
           the level below it rather than above it */
        for ( llev = xpost_stack_count(ctx->lo, vs);
                llev > (integer)lsav.save_.lev;
                llev-- )
        {
            xpost_save_restore_snapshot(ctx->lo);
        }
    }
}

/* Close the file a run wrapped around the program it was given. A run
   that reads its program to the end closes it there; one that stops
   before the end -- at its quit operator, or on an error that unwinds
   past every stopped context -- would otherwise leave it open, and the
   file a program arrives in as a string is one this run made itself. */
static void _close_run_input(Xpost_Context *ctx)
{
    if (xpost_object_get_type(ctx->run_input_file) == filetype)
    {
        (void) xpost_file_object_close(ctx->lo, ctx->run_input_file);
        ctx->run_input_file = null;
    }
}

XPAPI Xpost_Run_Status xpost_run(Xpost_Context *ctx, Xpost_Input_Type input_type, const void *inputptr, size_t set_size)
{
    Xpost_Object gsav = null;
    Xpost_Object lsav = null;
    const char *ps_str = NULL;
    const char *ps_file = NULL;
    const FILE *ps_file_ptr = NULL;
    int ret;
    Xpost_Object device;

    switch(input_type)
    {
        case XPOST_INPUT_FILENAME:
            ps_file = inputptr;
            break;
        case XPOST_INPUT_STRING:
            ps_str = inputptr;
            ps_file_ptr = tmpfile();
            if (ps_file_ptr == NULL)
            {
                XPOST_LOG_ERR("cannot create temporary file for program");
                return XPOST_RUN_FAILED;
            }
            if (set_size)
                fwrite(ps_str, 1, set_size, (FILE*)ps_file_ptr);
            else
                fwrite(ps_str, 1, strlen(ps_str), (FILE*)ps_file_ptr);
            rewind((FILE*)ps_file_ptr);
            break;
        case XPOST_INPUT_FILEPTR:
            ps_file_ptr = inputptr;
            break;
        case XPOST_INPUT_RESUME: /* resuming a returned session, skip startup */
            /* the resumed run restarts the cascade count and error record */
            ctx->onerr_run = 0;
            ctx->run_error_name[0] = '\0';
            ctx->run_error_info[0] = '\0';
            ctx->run_uncaught = 0;
            goto run;
    }

    /* a fresh run starts with a clean cascade count and error record */
    ctx->onerr_run = 0;
    ctx->run_error_name[0] = '\0';
    ctx->run_error_info[0] = '\0';
    ctx->run_uncaught = 0;

    /* prime the exec stack
       so it starts with a 'start*' procedure,
       and if it ever gets to the bottom, it quits.
       These procedures are all defined in data/init.ps
     */
    ctx->es_run_base = xpost_stack_count(ctx->lo, ctx->es);
    xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, quit));
    /*
       if ps_file is NULL:
         if stdin is a tty
           `start` proc defined in init.ps runs `executive` which prompts for user input
         else
           'startstdin' executes stdin but does not prompt

       if ps_file is not NULL:
       'startfile' executes a named file wrapped in a stopped context with handleerror
    */
    /* with skip_graphics set, dispatch to the no-graphics start procedures,
       which run the interpreter lockdown without loading the graphics modules.
       The interactive (tty) session always loads graphics. */
    if (ps_file)
    {
        /*printf("ps_file\n"); */
        xpost_stack_push(ctx->lo, ctx->os, xpost_object_cvlit(xpost_string_cons(ctx, strlen(ps_file), ps_file)));
        push_start_proc(ctx, ctx->skip_graphics ? "startfilenamenographics" : "startfilename");
    }
    else if (ps_file_ptr)
    {
        /* the stream the embedding caller handed over is the program to
           run, so the file object over it carries the attribute of a
           stream opened to be read: the start procedure executes it, and
           execution reads it */
        ctx->run_input_file =
            xpost_object_cvlit(xpost_file_cons(ctx->lo, ps_file_ptr, 1));
        if (xpost_object_get_type(ctx->run_input_file) == filetype)
        {
            ctx->run_input_file.tag &= ~XPOST_OBJECT_TAG_DATA_FLAG_ACCESS_MASK;
            ctx->run_input_file.tag |=
                XPOST_OBJECT_TAG_ACCESS_FILE_READ
                << XPOST_OBJECT_TAG_DATA_FLAG_ACCESS_OFFSET;
        }
        xpost_stack_push(ctx->lo, ctx->os, ctx->run_input_file);
        push_start_proc(ctx, ctx->skip_graphics ? "startfilenographics" : "startfile");
    }
    else
    {
        if (xpost_isatty(fileno(stdin)))
            push_start_proc(ctx, "start");
        else
            push_start_proc(ctx, ctx->skip_graphics ? "startstdinnographics" : "startstdin");
    }

    /* The bracket is this call's: it is taken here and rewound before
       this call returns. A job under XPOST_SHOWPAGE_RETURN is not one
       call -- it hands control back at each showpage and ends on a later
       call, which is where its virtual memory stops changing -- so no
       bracket is taken over it, rather than a save level being pushed
       that this call has nothing to do with.

       What the bracket encloses is the program. The language is loaded
       before it is taken, so that rewinding to it rewinds what the job
       wrote rather than what the context is made of; a context whose
       language did not load takes no bracket at all, and its run reports
       the failure the way a run on its own does. */
    if (ctx->job_snapshots
        && _showpage_semantic(ctx) != XPOST_SHOWPAGE_RETURN)
    {
        if (!ctx->sysdict_load_done)
            _load_language(ctx);

        if (ctx->sysdict_load_done)
        {
            gsav = xpost_save_create_snapshot_object(ctx->gl);
            lsav = xpost_save_create_snapshot_object(ctx->lo);
        }
    }

    /* Run! */
run:
    ctx->quit = 0;
    ctx->state = C_RUN;
    ret = mainloop(ctx);

    /* A context that did not validate has no run to report on, and
       nothing below may be asked of it: the memory it failed to answer
       for is where the page semantics are read from, where the device
       stands and where the snapshot records live. So this is read before
       any of them, and the run gives back the file it wrapped around the
       program and nothing else. */
    if (ret == XPOST_MAINLOOP_INVALID)
    {
        XPOST_LOG_ERR("the context did not validate; the run is abandoned");
        _close_run_input(ctx);
        return XPOST_RUN_FAILED;
    }

    if (_showpage_semantic(ctx) == XPOST_SHOWPAGE_RETURN)
    {
        if (ret == XPOST_MAINLOOP_YIELDED)
            return XPOST_RUN_YIELDED;

        /* the run stops at its quit operator, leaving the frames
           beneath it -- the run's own scheduling tail -- on the
           exec stack. A persistent context serving many runs
           accumulates them, and an error unwind can later walk
           down into a stale frame and execute it out of context.
           Discard everything this run left behind, for errored
           runs just as for completed ones. A run abandoned inside a
           wrapped operator leaves that call's frame here too: let go
           of the operands saved for it, or a context serving run after
           run fills the room the copies are taken from. */
        while (xpost_stack_count(ctx->lo, ctx->es) > (int)ctx->es_run_base)
        {
            Xpost_Object x = xpost_stack_pop(ctx->lo, ctx->es);

            if (xpost_object_get_type(x) == operatortype &&
                (x.mark_.padw == (unsigned int)XPOST_OP_CODE(ctx, wrapdone) ||
                 x.mark_.padw == (unsigned int)XPOST_OP_CODE(ctx, wrapsealed)))
                xpost_operator_wrapped_release(ctx,
                        xpost_stack_pop(ctx->lo, ctx->es));
        }

        _rewind_job_snapshots(ctx, gsav, lsav);
        _close_run_input(ctx);
        return ctx->run_uncaught ? XPOST_RUN_ERRORED : XPOST_RUN_COMPLETE;
    }

    XPOST_LOG_INFO("destroying device");
    /* the device lives in the graphics state; the DEVICE name is an
       accessor operator and no longer holds the dictionary itself */
    device = xpost_dict_get(ctx, ctx->privatedict,
            xpost_name_cons(ctx, ".graphicsdict"));
    if (xpost_object_get_type(device) == dicttype)
        device = xpost_dict_get(ctx, device, xpost_name_cons(ctx, "currgstate"));
    if (xpost_object_get_type(device) == dicttype)
        device = xpost_dict_get(ctx, device, xpost_name_cons(ctx, "device"));
    XPOST_LOG_INFO("device type=%s", xpost_object_type_names[xpost_object_get_type(device)]);
    /*xpost_operator_dump(ctx, 1); // is this pointer value constant? */
    if (xpost_object_get_type(device) == arraytype){
        XPOST_LOG_INFO("running proc");
        xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, quit));
        xpost_stack_push(ctx->lo, ctx->es, device);

        ctx->quit = 0;
        /* the device this leaves on the operand stack is the procedure's
           answer, so a run that did not reach its end has not left one */
        if (mainloop(ctx) == XPOST_MAINLOOP_DONE)
            device = xpost_stack_pop(ctx->lo, ctx->os);
        else
        {
            XPOST_LOG_ERR("the device procedure did not run to its end");
            device = null;
        }
    }
    if (xpost_object_get_type(device) == dicttype)
    {
        Xpost_Object Destroy;
        /* The release run is the one the device's block was issued to be
           given up by, recorded when the block was issued and reached
           from the block rather than from /Destroy -- an ordinary slot
           the program writes to. A device carrying such a block (the
           ones whose Destroy is a C operator) is given up by that
           operator whatever the slot now holds; the vector writers keep
           their state under a content block instead and give it up
           through the procedure under /Destroy, run below. */
        unsigned int release = xpost_handle_device_release(ctx, device);
        XPOST_LOG_INFO("destroying device dict");
        Destroy = xpost_dict_get(ctx, device, xpost_name_cons(ctx, "Destroy"));
        if (release != 0)
        {
            int res;
            xpost_stack_push(ctx->lo, ctx->os, device);
            res = xpost_operator_exec(ctx, release);
            if (res)
                XPOST_LOG_ERR("%s error destroying device", errorname[res]);
            else
                XPOST_LOG_INFO("destroyed device");
        }
	else if (xpost_object_get_type(Destroy) == arraytype)
	{
	    XPOST_LOG_INFO("running Destroy proc");
	    xpost_stack_push(ctx->lo, ctx->os, device);
	    /* the run this is tearing down stopped at its quit with the
	       frames it had yet to return through still on the exec
	       stack; a stop of this interval's own is what keeps the
	       teardown from carrying on down into them */
	    xpost_stack_push(ctx->lo, ctx->es, XPOST_OP(ctx, quit));
	    xpost_stack_push(ctx->lo, ctx->es, Destroy);

	    ctx->quit = 0;
	    if (mainloop(ctx) != XPOST_MAINLOOP_DONE)
	        XPOST_LOG_ERR("the device's Destroy did not run to its end");
	}
    }

    _rewind_job_snapshots(ctx, gsav, lsav);
    _close_run_input(ctx);
    return ctx->run_uncaught ? XPOST_RUN_ERRORED : XPOST_RUN_COMPLETE;
}

XPAPI void xpost_skip_graphics_set(Xpost_Context *ctx, int enable)
{
    ctx->skip_graphics = enable;
}

/* enable or disable per-job VM snapshots for a context */
XPAPI void xpost_job_snapshots_set(Xpost_Context *ctx, int enable)
{
    ctx->job_snapshots = enable;
}

XPAPI void xpost_stdout_handler_set(Xpost_Context *ctx,
                                    Xpost_Output_Fn fn,
                                    void *user)
{
    ctx->stdout_fn = fn;
    ctx->stdout_user = user;
}

XPAPI void xpost_stderr_handler_set(Xpost_Context *ctx,
                                    Xpost_Output_Fn fn,
                                    void *user)
{
    ctx->stderr_fn = fn;
    ctx->stderr_user = user;
}

/*
   destroy the interpreter's context and associated memory files,
   and with them the interpreter instance holding them.
 */
XPAPI void xpost_destroy(Xpost_Context *ctx)
{
    if (!ctx)
        return;

    /* The instance hands out one context, so this is the only handle
       that ends it. Anything else names a context this cannot account
       for, and taking a memory file apart under a live instance is
       worse than declining. */
    if (!itpdata || (ctx != &itpdata->ctab[0]))
    {
        XPOST_LOG_ERR("not the interpreter's context");
        return;
    }

    if (!ctx->quiet)
    {
        printf("bye!\n");
        fflush(NULL);
    }

    xpost_context_exit(ctx);

    free(itpdata);
    itpdata = NULL;
    xpost_ctx = NULL;
}
