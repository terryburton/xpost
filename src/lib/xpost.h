/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * Copyright (C) 2013-2016, Vincent Torri
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

#ifndef XPOST_H
#define XPOST_H

#ifdef XPAPI
# undef XPAPI
#endif

#ifdef _WIN32
# ifdef XPOST_BUILD
#  ifdef DLL_EXPORT
#   define XPAPI __declspec(dllexport)
#  else
#   define XPAPI
#  endif
# else
#  define XPAPI __declspec(dllimport)
# endif
#else
# ifdef __GNUC__
#  if __GNUC__ >= 4
#   define XPAPI __attribute__ ((visibility("default")))
#  else
#   define XPAPI
#  endif
# else
#  define XPAPI
# endif
#endif

#include <stdlib.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif /* ifdef __cplusplus */


/**
 * @file xpost.h
 * @brief This file provides the Xpost API functions.
 *
 * This is the master "include" file which includes
 * all headers in the proper order needed to control
 * xpost features at the top level.
 * @defgroup xpost_library Library functions
 *
 * @{
 */

/**
 * @brief Initialize the xpost library.
 *
 * @return The new init count. Will be 0 if initialization failed.
 *
 * The first time this function is called, it will perform all the internal
 * initialization required for the library to function properly and increment
 * the initialization counter. Any subsequent call only increment this counter
 * and return its new value, so it's safe to call this function more than once.
 *
 * @see xpost_quit();
 */
XPAPI int xpost_init(void);

/**
 * @brief Quit the xpost library.
 *
 * @return The new init count.
 *
 * If xpost_init() was called more than once for the running application,
 * xpost_quit() will decrement the initialization counter and return its
 * new value, without doing anything else. When the counter reaches 0, all
 * of the internal elements will be shutdown and any memory used freed.
 *
 * @see xpost_init()
 */
XPAPI int xpost_quit(void);

/**
 * @brief Request a PostScript-level interrupt.
 *
 * Raises the interrupt error at the interpreter's next evaluation
 * step, as the language specifies for an external interrupt request
 * such as control-C. Only a flag is set, so this function is safe to
 * call from a signal handler.
 */
XPAPI void xpost_interrupt(void);

/**
 * @brief Retrieve the version of the library.
 *
 * @param[out] maj The major version.
 * @param[out] min The minor version.
 * @param[out] mic The micro version.
 *
 * This function stores the major, minor and micro version of the library
 * respectively in the buffers @p maj, @p min and @p mic. @p maj, @p min
 * and @p mic can be @c NULL.
 */
XPAPI void xpost_version_get(int *maj, int *min, int *mic);

/**
 * @brief Return the path of the shared library.
 *
 * @return The path of the shared library.
 *
 * This function returns the path of the shared library.
 */
XPAPI const char *xpost_lib_dir_get(void);

/**
 * @brief Return the path of the data directory, based on the path of the
 * shared library.
 *
 * @return The path of the data directory.
 *
 * This function returns the path of the data directory, based on the shared library. More precisely, it is xpost_lib_path_get()../share/xpost.
 */
XPAPI const char *xpost_data_dir_get(void);

/**
 * @typedef Xpost_Context
 * @brief The context abstract structure for a thread of execution of ps code.
 */
typedef struct _Xpost_Context Xpost_Context;

/**
 * @typedef Xpost_Showpage_Semantics
 * @brief Specify the behavior the interpreter should take when executing `showpage`.
 */
typedef enum {
    XPOST_SHOWPAGE_DEFAULT, /**< Print "----showpage----\n" to stdout
                                 and read and discard a line of text
                                 from stdin (ie. wait for return). */
    XPOST_SHOWPAGE_NOPAUSE, /**< Bypasses this action but still
                                 performs a "flush" of the graphics
                                 device. */
    XPOST_SHOWPAGE_RETURN /**< Causes the interpreter to return
                               control to its caller; the suspended
                               context may be resumed by calling
                               xpost_run with the #XPOST_INPUT_RESUME
                               input type. */
} Xpost_Showpage_Semantics;

/**
 * @typedef Xpost_Output_Type
 * @brief Specify the interpretation of the outputptr parameter to xpost_create().
 */
