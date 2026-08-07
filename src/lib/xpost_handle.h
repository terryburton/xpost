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

#ifndef XPOST_HANDLE_H
#define XPOST_HANDLE_H

#include <stddef.h>

#include "xpost_memory.h" /* Xpost_Memory_File */
#include "xpost_object.h" /* Xpost_Object */
#include "xpost_context.h" /* Xpost_Context */

/*
 * Where C-level state that a dictionary names lives.
 *
 * Some of what a dictionary stands for is a struct of pointers and
 * counts held outside virtual memory. A device's instance state is: it
 * names raster memory, which is not part of VM (PLRM 3.7.3), so a
 * `restore` reaches neither a page's pixels nor a writer's accumulated
 * content. A font's is: it names the face the font program was opened
 * as, which the font machinery holds for the process. What the
 * dictionary holds under its key is a handle on the block rather than
 * the block itself.
 *
 * Such a dictionary is an ordinary dictionary and what it holds under
 * any key is whatever was last stored there, so the value a reader is
 * handed is the program's to choose. A handle is resolved here against
 * the record of what was issued, and one naming no live block, or a
 * block of another kind, or a block of another size, resolves to
 * nothing and the reader reports rather than follows it. A handle is
 * read out of the entity carrying it and checked back against that
 * entity, so a copy of a genuine handle names the block no more than a
 * string of the program's own does.
 *
 * A kind and a size together say what a block holds, so a block issued
 * for one purpose does not resolve for another that happens to want a
 * struct of the same width.
 *
 * Where the block was issued to is recorded as well, and whether it is
 * asked for bears on what a reader may do. A device's instance state is
 * reached only through the instance it was issued to. A font
 * dictionary is copied -- scalefont and makefont copy one, and a
 * re-encoded copy of a findfont dictionary shares its face -- so one
 * face is reached through many dictionaries, and only a release of the
 * face is held to the dictionary it was issued to.
 *
 * A block outlives the device's Destroy, which releases the resources
 * the struct names and stores the cleared struct back. It is released
 * when the entity carrying its handle is reclaimed: that entity is
 * marked in the memory table, so the collector's sweep, the entity
 * reclaimer and the memory file's teardown each reach it -- the three
 * points at which a file's struct is likewise given up.
 */

/**
 * @brief Memory-table tag bit marking an entity as a handle on a block.
 *
 * It sits above the fields an object tag uses, so an entity carries it
 * alongside the type the allocator recorded.
 */
#define XPOST_MEMORY_TABLE_TAG_HANDLE 0x80000000u

/**
 * @brief What a block holds.
 */
typedef enum
{
    XPOST_HANDLE_DEVICE = 1, /**< a device's instance state */
    XPOST_HANDLE_FONT        /**< a font's face */
} Xpost_Handle_Kind;

/**
 * @brief Issue a block of the given kind and store its handle in the
 * dictionary under key.
 *
 * The block is zeroed. Returns 0, or an error code.
 */
int xpost_handle_cons(Xpost_Context *ctx,
                      Xpost_Object dic,
                      Xpost_Object key,
                      Xpost_Object *anchor,
                      Xpost_Handle_Kind kind,
                      size_t size);

/**
 * @brief The block a handle names, or NULL where it names none of this
 * kind and size.
 */
XPOST_NOINLINE
void *xpost_handle_block(Xpost_Context *ctx,
                         Xpost_Object anchor,
                         Xpost_Handle_Kind kind,
                         size_t size);

/**
 * @brief The block a handle names, where it was issued to this
 * dictionary; NULL otherwise.
 */
XPOST_NOINLINE
void *xpost_handle_block_of(Xpost_Context *ctx,
                            Xpost_Object anchor,
                            Xpost_Object dic,
                            Xpost_Handle_Kind kind,
                            size_t size);

/**
 * @brief Give up the block an entity's handle names.
 */
void xpost_handle_release_entity(Xpost_Memory_File *mem,
                                 unsigned int ent);

/**
 * @brief Give up every block issued into a memory file.
 */
void xpost_handle_release_memory_file(Xpost_Memory_File *mem);

#endif
