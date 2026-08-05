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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h> /* NULL */

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_context.h"
#include "xpost_error.h"
#include "xpost_name.h"
#include "xpost_array.h"
#include "xpost_dict.h"

#include "xpost_operator.h"
#include "xpost_oplib.h"

#include "xpost_op_stack.h"
#include "xpost_op_string.h"
#include "xpost_op_array.h"
#include "xpost_op_dict.h"
#include "xpost_op_boolean.h"
#include "xpost_op_control.h"
#include "xpost_op_type.h"
#include "xpost_op_token.h"
#include "xpost_op_math.h"
#include "xpost_op_file.h"
#include "xpost_op_save.h"
#include "xpost_op_misc.h"
#include "xpost_op_packedarray.h"
#include "xpost_op_param.h"
#include "xpost_op_matrix.h"
#include "xpost_op_path.h"
#include "xpost_op_font.h"
#include "xpost_op_context.h"
#include "xpost_dev_generic.h"
#ifdef _WIN32
# include "xpost_dev_win32.h"
#endif
#ifdef HAVE_XCB
# include "xpost_dev_xcb.h"
#endif
#include "xpost_dev_bgr.h"
#include "xpost_dev_raster.h"
#ifdef HAVE_LIBPNG
# include "xpost_dev_png.h"
#endif
#ifdef HAVE_LIBJPEG
# include "xpost_dev_jpeg.h"
#endif

/* no-op operator useful as a break target.
   put 'breakhere' in the postscript program,
   run interpreter under gdb,
   gdb> b xpost_op_breakhere
   gdb> run
   will break in the breakhere function (of course),
   which you can follow back to the main loop (gdb> next),
   just as it's about to read the next token.
*/
int xpost_op_breakhere(Xpost_Context *ctx)
{
    (void)ctx;
    return 0;
}

/* create systemdict and call
   all initop?* functions, installing all operators */
int xpost_oplib_init_ops(Xpost_Context *ctx)
{
    Xpost_Object op;
    Xpost_Object n;
    Xpost_Object sd;
    Xpost_Memory_Table *tab;
    unsigned ent;
    Xpost_Operator *optab;

    /* Mark every reference-table entry uncaptured before any module
       registers. Zero cannot serve as the marker: it is a valid opcode --
       the first operator registered holds it -- so an entry left zero
       would be indistinguishable from a genuine capture of that operator.
       An unset entry must also never match a real opcode where the
       procedure walker recognises one, which -1 satisfies. */
#define XPOST_OP_REF_UNSET(ref, refname) XPOST_OP_CODE(ctx, ref) = -1;
    XPOST_OP_REFS(XPOST_OP_REF_UNSET)
#undef XPOST_OP_REF_UNSET

    sd = xpost_dict_cons (ctx, SDSIZE);
    if (xpost_object_get_type(sd) == nulltype)
    {
        XPOST_LOG_ERR("cannot allocate systemdict");
        return 0;
    }
    if (xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "systemdict"), sd))
    {
        XPOST_LOG_ERR("cannot name systemdict in itself");
        return 0;
    }
    xpost_stack_push(ctx->lo, ctx->ds, sd); // push systemdict on dictstack
    ent = xpost_object_get_ent(sd);
    tab = &ctx->gl->table;
    tab->tab[ent].sz = 0; // make systemdict immune to collection

#ifdef DEBUGOP
    xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
    puts("");
#endif

    if (xpost_oper_init_stack_ops(ctx, sd))

        return 0;

//#ifdef DEBUGOP
    //printf("\nops:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
//#endif

    op = xpost_operator_cons(ctx, "breakhere", (Xpost_Op_Func)xpost_op_breakhere, 0, 0);
    INSTALL;

    if (xpost_oper_init_string_ops(ctx, sd))

        return 0;
    //printf("\nopst:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_array_ops(ctx, sd))

        return 0;
    //printf("\nopar:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_dict_ops(ctx, sd))

        return 0;
    //printf("\nopdi:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_bool_ops(ctx, sd))

        return 0;
    //printf("\nopb:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_control_ops(ctx, sd))

        return 0;
    //printf("\nopc:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_type_ops(ctx, sd))

        return 0;
    //printf("\nopt:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_token_ops(ctx, sd))

        return 0;
    //printf("\noptok:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_math_ops(ctx, sd))

        return 0;
    //printf("\nopm:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_file_ops(ctx, sd))

        return 0;
    //printf("\nopf:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_save_ops(ctx, sd))

        return 0;
    //printf("\nopv:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_misc_ops(ctx, sd))

        return 0;
    //printf("\nopx:\n"); xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);

    if (xpost_oper_init_packedarray_ops(ctx, sd))

        return 0;
    if (xpost_oper_init_param_ops(ctx, sd))
        return 0;
    if (xpost_oper_init_matrix_ops(ctx, sd))
        return 0;
    if (xpost_oper_init_path_ops(ctx, sd))
        return 0;
    if (xpost_oper_init_font_ops(ctx, sd))
        return 0;
    if (xpost_oper_init_generic_device_ops(ctx, sd))
        return 0;
#ifdef _WIN32
    if (xpost_oper_init_win32_device_ops(ctx, sd))
        return 0;
#endif
#ifdef HAVE_XCB
    if (xpost_oper_init_xcb_device_ops(ctx, sd))
        return 0;
    //printf("xcb:\n");
#endif
    if (xpost_oper_init_bgr_device_ops(ctx, sd))
        return 0;
    if (xpost_oper_init_raster_device_ops(ctx, sd))
        return 0;
#ifdef HAVE_LIBPNG
    if (xpost_oper_init_png_device_ops(ctx, sd))
        return 0;
#endif
#ifdef HAVE_LIBJPEG
    if (xpost_oper_init_jpeg_device_ops(ctx, sd))
        return 0;
#endif
    if (xpost_oper_init_context_ops(ctx, sd))
        return 0;

    /* Every module has registered by here. A registration that could
       not place its operator in systemdict left the interpreter without
       it, and there is no recovering from that. */
    if (ctx->operator_install_refused)
    {
        XPOST_LOG_ERR("an operator could not be installed in systemdict");
        return 0;
    }

    /* An entry still holding the uncaptured marker names an operator no
       module registered, so nothing the interpreter schedules through it
       would be an operator at all. */
    {
        int uncaptured = 0;
#define XPOST_OP_REF_CHECK(ref, refname) \
        if (XPOST_OP_CODE(ctx, ref) == -1) \
        { \
            XPOST_LOG_ERR("no operator named %s to reach for", refname); \
            uncaptured = 1; \
        }
        XPOST_OP_REFS(XPOST_OP_REF_CHECK)
#undef XPOST_OP_REF_CHECK
        if (uncaptured)
            return 0;
    }

#ifdef DEBUGOP
    printf("final sd:\n");
    xpost_stack_dump(ctx->lo, ctx->ds);
    xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
#endif

    return 1;
}
