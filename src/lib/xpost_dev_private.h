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

#ifndef XPOST_DEV_PRIVATE_H
#define XPOST_DEV_PRIVATE_H

#include <stddef.h>

#include "xpost_memory.h" /* Xpost_Memory_File */
#include "xpost_object.h" /* Xpost_Object */
#include "xpost_context.h" /* Xpost_Context */

/*
 * Where a device's C-level instance state lives.
 *
 * A device's instance state is a struct of pointers and counts, and it
 * is held outside virtual memory: raster memory is not part of VM
 * (PLRM 3.7.3), so a `restore` reaches neither a page's pixels nor a
 * writer's accumulated content. What the instance dictionary holds
 * under its private key is a handle on the block rather than the block
 * itself. An instance dictionary is an ordinary dictionary and what it
 * holds under any key is whatever was last stored there, so the value a
 * device is handed is the program's to choose; a handle is resolved
 * here against the record of which block was issued to which instance,
 * and one naming no live block, or a block issued to another instance,
 * or a block of another size, resolves to nothing and the device
 * reports rather than follows it.
 *
 * A block outlives the device's Destroy, which releases the resources
 * the struct names and stores the cleared struct back. It is released
 * when the entity carrying its handle is reclaimed: that entity is
 * marked in the memory table, so the collector's sweep, the entity
 * reclaimer and the memory file's teardown each reach it -- the three
 * points at which a file's struct is likewise given up.
 */

/**
 * @brief Memory-table tag bit marking an entity as a device's private
 * handle.
 *
 * It sits above the fields an object tag uses, so an entity carries it
 * alongside the type the allocator recorded.
 */
#define XPOST_MEMORY_TABLE_TAG_DEVICE_PRIVATE 0x80000000u

/**
 * @brief Issue a block of device state and store its handle in the
 * instance dictionary under key.
 *
 * The block is zeroed. Returns 0, or an error code.
 */
int xpost_dev_private_cons(Xpost_Context *ctx,
                           Xpost_Object devdic,
                           Xpost_Object key,
                           Xpost_Object *anchor,
                           size_t size);

/**
 * @brief The block a handle names, or NULL where it names none.
 */
XPOST_NOINLINE
void *xpost_dev_private_block(Xpost_Context *ctx,
                              Xpost_Object anchor,
                              size_t size);

/**
 * @brief The block a handle names, where it was issued to this instance
 * dictionary; NULL otherwise.
 */
XPOST_NOINLINE
void *xpost_dev_private_block_of(Xpost_Context *ctx,
                                 Xpost_Object anchor,
                                 Xpost_Object devdic,
                                 size_t size);

/**
 * @brief Give up the block an entity's handle names.
 */
void xpost_dev_private_release_entity(Xpost_Memory_File *mem,
                                      unsigned int ent);

/**
 * @brief Give up every block issued into a memory file.
 */
void xpost_dev_private_release_memory_file(Xpost_Memory_File *mem);

#endif
