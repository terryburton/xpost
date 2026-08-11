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

#ifndef XPOST_ITP_H
#define XPOST_ITP_H

#include "xpost_private.h" /* XPOST_TEST_VISIBLE */

/**
 * @file xpost_interpreter.h
 * @brief the interpreter functions
 *
 * The interpreter module manages the itpdata structure, allocating
 * contexts from a table, and allocating memory files to the contexts
 * also from tables. The itpdata structure thus encapsulates the entire
 * dynamic state of the interpreter as a whole.
 *
 * The interpreter module also contains functions for eval actions,
 * the core interpreter loop,
 *
 * @{
 */

/*# define MAXCONTEXT 10 // moved to xpost_context.h. <- must include first!
 */
#define MAXMFILE 10

typedef struct
{
    Xpost_Context ctab[MAXCONTEXT];
    unsigned int cid;
    Xpost_Memory_File gtab[MAXMFILE];
    Xpost_Memory_File ltab[MAXMFILE];
    int in_onerror;
} Xpost_Interpreter;


extern Xpost_Interpreter *itpdata;

/* garbage collection does not run during initializing */
int xpost_interpreter_get_initializing(void);
void xpost_interpreter_set_initializing(int i);

Xpost_Context *xpost_interpreter_cid_get_context(unsigned int cid);

/**
 * The event-handler handler.

 * In a multi-threaded configuration, this may not execute at in every eval()
 * but by a superior strategy.
 */
int idleproc(Xpost_Context *ctx);

extern int _xpost_interpreter_is_tracing;

/**
 * @brief Give the operands of the wrapped-operator calls being
 *        abandoned back to their caller, and drop what those calls
 *        left (PLRM 3.11.1 step 1).
 *
 * The interpreter does this itself for an error it raises, before it
 * records the command in $error. The PostScript error hook asks for it
 * on behalf of an error a body raised with signalerror, whose stop
 * never reaches the interpreter's handler, and stop asks for it for
 * the calls it abandons, which covers a body that caught a failure in
 * a stopped context of its own and raised it again with a bare stop,
 * passing no hook at all. Reading the frames does not spend them, so
 * the paths that ask early and the stop that ends them all reach the
 * same state. The walk ends at the boundary an operator leaves under a
 * procedure of the program's that it calls back into: a failure in
 * there is that procedure's, and the calls beneath it keep what they
 * consumed.
 */
int xpost_op_errorunwind(Xpost_Context *ctx);

int xpost_interpreter_init(Xpost_Interpreter *itp, const char *device);
void xpost_interpreter_exit(Xpost_Interpreter *itp);

/**
 * @brief Load the language into the context.
 *
 * The first of the two steps a run brings a context up with before the
 * program it was given: the modules are read and the interpreter is
 * locked down, and what stands afterwards is the language -- the same
 * names with the same values however the run was started. The second
 * step, making the device this run was started with, is what settles
 * something of the run, and it is not done here.
 *
 * A run does this itself for the context it was handed, so a caller
 * that only runs programs never needs it. It is separate for the sake
 * of the point between the two steps, which is where a context's
 * virtual memory is a picture of the language and of nothing else.
 *
 * The load runs once in the life of a context; whether it succeeded is
 * read from the context afterwards.
 */
XPOST_TEST_VISIBLE void xpost_interpreter_load_language(Xpost_Context *ctx);

/**
 * @brief Run a procedure to completion and return, so a C caller
 *        reaches a procedure of the program's as an ordinary call.
 *
 * The procedure runs on the stacks of the surrounding run, stepping the
 * evaluator until the execution stack falls back to the depth it
 * started at. Callers are operators that need the procedure's answer
 * to finish their own work, such as a filter refilling from a data
 * source.
 *
 * An error inside the procedure is raised here. It runs under the
 * boundary an operator leaves beneath a call into the program's own
 * code, so the operands restored are the procedure's rather than those
 * of the operator that called in (PLRM 3.11.1).
 *
 * The nesting is bounded. The depth is the program's to choose, and one
 * past what the C stack carries is limitcheck.
 *
 * @return 0, or the error the run could not handle.
 */
int xpost_interpreter_run_nested(Xpost_Context *ctx, Xpost_Object P);

/**
 * @}
 */

#endif
