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

#ifndef XPOST_CONTEXT_H
#define XPOST_CONTEXT_H

/**
 * @file xpost_context.h
 * @brief This file provides the context functions.
 *
 * This header provides the Xpost context functions.
 * @defgroup xpost_library Library functions
 *
 * @{
 */

#define MAXCONTEXT 10

/**
 * @brief valid values for Xpost_Context::vmmode
 */
enum { LOCAL, GLOBAL };

/**
 * @brief valid values for Xpost_Context::state
 */
enum { C_FREE, C_IDLE, C_RUN, C_WAIT, C_IOBLOCK, C_ZOMB };

/**
 * @def XPOST_OP_REFS
 * @brief every operator the interpreter itself reaches for, by name
 *
 * C reaches a standard operator by holding the operator object. Pushing
 * the operator's name instead defers the decision to the dictionary stack
 * as it stands when the name runs, so a program that defines that name --
 * which PLRM 3.3 entitles it to do -- takes over the inside of a standard
 * operator. Resolving a name at run time also costs a global name intern
 * and a linear walk of the operator table, and answers null for a name
 * that is not there, which the interpreter then schedules.
 *
 * This is the one statement of that set. Each entry gives the C spelling
 * of the reference and the operator's name in the language; the operator
 * prefix on some of them keeps a C keyword out of a member name. From it
 * are generated the member that holds each opcode, the marker written
 * before registration begins, and the check that every one was captured.
 * Capture happens inside xpost_operator_cons keyed by the name, so it
 * cannot be forgotten at a registration or attached to the wrong operator
 * -- both of which the marker and the check exist because of.
 *
 * Reach an entry through XPOST_OP (the operator object, to schedule it) or
 * XPOST_OP_CODE (its opcode, to recognise it); tests/check-op-references.sh
 * holds the tree to that.
 */
#define XPOST_OP_REFS(_) \
    /* recognised inline by the procedure walker */ \
    _(oppop,               "pop") \
    _(opexch,              "exch") \
    _(opdup,               "dup") \
    _(opindex,             "index") \
    _(oproll,              "roll") \
    _(opadd,               "add") \
    _(opsub,               "sub") \
    _(opmul,               "mul") \
    _(opeq,                "eq") \
    _(opne,                "ne") \
    _(oplt,                "lt") \
    _(ople,                "le") \
    _(opgt,                "gt") \
    _(opge,                "ge") \
    _(opif,                "if") \
    _(opifelse,            "ifelse") \
    _(opdef,               "def") \
    _(opget,               "get") \
    _(opput,               "put") \
    _(optype,              "type") \
    /* iteration: the operator that starts one and the continuation that \
       carries it, which is scheduled beneath each pass */ \
    _(opfor,               "for") \
    _(repeat,              "repeat") \
    _(loop,                "loop") \
    _(forall,              "forall") \
    _(filenameforall,      "filenameforall") \
    _(forcont,             "for.iterate") \
    _(repeatcont,          "repeat.iterate") \
    _(loopcont,            "loop.iterate") \
    _(arrayforallcont,     "forall.array.iterate") \
    _(stringforallcont,    "forall.string.iterate") \
    _(dictforallcont,      "forall.dict.iterate") \
    _(contfilenameforall,  "contfilenameforall") \
    /* scheduled by an operator implemented in C to finish its own work */ \
    _(cvx,                 "cvx") \
    _(load,                "load") \
    _(exec,                "exec") \
    _(token,               "token") \
    _(copy,                "copy") \
    _(stop,                "stop") \
    _(quit,                "quit") \
    _(join,                "join") \
    _(matrix,              "matrix") \
    _(defaultmatrix,       "defaultmatrix") \
    _(setmatrix,           "setmatrix") \
    _(concat,              "concat") \
    _(concatmatrix,        "concatmatrix") \
    _(rotate,              "rotate") \
    _(transform,           "transform") \
    _(itransform,          "itransform") \
    _(moveto,              "moveto") \
    _(lineto,              "lineto") \
    /* closes the array a device method call is assembled into */ \
    _(rbracket,            "]") \
    /* the frame marker a wrapped operator leaves on the execution stack, \
       and the same marker on a call a failure passed a boundary to leave */ \
    _(wrapdone,            "wrap.done") \
    _(wrapsealed,          "wrap.sealed") \
    /* the boundary an operator calling back into a procedure of the \
       program's leaves on the execution stack */ \
    _(calloutdone,         "callout.done")

/**
 * @def XPOST_OP_CODE
 * @brief the opcode of a referenced operator, for recognising one
 */
#define XPOST_OP_CODE(ctx, ref) ((ctx)->opcode_shortcuts.ref)

#define XPOST_OP_REF_MEMBER(ref, name) int ref;

/** @struct Xpost_Context
 * @brief The context structure for a thread of execution of ps code
 */
struct _Xpost_Context {

