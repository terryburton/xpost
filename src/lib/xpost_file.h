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

/**
 * @file xpost_file.h
 * @brief This file provides the Xpost functions.
 *
 * This header provides the Xpost management functions.
 * @defgroup xpost_library Library functions
 *
 * @{
 */

#ifndef XPOST_F_H
#define XPOST_F_H

#include "xpost_private.h" /* XPOST_TEST_VISIBLE */

/*
   a filetype object uses .mark_.padw to store the ent
   for the Xpost_File *

   The Xpost_File code for abstract use of files and file-like
   interfaces is a slight variation of an approach described
   by Tim Rentsch.

   Using simple inheritance (by composition), the Xpost_File*
   functions are virtualized through this vtable. With the 
   specified inlining this should result in minimal overhead
   of simple pointer indirection.

   String-backed MemoryFiles and morphism-interfaced FilteredFiles
   will implement the same virtual functions for their respective
   structures.
   */

typedef struct Xpost_File Xpost_File;

/* What a file is a filter over. A decode filter is a file over the source
   it reads; an encode filter is a file over the target it writes; a file
   that is a stream in its own right is over nothing. Which of the three
   is not a question about the coding, so it is not asked of the coding:
   the constructor a filter is born through states it, in the same call
   that names the filter's methods and hands it the stream, and the
   machinery that takes and gives up the claim on that stream reads the
   answer off the file. */
typedef enum
{
    XPOST_FILE_WRAPS_NOTHING = 0,
    XPOST_FILE_WRAPS_SOURCE,
    XPOST_FILE_WRAPS_TARGET
} Xpost_File_Wraps;

typedef struct Xpost_File_Methods
{
    int (*readch)(Xpost_File*);
    int (*writech)(Xpost_File*, int);
    int (*close)(Xpost_File*);
    int (*flush)(Xpost_File*);
    void (*purge)(Xpost_File*);
    int (*unreadch)(Xpost_File*, int);
    long (*tell)(Xpost_File*);
    int (*seek)(Xpost_File*, long);
} Xpost_File_Methods;

/* A filter holds the stream it decodes from (or encodes to) as a plain
   pointer, so that stream must outlive it however the two are closed.
   refs counts the filters holding this stream; closed records that its
   own file object has been closed. A closed stream whose refs have not
   all been released stays allocated -- its methods then report end of
   data and refuse writes -- and the last filter to release it frees it.

   owned marks a stream the file machinery made for one filter's use and
   which no program object names: the filter above it is the only thing
   that can close it, so it does, along with itself.

   ent is the file entity holding the pointer to this struct. No program
   object naming a stream does not mean no ENTITY names it -- an owned
   stream still has one, and restore's close sweep walks entities rather
   than objects, so it reaches one. Whoever frees the struct must
   therefore clear the entity first, and can only do that if the struct
   says which entity that is.

   wraps says which stream, if any, this file holds beneath it, and so
   which of the two filter bases it begins with. */
struct Xpost_File
{
    Xpost_File_Methods *methods;
    int refs;
    int closed;
    int owned;
    unsigned int ent;
    Xpost_File_Wraps wraps;
};

/* Every file subtype begins with the base, so a subtype's address and its
   base's address are the same address and the cast between them is the
   whole conversion. That is what lets the method table hold one function
   type for every subtype: a method receives the base and casts back down
   to the struct the allocation actually is. */
typedef struct Xpost_DiskFile
{
    Xpost_File methods;
    FILE *file;
    int poll_before_read; /* select() before each read: only needed for
                             pipes/terminals/sockets, where a read may block;
                             regular files are always ready */
    int input;            /* opened for reading only. flushfile means two
                             different things by the direction (PLRM 8.2) and
                             a stdio stream does not say which it is, so the
                             opener, which knows the access string, says */
} Xpost_DiskFile;

typedef struct Xpost_MemoryFile
{
    Xpost_File methods;
    unsigned char *contents;
    int is_malloc;
    int is_read;
    size_t read_next;
    size_t read_limit;
    size_t write_next;
    size_t write_capacity;
} Xpost_MemoryFile;

/* interface fgetc
   in preparation for more elaborate cross-platform non-blocking mechanisms
cf. http://stackoverflow.com/questions/20428616/how-to-handle-window-events-while-waiting-for-terminal-input
and http://stackoverflow.com/questions/25506324/how-to-do-pollstdin-or-selectstdin-when-stdin-is-a-windows-console
   */
/**
 * @brief Read a byte from an Xpost_File abstraction.
 */
static inline
int xpost_file_getc(Xpost_File *in)
{
    return in->methods->readch(in);
}