typedef enum {
    XPOST_OUTPUT_DEFAULT, /**< Ignores outputptr. */
    XPOST_OUTPUT_FILENAME, /**< Treats outputptr as a char* to a
                                zero-terminated OS path string
                                (implemented in pgm and ppm devices). */
    XPOST_OUTPUT_BUFFERIN, /**< Treats outputptr as an unsigned char *
                                and renders directly into this memory.
                                Implemented by the raster device, which
                                keeps its page extent in front of the
                                page and so needs room for that as well;
                                every other device allocates a page of
                                its own and leaves this memory alone. The
                                memory stays the caller's throughout: it
                                is not given back with
                                xpost_output_buffer_release(), which has
                                nothing to give back for a page the
                                library did not allocate. */
    XPOST_OUTPUT_BUFFEROUT /**< Treats outputptr as an unsigned char **
                                and assigns a new buffer to the
                                unsigned char * which outputptr points
                                to. The buffer is given back with
                                xpost_output_buffer_release(). */
} Xpost_Output_Type;

/**
 * @typedef Xpost_Input_Type
 * @brief Specify the interpretation of the inputptr parameter to xpost_run().
 */
typedef enum {
    XPOST_INPUT_STRING, /**< Treats inputptr as a char * to an
                             zero-terminated ascii string, writes the
                             whole string into a temporary file and
                             falls through to the #XPOST_INPUT_FILEPTR
                             case. */
    XPOST_INPUT_FILENAME, /**< Treats inputptr as a char * to a
                              zero-terminated OS path string, and
                              pushes the path string itself,
                              scheduling a procedure to execute it. */
    XPOST_INPUT_FILEPTR, /**< Treats inputptr as a FILE *, creates a
                               postscript file object and pushes it on
                               the execution stack (scheduling it to
                               execute). */
    XPOST_INPUT_RESUME /**< Bypasses any execution scheduling. */
} Xpost_Input_Type;

/**
 * @typedef Xpost_Set_Size
 * @brief FIXME: to fill...
 *
 * Currently, only "ignore size" is implemented.
 */
typedef enum {
    XPOST_IGNORE_SIZE,
    XPOST_USE_SIZE
} Xpost_Set_Size;

/**
 * @typedef Xpost_Output_Message
 * @brief Specify the kind of messages that the interpreter displays to output.
 */
typedef enum
{
    XPOST_OUTPUT_MESSAGE_QUIET, /**< Suppress interpreter messages. */
    XPOST_OUTPUT_MESSAGE_VERBOSE, /**< Display some interpreter messages. */
    XPOST_OUTPUT_MESSAGE_TRACING /**< Display all interpreter messages and fill xdump* file. */
} Xpost_Output_Message;

/**
 * @brief Create a newly allocated context.
 *
 * @param device
 * @param output_type
 * @param outputptr
 * @param semantics
 * @param quiet
 * @param set_size
 * @param width The height of the context page.
 * @param height The height of the context page.
 *
 * @return The interpreter's context, or @c NULL on failure.
 *
 * This function creates a #Xpost_Context with the given parameters,
 * bringing up the one interpreter instance the process may hold and
 * returning the context that instance runs.
 *
 * The instance is single: an interpreter's multiple execution contexts
 * live in its own context table, so a second call to this function while
 * an instance is live returns @c NULL rather than replacing the live
 * instance under a context the caller still holds. Sequential use is
 * unrestricted -- once xpost_destroy() has ended an instance, this
 * function creates another.
 *
 * When not needed the context must be freed with xpost_destroy().
 *
 * @see xpost_destroy()
 */
XPAPI Xpost_Context *xpost_create(const char *device,
                                  Xpost_Output_Type output_type,
                                  const void *outputptr,
                                  Xpost_Showpage_Semantics semantics,
                                  Xpost_Output_Message output_msg,
                                  Xpost_Set_Size set_size,
                                  int width,
                                  int height);

/**
 * @brief Add extra definitions to userdict
 *
 * @param ctx The context to use.
 * @param cnt The number of elements in the @p defs array
 * @param defs An argv-style array of pointers to "key=value" strings.
 *
 * This function allows extra defined key/value pairs to be
 * added to userdict after a context is created using xpost_create,
 * presumably before calling xpost_run.
 *
 * Definitions may be used by the ps program or to supply control
 * information to specific devices.
 *
 * This will present some duplication of features once the
 * setpagedevice and setuserparams operators are implemented.
 * But it still represents a useful construct for postscript code.
 */
