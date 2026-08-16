/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
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

#ifndef XPOST_OP_CONTROL_H
#define XPOST_OP_CONTROL_H

/*
 * The access rule for scheduling an object for execution, shared
 * between the operators that schedule one and the interpreter's fused
 * procedure execution so the rule exists once.
 *
 * Executing an array or a string reads its contents, which an object
 * with no access forbids (PLRM 3.3.2); a dictionary is only pushed onto
 * the dictionary stack, so it is exempt. A file is read a token at a
 * time as it runs rather than scheduled whole, so its own rule is asked
 * where that reading happens, in the interpreter's evalfile.
 *
 * The two types the rule applies to carry their access in the tag of
 * the object in hand, so the field is read there. That is what the
 * general accessor returns for them; going through it instead would
 * put a call on the path every procedure call takes, to reach the
 * indirection only a dictionary or a file needs.
 *
 * The test on the type is therefore load-bearing for the answer and not
 * only for the cost, which is worth saying because the shape invites
 * widening it for a new type or for symmetry. Files do arrive here --
 * exec takes anytype, and running one is how a program executes a file
 * -- and they are turned away by the left of the conjunction before the
 * tag is ever read. That order is what keeps the answer right: an
 * execute-only file and a no-access file hold the SAME access field,
 * and are told apart only by a flag outside the mask read here, so the
 * field alone calls an execute-only file no-access -- the one state that
 * must still run. A file's rule lives in evalfile because a file's
 * access is a set of capabilities rather than a rung, and only the
 * accessor's own indirection folds that flag in.
 */
static inline int xpost_op_exec_access_ok(Xpost_Context *ctx, Xpost_Object O)
{
    Xpost_Object_Type type = xpost_object_get_type(O);

    (void)ctx;
    return !((type == arraytype || type == stringtype)
             && (O.tag & XPOST_OBJECT_TAG_DATA_FLAG_ACCESS_MASK)
                == (XPOST_OBJECT_TAG_ACCESS_NONE
                    << XPOST_OBJECT_TAG_DATA_FLAG_ACCESS_OFFSET));
}

/* terminate the innermost stopped context; with none, report and quit */
int xpost_op_stop(Xpost_Context *ctx);

/* record what ended the run for the embedding caller, from $error */
void xpost_op_record_run_error(Xpost_Context *ctx);

int xpost_oper_init_control_ops(Xpost_Context *ctx, Xpost_Object sd);

#endif