static inline
int xpost_file_putc(Xpost_File *out, int c)
{
    return out->methods->writech(out, c);
}

static inline
int xpost_file_close(Xpost_File *f)
{
    return f->methods->close(f);
}

static inline
int xpost_file_flush(Xpost_File *f)
{
    return f->methods->flush(f);
}

static inline
void xpost_file_purge(Xpost_File *f)
{
    f->methods->purge(f);
}

static inline
int xpost_file_ungetc(Xpost_File *in, int c)
{
    return in->methods->unreadch(in, c);
}

static inline
long xpost_file_tell(Xpost_File *f)
{
    return f->methods->tell(f);
}

static inline
int xpost_file_seek(Xpost_File *f, long offset)
{
    return f->methods->seek(f, offset);
}


/**
 * @brief Construct a file object given a FILE* and the direction it was
 * opened in (non-zero for a stream that is only read).
 */
Xpost_Object xpost_file_cons(Xpost_Memory_File *mem, /*@NULL@*/ const FILE *fp,
                             int input);

/**
 * @brief Construct a readable file object over a private copy of a
 * pointer and size.
 */
Xpost_Object xpost_file_cons_readstring(Xpost_Memory_File *mem, const unsigned char *ptr, unsigned int len);

/**
 * @brief Hand a synthesised stream to the filter that will wrap it.
 *
 * The file machinery makes a stream for one filter's use -- an in-memory
 * file over a copy of a string, a decoding filter that a predictor stage
 * is layered over -- and no program object names it. Marking it here is
 * what makes the wrapping filter close and free it when it closes;
 * without that the stream, and everything it holds, is unreachable and
 * unreleasable.
 */
void xpost_file_hand_over(Xpost_Memory_File *mem, Xpost_Object f);

/**
 * @brief Construct an ASCII85Decode filter file over a source file object.
 *
 * The source file is not owned: closing the filter leaves it open,
 * positioned just after the "~>" end-of-data marker once the filter
 * has been read to end of file.
 */
Xpost_Object xpost_file_cons_filter_a85(Xpost_Memory_File *mem, Xpost_Object src);

/**
 * @brief The remaining decode filter constructors: hexadecimal,
 * run-length, byte-range/delimited subfiles, and (with zlib) flate.
 * All follow the ASCII85Decode contract: read filters over an
 * unowned source.
 */
Xpost_Object xpost_file_cons_filter_hex(Xpost_Memory_File *mem, Xpost_Object src);
Xpost_Object xpost_file_cons_filter_rle(Xpost_Memory_File *mem, Xpost_Object src);
Xpost_Object xpost_file_cons_filter_subfile(Xpost_Memory_File *mem, Xpost_Object src, int count, const char *eod, int eodlen);
Xpost_Object xpost_file_cons_filter_flate(Xpost_Memory_File *mem, Xpost_Object src);
Xpost_Object xpost_file_cons_filter_dct(Xpost_Memory_File *mem, Xpost_Object src);
Xpost_Object xpost_file_cons_filter_rsd(Xpost_Memory_File *mem, Xpost_Object src);
Xpost_Object xpost_file_cons_filter_lzw(Xpost_Memory_File *mem, Xpost_Object src, int early);

/**
 * @brief undo the differencing an LZW or Flate stream was compressed with.
 *
 * Layers over the decompressing filter: predictor 2 is horizontal
 * differencing, 10 and above the PNG row filters (PLRM Table 3.20).
 */
Xpost_Object xpost_file_cons_filter_predictor(Xpost_Memory_File *mem,
                                              Xpost_Object src,
                                              int predictor, int colors,
                                              int bpc, int columns);
Xpost_Object xpost_file_cons_filter_ccitt(Xpost_Memory_File *mem, Xpost_Object src, int k, int columns, int rows, int blackis1, int byteal, int eol, int eob);
Xpost_Object xpost_file_cons_filter_enc_null(Xpost_Memory_File *mem, Xpost_Object tgt);
Xpost_Object xpost_file_cons_filter_enc_hex(Xpost_Memory_File *mem, Xpost_Object tgt);
Xpost_Object xpost_file_cons_filter_enc_a85(Xpost_Memory_File *mem, Xpost_Object tgt);
Xpost_Object xpost_file_cons_filter_enc_rle(Xpost_Memory_File *mem, Xpost_Object tgt, int recsize);
Xpost_Object xpost_file_cons_filter_enc_flate(Xpost_Memory_File *mem, Xpost_Object tgt);
Xpost_Object xpost_file_cons_filter_enc_lzw(Xpost_Memory_File *mem, Xpost_Object tgt, int early);
Xpost_Object xpost_file_cons_filter_enc_ccitt(Xpost_Memory_File *mem, Xpost_Object tgt, int k, int columns, int rows, int blackis1, int byteal, int eol, int eob);
Xpost_Object xpost_file_cons_filter_enc_dct(Xpost_Memory_File *mem, Xpost_Object tgt, int columns, int rows, int colors, double qfactor, int colortransform, const int *hsamp, const int *vsamp);
Xpost_Object xpost_file_cons_filter_eexec(Xpost_Memory_File *mem, Xpost_Object src);