XPAPI int xpost_add_definitions(Xpost_Context *ctx,
                                int cnt,
                                char *defs[]);

/**
 * @brief Run a context's programs without loading graphics.
 *
 * By default each xpost_run() job loads the graphics modules before
 * running. When enabled, xpost_run() selects the no-graphics start
 * procedures: the interpreter is still locked down (the language
 * relocates into systemdict and the private namespaces are sealed),
 * but the graphics modules are never loaded. Use for a program that
 * needs no graphics, or to exercise the no-graphics lockdown path.
 */
XPAPI void xpost_skip_graphics_set(Xpost_Context *ctx, int enable);

/**
 * @brief Build the language rather than read it out of an image.
 *
 * Where the environment names an image of virtual memory,
 * xpost_create() reads the language out of it instead of running the
 * boot files, and the context comes up with the language the image was
 * written with. That is decided before a caller has said anything about
 * what language it wants, so a caller that wants another one -- one
 * without graphics -- says so here, before creating the context.
 *
 * There is no way back: a process that has said this builds the
 * language for the rest of its life.
 */
XPAPI void xpost_vm_image_refuse(void);

/**
 * @brief Declare that this context serves no interactive user.
 *
 * A program named to xpost_run() as XPOST_INPUT_FILENAME is a job, and a
 * job ends where its program ends. When enabled, that is all a run does:
 * the run returns when the named program returns, whatever standard
 * input happens to be. Left disabled, a run over a named program offers
 * the interactive executive after the program when standard input is a
 * terminal, and returns as above when it is not.
 *
 * A program that wants a session after itself asks for one in the
 * language, with the executive operator, which is unaffected either way.
 */
XPAPI void xpost_batch_set(Xpost_Context *ctx, int enable);

/**
 * @brief Receives text output from the interpreter.
 *
 * @param user The pointer registered alongside the handler.
 * @param buf The bytes written by the program.
 * @param len The number of bytes.
 * @return The number of bytes accepted; a short count is an error.
 */
typedef size_t (*Xpost_Output_Fn)(void *user, const char *buf, size_t len);

/**
 * @brief Divert the program's standard-output text to a handler.
 *
 * Everything a program writes to its standard output -- print, = and
 * writes to the %stdout file -- is passed to @p fn instead of the
 * process's stdout. Device output (files, buffers) is not affected.
 * Pass NULL to restore the default.
 */
XPAPI void xpost_stdout_handler_set(Xpost_Context *ctx,
                                    Xpost_Output_Fn fn,
                                    void *user);

/**
 * @brief Divert the program's standard-error text to a handler.
 *
 * As xpost_stdout_handler_set(), for writes to the %stderr file.
 */
XPAPI void xpost_stderr_handler_set(Xpost_Context *ctx,
                                    Xpost_Output_Fn fn,
                                    void *user);

/**
 * @brief Enable or disable per-job VM snapshots for a context.
 *
 * By default each xpost_run() job takes virtual-memory snapshots that
 * the quit path restores. A persistent context that serves many runs
 * accumulates one save level per run, pinning each run's garbage
 * against collection; disable the snapshots for such use.
 */
XPAPI void xpost_job_snapshots_set(Xpost_Context *ctx, int enable);

/**
 * @brief File-access sandbox: permit directory trees, then engage.
 *
 * Before the sandbox is engaged, a program's disk access is
 * unrestricted. xpost_path_permit_read() (xpost_path_permit_write())
 * grants reading (writing) of files within a directory tree;
 * xpost_path_control_engage() then denies every other disk open by the
 * running program. Engaging is process-wide and one-way -- it cannot be
 * reversed and the permit set is frozen -- so configure the permitted
 * directories first and engage before running untrusted input.
 * Resource-file loading is separately confined and is unaffected.
 *
 * The permit functions answer whether the directory is permitted
 * afterwards: 1 when it is, including when the permitted set already
 * covers it -- asking again for the same tree costs nothing and may be
 * done as often as is convenient -- and 0 when the set does not cover it
 * and cannot be extended to, because the directory does not resolve, the
 * sandbox is engaged, or the table (64 entries) is full. A refusal is
 * also reported on the error log.
 *
 * The sandbox belongs to the process, not to a context. Every context
 * the process creates is confined by the same latch and reaches the same
 * directories, so one created after the sandbox is engaged finds it
 * engaged and finds what was permitted before it existed. This confines
 * the process against the program it runs; it does not divide one job in
 * the process from another.
 *
 * This is defence in depth: it complements, and does not replace,
 * operating-system confinement of the host process.
 */
