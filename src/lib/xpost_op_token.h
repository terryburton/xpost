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

#ifndef XPOST_OP_TOKEN_H
#define XPOST_OP_TOKEN_H

int xpost_oper_init_token_ops(Xpost_Context *ctx, Xpost_Object sd);

/**
 * @brief decode one number of a binary token or encoded number string
 * (PLRM 3.14.4/3.14.5): @p rep selects representation (0..31 32-bit
 * fixed point scaled by rep, 32..47 16-bit fixed point scaled by
 * rep-32, 48 IEEE real, 49 native-order real; +128 = low-order byte
 * first), @p p the encoded bytes. 0 or the error to raise.
 */
int xpost_scanner_rep_number(unsigned int rep, const unsigned char *p, Xpost_Object *retval);

/**
 * @brief read a radix number (PLRM 3.2) from the head of @p s: a decimal
 * base of 2 through 36, '#', then one or more digits ranging from 0 to
 * base-1, with A through Z (or a through z) standing for 10 upwards.
 *
 * @p end is left at the first character that is not one of the base's
 * digits, past the whole of the numeral whether or not it fits.
 *
 * 0 with the number in @p out, limitcheck for a number past the
 * integer's field, or -1 for text that is not a radix number at all --
 * which PLRM 3.2 makes a name rather than an error.
 */
int xpost_scanner_radix_number(const char *s, const char **end, integer *out);

#endif