    /**< opcode of each operator the interpreter reaches for, captured as
         the operators are registered; see XPOST_OP_REFS */
    struct
    {
        XPOST_OP_REFS(XPOST_OP_REF_MEMBER)
    } opcode_shortcuts;
#undef XPOST_OP_REF_MEMBER

    Xpost_Object currentobject;  /**< currently-executing object, for error() */

    /* Set when a registration could not place an operator in systemdict.
       Registration is several hundred calls spread over two dozen
       modules, each of which would otherwise have to carry the answer
       back by hand; this collects it in one place, which
       xpost_oplib_init_ops reads once when they have all run. An
       interpreter missing an operator is not one that can run a
       program. */
    int operator_install_refused;

    /* operands the dispatcher coerced from integer to real for the current
       operator; an error restores them to the originals the program pushed,
       as PLRM 3.11 requires. Empty for operators that coerce nothing. */
    int op_restore_n;
    unsigned char op_restore_idx[8];
    Xpost_Object op_restore_val[8];

    /* array-packing mode (setpacking/currentpacking): when set, the scanner
       builds { } procedures read-only. packing_hist records the mode at each
       save level so restore reverts it, as the parameter is save/restore-subject */
    int packing;
    unsigned char packing_hist[256];

    /* cache of name -> value resolutions against the dict stack,
       invalidated in bulk whenever any binding may have changed */
    unsigned int *namecache_gen;   /**< generation per (name index, bank) */
    Xpost_Object *namecache_val;   /**< cached resolution */
    unsigned int namecache_size;   /**< entries allocated */
    unsigned int namebind_gen;     /**< current binding generation */

    Xpost_Object typenames[XPOST_OBJECT_NTYPES + 1]; /**< executable name per
                                                          type index, populated
                                                          on first use; the
                                                          index past the object
                                                          types names a packed
                                                          array (see
                                                          xpost_op_type.h) */

    /*@dependent@*/
    Xpost_Memory_File *gl; /**< global VM */
    /*@dependent@*/
    Xpost_Memory_File *lo; /**< local VM */

    unsigned int id; /**< cid for this context */

    unsigned int os, es, ds, hold; /**< stack addresses in local VM */
    /** The random number generator's state (PLRM 8.2 rand, srand, rrand).
        rrand reports it as an integer and srand takes one back, so it is
        the integer's own width: a state narrower than that would report a
        seed as something other than the seed it was given. */
    dword rand_next;
    unsigned int vmmode; /**< allocating in GLOBAL or LOCAL */

    /** The two font directories, so setglobal can rebind the name
        FontDirectory to whichever the allocation mode calls for (PLRM).
        Both are null until the boot file has defined them. */
    Xpost_Object localfontdir;
    Xpost_Object globalfontdir;
    unsigned int state;  /**< process state: running, blocked, iowait */
    unsigned int quit;  /**< if 1 cause mainloop() to return, if 0 keep looping */

    Xpost_Object event_handler;
    Xpost_Object window_device;

    /** The page device the graphics state template names, recorded by
        setpagedevice as it installs one, with the save depth it was
        installed at (depth + 1; zero when nothing is recorded) and the
        Destroy operator the instance carried then (null for a device
        whose Destroy is a PostScript procedure). A device holds its
        raster or its content accumulator outside virtual memory, so the
        collector has no claim on that memory and no reason to look:
        whoever takes the device out of the graphics state has to release
        it. setpagedevice does so for the device it replaces, and restore
        does so here, for the one it displaces (PLRM 6.1). The device is
        rooted in the collector, so the entity cannot be recycled while
        this names it. */
    Xpost_Object pagedevice;
    Xpost_Object pagedevice_destroy;
    unsigned int pagedevice_depth;

    /**< privatedict -- a LOCAL dictionary that holds the interpreter's local
         machinery (the device class dictionaries, the wrapped-operator anchor
         procedures, the graphics scratch and template). Rooted here so the
         collector keeps it and its contents, but never pushed on the dict
         stack, so a program can neither name nor enumerate its members. The
         C reaches the device classes through it; PostScript through a frozen
         reference. Set from init.ps by .setprivatedict. */
    Xpost_Object privatedict;
    const char *device_str;

    int quiet; /**< the -q/--quiet startup flag, retained so the shutdown
                    message can honour it without reading a PostScript name:
                    QUIET lives in the private .internaldict, out of a program's
                    reach, once init.ps has relocated it there. */

    int ignoreinvalidaccess; //briefly allow invalid access to put userdict in systemdict (per PLRM)

    int sysdict_unlocked; /**< systemdict is temporarily writeable while the
                            graphics language loads into it; the error handler
                            relocks it if a load faults */
    int sysdict_load_done; /**< the graphics language has been loaded into
                             systemdict; the one-shot unlock is spent */