/**
 * @brief The single path-to-stream opener for disk-backed files.
 *
 * Every disk file the interpreter opens passes through here, so
 * file-access policy has one enforcement point. Returns an open stream,
 * or NULL with *err set to a suitable error code. @p internal marks a
 * trusted interpreter-managed path (temporary scratch) rather than one
 * derived from the running program.
 */
FILE *xpost_diskfile_fopen(const char *path, const char *mode, int internal, int *err);

/**
 * @brief Delete @p path, subject to the file-access sandbox.
 *
 * A filesystem-control operation rather than a stream open. Under the
 * engaged sandbox @p path must be write-permitted. Returns 0 on success,
 * -1 with *err set otherwise.
 */
int xpost_diskfile_remove(const char *path, int *err);

/**
 * @brief Rename @p oldpath to @p newpath, subject to the sandbox.
 *
 * Under the engaged sandbox both paths must be write-permitted. Returns 0
 * on success, -1 with *err set otherwise.
 */
int xpost_diskfile_rename(const char *oldpath, const char *newpath, int *err);

/**
 * @brief May the running program see @p path (to open or enumerate it)?
 *
 * True when the sandbox is not engaged or @p path is read-permitted. Used
 * to filter directory enumeration to the visible files.
 */
int xpost_diskfile_readable(const char *path);

/**
 * @brief Has the file-access sandbox been engaged?
 */
int xpost_path_control_is_engaged(void);

/**
 * @brief Validate that s[0..len) is a safe single path component.
 *
 * Rejects path separators, ':' , NUL and control bytes, '.' and '..', a
 * leading dot or space, a trailing dot or space, and reserved device
 * names, so an externally-derived name cannot express a path. Returns 1
 * if safe, 0 otherwise.
 */
int xpost_path_safe_leaf(const char *s, size_t len);

/**
 * @brief Open @p rel for reading beneath directory @p root.
 *
 * The operating system confines resolution to @p root (no escape via ".."
 * or a symlink). @p rel should already be composed of safe leaves. Returns
 * an open stream, or NULL with *err set.
 */
FILE *xpost_diskfile_fopen_beneath(const char *root, const char *rel, int *err);

/**
 * @brief Open and construct a file object given filename and mode.
 */
XPOST_TEST_VISIBLE int xpost_file_open(Xpost_Memory_File *mem, char *fn, char *mode, Xpost_Object *retval);

/**
 * @brief Return the FILE* from the file object.
 */
Xpost_File *xpost_file_get_file_pointer(Xpost_Memory_File *mem, Xpost_Object f);

/**
 * @brief Get the status of the file object.
 */
int xpost_file_get_status(Xpost_Memory_File *mem, Xpost_Object f);

/**
 * @brief Return number of bytes available to read.
 */
int xpost_file_get_bytes_available(Xpost_Memory_File *mem, Xpost_Object f, int *retval);

/**
 * @brief Close the file and deallocate the descriptor in VM.
 */
int xpost_file_object_close(Xpost_Memory_File *mem, Xpost_Object f);
int xpost_file_object_close_at_eod(Xpost_Memory_File *mem, Xpost_Object f);

/**
 * @brief Return the entity of the stream a file wraps, or zero for none.
 */
unsigned int xpost_file_underlying_entity(Xpost_Memory_File *mem, unsigned int ent);

/**
 * @brief Release the stream an entity holds, naming it by entity alone.
 */
void xpost_file_release_entity(Xpost_Memory_File *mem, unsigned int ent);

int xpost_file_read(char *buf, int size, int count, Xpost_File *fp);
int xpost_file_write(const char *buf, int size, int count, Xpost_File *fp);
FILE *xpost_file_stdio_stream_get(Xpost_File *fp);

/**
 * @brief Read a byte from file object.
 */
Xpost_Object xpost_file_read_byte(Xpost_Memory_File *mem, Xpost_Object f);

/**
 * @brief Write a byte to a file object.
 */
int xpost_file_write_byte(Xpost_Memory_File *mem, Xpost_Object f, Xpost_Object b);

/**
 * @}
 */


int xpost_diskfile_stat(const char *path, long *pages, long *bytes, long *referred, long *created);

#endif