XPAPI int xpost_path_permit_read(const char *dir);
XPAPI int xpost_path_permit_write(const char *dir);
XPAPI void xpost_path_control_engage(void);

/**
 * @brief Append a directory to the resource search path.
 *
 * findresource searches these directories, in the order added, when a
 * resource is not already defined in virtual memory. Add directories
 * before running the program that resolves resources. Returns 1 on
 * success, 0 on failure.
 */
XPAPI int xpost_add_resource_dir(Xpost_Context *ctx, const char *dir);

/**
 * @brief Outcome of executing a program with xpost_run().
 *
 * A context that reports #XPOST_RUN_COMPLETE, #XPOST_RUN_YIELDED or
 * #XPOST_RUN_ERRORED remains usable for further runs; after
 * #XPOST_RUN_FAILED it must be destroyed. When a run reports
 * #XPOST_RUN_ERRORED, xpost_error_name_get() identifies the
 * PostScript error that ended it.
 */
typedef enum {
    XPOST_RUN_COMPLETE = 0, /**< the program ran to completion */
    XPOST_RUN_YIELDED,      /**< showpage returned control to the caller
                                 (#XPOST_SHOWPAGE_RETURN); pass
                                 #XPOST_INPUT_RESUME to continue */
    XPOST_RUN_ERRORED,      /**< an uncaught PostScript error ended the
                                 program; the context has been tidied
                                 and accepts further runs */
    XPOST_RUN_FAILED        /**< the run could not be scheduled or the
                                 interpreter is no longer coherent */
} Xpost_Run_Status;

/**
 * @brief Name of the PostScript error that ended the last run.
 *
 * Valid after xpost_run() returns #XPOST_RUN_ERRORED, until the next
 * run on the same context; the empty string otherwise. The name is the
 * standard error name, e.g. "typecheck" or "undefined".
 */
XPAPI const char *xpost_error_name_get(Xpost_Context *ctx);

/**
 * @brief Additional information for the error that ended the last run.
 *
 * The errorinfo detail recorded alongside the error, when the program
 * supplied one; the empty string otherwise.
 */
XPAPI const char *xpost_error_info_get(Xpost_Context *ctx);

/**
 * @brief Execute ps program.
 *
 * @param ctx The context to run.
 * @param input_type The input type to use.
 * @param inputptr The pointer passed to the interpreter.
 * @param size The size of the memory passed to the interpreter.
 * @return The run's outcome as an #Xpost_Run_Status.
 *
 * This function executes a ps program until quit, fall-through to quit,
 * #XPOST_SHOWPAGE_RETURN semantic, or error (default action: message,
 * purge and quit).
 *
 * Depending upon @p input_type, this function will package the input
 * into an appropriate postscript object and schedule it for execution
 * by marking it executable and pushing to the exec stack, or by
 * pushing to the operand stack and pushing to the exec stack a small
 * program to execute it.
 *
 * The parameter @p size is used when @p input_type is
 * #XPOST_INPUT_STRING. If @p inputptr is a nul terminated string, 0
 * can be passed and the default size will be the length of the
 * string. If @p inputptr is a piece of memory, then pass the size of
 * that memory.
 *
 * For a filename, push a proc to open and execute it.
 *
 * For a string, write to a temp file and fall-through to FILE * case.
 *
 * For a FILE *, mark executable and push to exec stack.
 *
 * As a special-case, if executing a FILE *, and that file is a
 * console or tty, it pushes a proc which launches the postscript
 * `executive` which offers PS> prompts.
 *
 * If an output device (such as a window) has been specified in the
 * call to xpost_create(), it is here in the startup code, where
 * the device is initialized. The device is specified in xpost_create,
 * not because it is needed at that point, but because it is considered
 * a constant for the context, whereas it is intended that a context
 * may be re-used by calling xpost_run upon it again, presumably with
 * differing arguments.
 *
 * @see #Xpost_Input_Type
 */
