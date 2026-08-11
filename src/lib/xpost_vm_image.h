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

#ifndef XPOST_VM_IMAGE_H
#define XPOST_VM_IMAGE_H

#include "xpost_private.h" /* XPOST_TEST_VISIBLE */

/**
 * @file xpost_vm_image.h
 * @brief Writing a context's virtual memory out as one file.
 *
 * An image is both banks of a context's virtual memory written whole:
 * the bytes of each arena, the table that indexes it, and the arena's
 * own bookkeeping. What is left out is host state -- where this process
 * mapped the arena, which descriptor it was opened on, and the
 * functions installed in the memory file -- none of which is part of
 * what the arena holds and all of which a reader builds for itself.
 *
 * What the file does NOT hold is as much the point as what it does. The
 * operator table's rows carry the addresses of the C functions that
 * implement the operators, and those are this process's; an entity that
 * names a block held outside virtual memory names it by a handle this
 * process issued. tests/vm_host_state.register is where both are
 * written down and what a reader must do with each. This module writes
 * them as they stand, unaltered, so that a reader of a written image
 * meets them rather than a fabrication -- and so that a comparison of
 * two images reports them instead of being quietly blinded to them.
 *
 * An image is a picture of one build's memory and not a portable
 * document. The object width, the size of an entity table row and the
 * byte order are the writing build's, and the header records the first
 * two so that a reader given an image of the other object width refuses
 * it rather than reading one build's objects as another's.
 *
 * Return convention: 1 for success and 0 for failure, as the memory
 * module uses.
 */

/**
 * @typedef Xpost_Vm_Image_Bank_Field
 * @brief The arena bookkeeping an image carries, one field per name.
 *
 * These are the members of the memory file that describe the arena
 * rather than the host: what of it is in use, where the collector's
 * domain begins, what the allocator and the collector have counted, and
 * the flags a bank carries between allocations. They are written as one
 * run of values in this order, so that a reader can name each one it
 * reads back and a comparison can say which differed.
 *
 * Deliberately absent: the arena's mapped capacity and the entity
 * table's allocated capacity. Both say how much room this process asked
 * for around what the bank holds -- one is rounded by the page size and
 * a growth policy, the other doubles from a fixed start -- and a reader
 * arrives at its own for whatever it reads.
 */
typedef enum
{
    XPOST_VM_IMAGE_BANK_USED,
    XPOST_VM_IMAGE_BANK_START,
    XPOST_VM_IMAGE_BANK_NEXTENT,
    XPOST_VM_IMAGE_BANK_FREE_SUBSTACK,
    XPOST_VM_IMAGE_BANK_FREE_SCAN,
    XPOST_VM_IMAGE_BANK_PERIOD,
    XPOST_VM_IMAGE_BANK_THRESHOLD,
    XPOST_VM_IMAGE_BANK_GC_ENT_BUDGET,
    XPOST_VM_IMAGE_BANK_FILE_BIRTH_MAX,
    XPOST_VM_IMAGE_BANK_GC_AUTO,
    XPOST_VM_IMAGE_BANK_GC_PENDING,
    XPOST_VM_IMAGE_BANK_ENT_RESERVE_OPEN,
    XPOST_VM_IMAGE_BANK_ENT_EXHAUSTED,
    XPOST_VM_IMAGE_BANK_PUSH_REFUSED,
    XPOST_VM_IMAGE_BANK_FIELDS
} Xpost_Vm_Image_Bank_Field;

/**
 * @typedef Xpost_Vm_Image_Row_Field
 * @brief The fields of one entity table row, in the order written.
 */
typedef enum
{
    XPOST_VM_IMAGE_ROW_ADR,
    XPOST_VM_IMAGE_ROW_USED,
    XPOST_VM_IMAGE_ROW_SZ,
    XPOST_VM_IMAGE_ROW_MARK,
    XPOST_VM_IMAGE_ROW_TAG,
    XPOST_VM_IMAGE_ROW_FIELDS
} Xpost_Vm_Image_Row_Field;

/**
 * @def XPOST_VM_IMAGE_MAGIC
 * @brief What an image begins with, so a reader knows what it has.
 */
#define XPOST_VM_IMAGE_MAGIC "XPOSTVM\n"
#define XPOST_VM_IMAGE_MAGIC_LEN 8

/**
 * @def XPOST_VM_IMAGE_VERSION
 * @brief The layout below, which a reader must know in full.
 */
#define XPOST_VM_IMAGE_VERSION 1u

/**
 * @def XPOST_VM_IMAGE_BANKS
 * @brief Global then local, which is the order the banks are written in.
 */
#define XPOST_VM_IMAGE_BANKS 2u

/**
 * @def XPOST_VM_IMAGE_FILE_BIRTHS
 * @brief How many birth-stamp counters a bank carries.
 */
#define XPOST_VM_IMAGE_FILE_BIRTHS 256u

/**
 * @brief The name of one arena bookkeeping field.
 *
 * Answers the empty string for an index outside the set, so a reader of
 * an image written by a version it does not know cannot walk off the
 * end of the names while reporting.
 */
XPOST_TEST_VISIBLE const char *xpost_vm_image_bank_field_name(unsigned int field);

/**
 * @brief The name of one entity table row field.
 */
XPOST_TEST_VISIBLE const char *xpost_vm_image_row_field_name(unsigned int field);

/**
 * @brief The name of one bank, by its position in an image.
 */
XPOST_TEST_VISIBLE const char *xpost_vm_image_bank_name(unsigned int bank);

/**
 * @brief Write both banks of @p ctx's virtual memory to @p path.
 *
 * @param[in] ctx The context whose virtual memory is written.
 * @param[in] path Where to write it.
 * @return 1 on success, 0 on failure.
 *
 * The image is of the memory as it stands at the call. Nothing is
 * collected, moved or normalised on the way out: what a reader meets is
 * what the interpreter was running on.
 *
 * The layout, in the writing build's byte order:
 *
 *   XPOST_VM_IMAGE_MAGIC, then four values -- the version, the size of
 *   an object, the highest entity number an object can carry, and the
 *   number of banks. The two middle values are what makes an image of
 *   one object width unreadable as the other rather than silently
 *   wrong.
 *
 *   then, for each bank in turn: its name, eight bytes, padded with
 *   zeros; XPOST_VM_IMAGE_BANK_FIELDS values in the order the field
 *   enumeration gives; XPOST_VM_IMAGE_FILE_BIRTHS counters; five values
 *   for each of the bank's entity table rows, in the order the row
 *   enumeration gives; and finally the bytes of the arena, from its
 *   start to the high-water mark the bank's used field records.
 *
 * Every value is a four-byte unsigned quantity. The signed members of
 * the memory file are written as the four bytes they occupy, so a
 * reader takes them back as it stored them.
 */
XPOST_TEST_VISIBLE int xpost_vm_image_write(Xpost_Context *ctx, const char *path);

#endif