    int es_over;              /**< the exec-stack ceiling has been reported;
                                   holds off a re-raise until depth recedes */
    int os_over;              /**< likewise for the operand stack */
    int ds_over;              /**< likewise for the dictionary stack */
    int onerr_run;            /**< consecutive errors handled without the run
                                   reaching `stop`; a runaway error cascade
                                   (an error raised from within the error
                                   machinery itself) drives this without bound */
    int scanner_defer; /**< the token just scanned is a brace procedure:
                            the interpreter pushes it as data rather than
                            executing it. A binary object sequence also
                            scans to an executable array but executes. */
    int scan_proc_depth; /**< the scanner's live brace-procedure nesting,
                              bounded against C-stack exhaustion; zero
                              between scans */

    size_t (*stdout_fn)(void *, const char *, size_t); /**< divert %stdout text */
    void *stdout_user;
    size_t (*stderr_fn)(void *, const char *, size_t); /**< divert %stderr text */
    void *stderr_user;

    char run_error_name[48];  /**< error that ended the last run ("" if none) */
    char run_error_info[128]; /**< errorinfo detail for the same ("" if none) */
    int run_uncaught;         /**< an error unwound past every stopped context */

    Xpost_Object run_input_file; /**< the file a run wrapped around the
                                      program it was given, when the run
                                      made the file itself; closed when the
                                      run ends, so a run that stopped before
                                      the end of its program does not leave
                                      it open. Invalid between runs. */

    unsigned int es_run_base; /**< exec-stack depth at xpost_run entry;
                                    a completed run is truncated back to
                                    this depth so its scheduling frames
                                    cannot accumulate across jobs */
    int skip_graphics; /**< run the interpreter lockdown (.finalize) without
                            loading graphics; xpost_run selects the no-graphics
                            start procedures so a program that needs no graphics
                            never pays to load them, and the no-graphics lockdown
                            path is exercised */

    int job_snapshots; /**< take VM snapshots around each xpost_run job
                            (restored on the quit path); disable for a
                            persistent context serving many runs, where
                            the per-run snapshots would accumulate save
                            levels and pin every run's garbage */

    int (*xpost_interpreter_cid_init)(unsigned int *cid);
    Xpost_Memory_File *(*xpost_interpreter_alloc_local_memory)(void);
    Xpost_Memory_File *(*xpost_interpreter_alloc_global_memory)(void);
    XPOST_MUST_CHECK int (*garbage_collect_function)(Xpost_Memory_File *mem,
                                                     int dosweep,
                                                     int markall);
};

int xpost_context_init_ctxlist(Xpost_Memory_File *mem);
int xpost_context_append_ctxlist(Xpost_Memory_File *mem, unsigned cid);

/**
 * @brief initialize the context structure
 */
int xpost_context_init(Xpost_Context *ctx,
                       int (*xpost_interpreter_cid_init)(unsigned int *cid),
                       Xpost_Context *(*xpost_interpreter_cid_get_context)(unsigned int cid),
                       int (*xpost_interpreter_get_initializing)(void),
                       void (*xpost_interpreter_set_initializing)(int),
                       Xpost_Memory_File *(*xpost_interpreter_alloc_local_memory)(void),
                       Xpost_Memory_File *(*xpost_interpreter_alloc_global_memory)(void),
                       int (*garbage_collect_function)(Xpost_Memory_File *mem, int dosweep, int markall));

/**
 * @brief destroy the context structure, and all components
 */
void xpost_context_exit(Xpost_Context *ctx);

/**
 * @brief utility function for extracting from the context
 *        the mfile relevant to an object
 */
/*@dependent@*/
XPOST_TEST_VISIBLE Xpost_Memory_File *xpost_context_select_memory(Xpost_Context *ctx, Xpost_Object o);

/**
 * @brief print a dump of the context structure data to stdout
 */
void xpost_context_dump(Xpost_Context *ctx);

/**
 * @brief install a function to be called by eval()
 */
int xpost_context_install_event_handler(Xpost_Context *ctx,
                                        Xpost_Object operator,
                                        Xpost_Object device);

/**
 * @brief fork new process with shared global and shared local vm (lightweight process)
 *
 * The memory files are shared because a context's name tables, operator
 * table and systemdict are built by the interpreter above this module: a
 * fork given memory files of its own would declare those entities
 * present and have none.
 */
unsigned int xpost_context_fork3(Xpost_Context *ctx,
                                 int (*xpost_interpreter_cid_init)(unsigned int *cid),
                                 Xpost_Context *(*xpost_interpreter_cid_get_context)(unsigned int cid),
                                 Xpost_Memory_File *(*xpost_interpreter_alloc_local_memory)(void),
                                 Xpost_Memory_File *(*xpost_interpreter_alloc_global_memory)(void),
                                 int (*garbage_collect_function)(Xpost_Memory_File *mem, int dosweep, int markall));

/**
 * @}
 */

#endif