XPAPI Xpost_Run_Status xpost_run(Xpost_Context *ctx,
                    Xpost_Input_Type input_type,
                    const void *inputptr,
                    size_t size);

/**
 * @brief Destroy the given context.
 *
 * @param ctx The context to destroy.
 *
 * This function destroys the context @p ctx which has been created with
 * xpost_create(), and with it the interpreter instance holding it.
 * Nothing of the instance outlives the call, so xpost_create() may be
 * called again afterwards to obtain another.
 *
 * A @c NULL @p ctx, or a pointer that is not the context xpost_create()
 * returned for the live instance, is declined and nothing is destroyed.
 *
 * @see xpost_create()
 */
XPAPI void xpost_destroy(Xpost_Context *ctx);

/**
 * @brief Give back a page buffer a run handed over.
 *
 * @param buffer The address a #XPOST_OUTPUT_BUFFEROUT run was given as
 *        its outputptr, holding the buffer that run stored there.
 *
 * A run started with #XPOST_OUTPUT_BUFFEROUT stores its finished page
 * through the address the caller gave xpost_create(), and the page is
 * the caller's from that moment. It is not part of the interpreter's
 * memory: xpost_destroy() leaves it alone and nothing the interpreter
 * does afterwards reaches it, so a caller may destroy the context and
 * read the pixels after. This call is how the buffer is given back.
 *
 * Pass the address xpost_create() was given, holding the pointer the run
 * stored there. The buffer is released and the pointer set to null, so a
 * second call on the same variable does nothing: a null @p buffer, and a
 * @p buffer holding null, are both accepted and do nothing. Any address
 * holding the pointer will do -- what the call reads is the pointer and
 * what it clears is the variable it was handed -- but a caller that
 * releases through a copy is left holding an original that no longer
 * names memory. An address holding a pointer this library did not hand
 * over is as undefined as passing such a pointer to free().
 *
 * Release once the context is done with the buffer. Every page of a run
 * is painted into the same buffer and the context paints into it until
 * it is destroyed, so the buffer is given back after xpost_destroy(), or
 * at least after the last run that paints a page.
 *
 * A caller that never releases leaks the buffer. Neither xpost_destroy()
 * nor xpost_quit() gives it back, since neither can know whether the
 * caller is still reading it; this call is the only one that does.
 *
 * How the memory was obtained, and how it is given back, is the
 * library's business and not part of this contract -- which is why the
 * buffer is released here rather than by the caller's own free().
 *
 * @see xpost_create()
 */
XPAPI void xpost_output_buffer_release(unsigned char **buffer);

/**
 * @brief Set quality value for compression of JPEG files.
 *
 * @param ctx The context to use.
 * @param quality The quality value, between 0 and 100.
 *
 * This function is a helper to set the quality value for compressing
 * a JPEG file to @p quality. It internally uses
 * xpost_add_definitions(). @p quality must be between 0 and 100. On
 * error, nothing is done.
 *
 * @see xpost_add_definitions()
 */
XPAPI void
xpost_dev_jpeg_options_set(Xpost_Context *ctx,
                           int quality);

/**
 * @brief Set quality value for compression of PNG files.
 *
 * @param ctx The context to use.
 * @param compression_level The compression level, between 0 and 9.
 * @param interlaced Whether the PNG file is interlaced or not.
 *
 * This function is a helper to set the compression level and whether
 * the PNG file is interlaced or not with respectively
 * @p compression_level and @p interlaced. It internally uses
 * xpost_add_definitions(). @p compression_level must be between 0 and
 * 9. On error, nothing is done.
 *
 * @see xpost_add_definitions()
 */
XPAPI void
xpost_dev_png_options_set(Xpost_Context *ctx,
                          int compression_level,
                          int interlaced);

/**
 * @}
 */


#ifdef __cplusplus
}
#endif /* ifdef __cplusplus */

#endif
