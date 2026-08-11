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

/* Bringing an interpreter up out of an image of virtual memory, and what
 * has to hold for that to be worth doing.
 *
 * The thing an image can do that nothing raises is dispatch to the wrong
 * operator. An operator object carries the number of its table row and
 * nothing else, so an image read into a table whose rows came out in
 * another order runs the wrong code and reports nothing at all. Every
 * other way an image can be wrong announces itself; that one does not.
 * So the read holds the two tables to each other by name, row for row,
 * and this is what says the holding works -- by handing the read an
 * image whose names have been swapped and requiring it to say no.
 *
 * WHAT THIS ESTABLISHES.
 *
 *   Idempotence. A context brought up out of an image writes back the
 *   image it read, to the byte. That is the gate an image has to pass:
 *   a wrong one is a silent wrong answer rather than a crash, so what is
 *   required is not that the interpreter still runs but that the memory
 *   it runs on is the memory the image describes. The two runs are
 *   separate processes, so an image that carried anything of the process
 *   that wrote it would come back different.
 *
 *   Refusal. Every way an image can be unusable ends with the language
 *   built from the boot files instead: the stamps at its head each
 *   damaged in turn, its operator names permuted, its entity table
 *   pointed past its arena, and the file itself cut short, lengthened or
 *   made unrecognisable. Each is required to leave a working
 *   interpreter that says it built the language rather than read it.
 *
 * WHAT IT DOES NOT ESTABLISH.
 *
 *   That an interpreter out of an image behaves as one that booted. That
 *   is the whole suite's question, asked by running the whole suite with
 *   an image in use, and no single test answers it.
 *
 *   That a stamp catches what it names. A stamp is damaged here by
 *   changing the value in the file, which says the comparison happens
 *   and not that the value it compares is the right value to compare.
 *
 * Modes:
 *   boot                 bring a context up as a run does and say
 *                        which way the language arrived; the
 *                        environment says which image to read and
 *                        which to write
 *   damages              how many ways an image can be damaged, and
 *                        what each is called
 *   damage <in> <out> <n>  copy an image, damaging it the nth way
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_context.h"
#include "xpost_interpreter.h"
#include "xpost_operator.h"
#include "xpost_vm_image.h"

#include "xpost_test.h"

/* What the runner reads to tell one boot from the other. */
#define SAID_READ "the language was read from an image"
#define SAID_BUILT "the language was built from the boot files"

static unsigned int _u32(const unsigned char *p)
{
    unsigned int v;

    memcpy(&v, p, sizeof v);
    return v;
}

static void _put_u32(unsigned char *p, unsigned int v)
{
    memcpy(p, &v, sizeof v);
}

/*
 *
 * Bringing a context up, the way a run does.
 *
 */

static void _boot(void)
{
    Xpost_Context *ctx;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return;
    }
    ctx = xpost_create("null", XPOST_OUTPUT_DEFAULT, NULL,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, 100, 100);
    if (!ctx)
    {
        report_failure("xpost_create");
        xpost_quit();
        return;
    }

    /* The language, and then nothing: the run's device is what would be
       made next, and it is what an image must not carry. */
    xpost_interpreter_load_language(ctx);

    printf("%s\n", xpost_vm_image_in_use() ? SAID_READ : SAID_BUILT);

    /* Whatever image this run was told to write has been written by
       now, at the point the interpreter writes one: inside the language
       load, after the language stands and before this run has settled
       anything of its own. Nothing here writes one, so that what is
       compared is the file the interpreter itself produces. */

    /* The interpreter has to be one that runs, whichever way it came up:
       an image read into a context that cannot then execute anything
       would pass a comparison of bytes and be worthless. */
    if (xpost_run(ctx, XPOST_INPUT_STRING,
                  "2 3 add 5 ne { (arithmetic\n) print flush } if\n", 0)
        != XPOST_RUN_COMPLETE)
        report_failure("the interpreter would not run a program");

    xpost_destroy(ctx);
    xpost_quit();
}

/*
 *
 * Damaging an image.
 *
 */

/* Where each part of an image begins, for a damage that has to reach
   into one. Everything up to the banks is a run of fixed-width values
   and length-prefixed names, so it is walked rather than indexed. */
typedef struct
{
    unsigned char *bytes;
    size_t len;
    unsigned int stamp[XPOST_VM_IMAGE_STAMPS];
    size_t operators;       /* the first operator row */
    size_t bank[XPOST_VM_IMAGE_BANKS]; /* each bank's name */
} Image;

static int _read(const char *path, Image *im)
{
    FILE *f;
    long len;
    size_t at;
    unsigned int i;
    unsigned int b;

    memset(im, 0, sizeof *im);
    f = fopen(path, "rb");
    if (!f)
    {
        report_failure("cannot open the image %s", path);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0)
    {
        report_failure("cannot measure the image %s", path);
        fclose(f);
        return 0;
    }
    im->len = (size_t)len;
    im->bytes = malloc(im->len ? im->len : 1);
    if (!im->bytes || fread(im->bytes, 1, im->len, f) != im->len)
    {
        report_failure("cannot read the image %s", path);
        fclose(f);
        return 0;
    }
    fclose(f);

    at = XPOST_VM_IMAGE_MAGIC_LEN;
    if (im->len < at + XPOST_VM_IMAGE_STAMPS * sizeof(unsigned int))
    {
        report_failure("%s is too short to be an image", path);
        return 0;
    }
    for (i = 0; i < XPOST_VM_IMAGE_STAMPS; i++, at += sizeof(unsigned int))
        im->stamp[i] = _u32(im->bytes + at);

    im->operators = at;
    for (i = 0; i < im->stamp[XPOST_VM_IMAGE_STAMP_OPERATORS]; i++)
    {
        unsigned int namelen;

        if (im->len < at + 2 * sizeof(unsigned int))
        {
            report_failure("%s ends among its operator names", path);
            return 0;
        }
        namelen = _u32(im->bytes + at + sizeof(unsigned int));
        at += 2 * sizeof(unsigned int) + namelen + (4u - (namelen % 4u)) % 4u;
    }
    at += im->stamp[XPOST_VM_IMAGE_STAMP_CONTEXT_FIELDS] * sizeof(unsigned int);
    at += (size_t)(im->stamp[XPOST_VM_IMAGE_STAMP_ROOTS]
                   + im->stamp[XPOST_VM_IMAGE_STAMP_TYPENAMES])
          * sizeof(Xpost_Object);

    for (b = 0; b < XPOST_VM_IMAGE_BANKS; b++)
    {
        unsigned int used;
        unsigned int nextent;

        if (im->len < at + 8 + (XPOST_VM_IMAGE_BANK_FIELDS
                                + XPOST_VM_IMAGE_FILE_BIRTHS)
                              * sizeof(unsigned int))
        {
            report_failure("%s ends in the middle of the %s bank", path,
                           xpost_vm_image_bank_name(b));
            return 0;
        }
        im->bank[b] = at;
        at += 8;
        used = _u32(im->bytes + at
                    + XPOST_VM_IMAGE_BANK_USED * sizeof(unsigned int));
        nextent = _u32(im->bytes + at
                       + XPOST_VM_IMAGE_BANK_NEXTENT * sizeof(unsigned int));
        at += (XPOST_VM_IMAGE_BANK_FIELDS + XPOST_VM_IMAGE_FILE_BIRTHS)
              * sizeof(unsigned int);
        at += (size_t)nextent * XPOST_VM_IMAGE_ROW_FIELDS * sizeof(unsigned int);
        at += used;
    }
    if (at != im->len)
    {
        report_failure("%s carries %lu bytes past what it describes", path,
                       (unsigned long)(im->len - at));
        return 0;
    }
    return 1;
}

/* The first row of a bank's entity table. */
static size_t _rows(const Image *im, unsigned int bank)
{
    return im->bank[bank] + 8
           + (XPOST_VM_IMAGE_BANK_FIELDS + XPOST_VM_IMAGE_FILE_BIRTHS)
             * sizeof(unsigned int);
}

/* Swap the names of two operator rows, leaving everything else where it
   was. Two of the same length, so that nothing after them moves and the
   only difference between the two files is which operator each row says
   it is: an image that says row seven is the operator row eight used to
   be, and nothing else. */
static int _swap_two_names(Image *im)
{
    unsigned int n = im->stamp[XPOST_VM_IMAGE_STAMP_OPERATORS];
    unsigned char tmp[256];
    size_t at = im->operators;
    size_t first = 0;
    unsigned int firstlen = 0;
    unsigned int i;

    for (i = 0; i < n; i++)
    {
        unsigned int namelen = _u32(im->bytes + at + sizeof(unsigned int));
        size_t name = at + 2 * sizeof(unsigned int);

        if (namelen && namelen == firstlen &&
            memcmp(im->bytes + first, im->bytes + name, namelen) != 0)
        {
            memcpy(tmp, im->bytes + first, namelen);
            memcpy(im->bytes + first, im->bytes + name, namelen);
            memcpy(im->bytes + name, tmp, namelen);
            return 1;
        }
        if (namelen && namelen <= sizeof tmp)
        {
            first = name;
            firstlen = namelen;
        }
        at = name + namelen + (4u - (namelen % 4u)) % 4u;
    }
    report_failure("this image has no two operators named alike in length");
    return 0;
}

/* Every way an image is damaged here. The stamps are damaged one at a
   time, by name, so that a stamp added to the head of an image without a
   comparison behind it fails this rather than passing unnoticed. */
typedef enum
{
    DAMAGE_MAGIC,
    DAMAGE_NAMES,
    DAMAGE_ROW,
    DAMAGE_SHORT,
    DAMAGE_LONG,
    DAMAGE_FIXED
} Damage;

#define DAMAGES (DAMAGE_FIXED + XPOST_VM_IMAGE_STAMPS)

static const char *_damage_name(unsigned int which)
{
    static char buf[64];

    switch (which)
    {
        case DAMAGE_MAGIC: return "what an image begins with";
        case DAMAGE_NAMES: return "two operator names, swapped";
        case DAMAGE_ROW: return "an entity pointed past its arena";
        case DAMAGE_SHORT: return "an image cut short";
        case DAMAGE_LONG: return "an image with a byte on the end";
        default: break;
    }
    snprintf(buf, sizeof buf, "the stamp for %s",
             xpost_vm_image_stamp_name(which - DAMAGE_FIXED));
    return buf;
}

static void _damages(void)
{
    unsigned int i;

    printf("%u\n", (unsigned int)DAMAGES);
    for (i = 0; i < DAMAGES; i++)
        printf("%s\n", _damage_name(i));
}

static void _damage(const char *in, const char *out, unsigned int which)
{
    Image im;
    FILE *f;
    size_t len;

    if (which >= DAMAGES)
    {
        report_failure("there is no %uth way to damage an image", which);
        return;
    }
    if (!_read(in, &im))
    {
        free(im.bytes);
        return;
    }
    len = im.len;

    switch (which)
    {
        case DAMAGE_MAGIC:
            im.bytes[0] ^= 0xff;
            break;
        case DAMAGE_NAMES:
            if (!_swap_two_names(&im))
            {
                free(im.bytes);
                return;
            }
            break;
        case DAMAGE_ROW:
            /* the first entity of global memory, told it lies past the
               end of the arena it is in */
            _put_u32(im.bytes + _rows(&im, 0)
                     + XPOST_VM_IMAGE_ROW_ADR * sizeof(unsigned int),
                     0xffff0000u);
            break;
        case DAMAGE_SHORT:
            if (len < 64)
            {
                report_failure("the image is too short to cut short");
                free(im.bytes);
                return;
            }
            len -= 64;
            break;
        case DAMAGE_LONG:
            im.bytes[0] = im.bytes[0];  /* the byte goes on below */
            break;
        default:
            _put_u32(im.bytes + XPOST_VM_IMAGE_MAGIC_LEN
                     + (which - DAMAGE_FIXED) * sizeof(unsigned int),
                     im.stamp[which - DAMAGE_FIXED] ^ 0x5a5a5a5au);
            break;
    }

    f = fopen(out, "wb");
    if (!f)
    {
        report_failure("cannot open %s to write a damaged image to", out);
        free(im.bytes);
        return;
    }
    if (fwrite(im.bytes, 1, len, f) != len ||
        (which == DAMAGE_LONG && fputc(0, f) == EOF) ||
        fclose(f) != 0)
        report_failure("cannot write a damaged image to %s", out);
    free(im.bytes);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "boot") == 0)
    {
        _boot();
        return verdict();
    }
    if (argc == 2 && strcmp(argv[1], "damages") == 0)
    {
        _damages();
        return verdict();
    }
    if (argc == 5 && strcmp(argv[1], "damage") == 0)
    {
        _damage(argv[2], argv[3], (unsigned int)strtoul(argv[4], NULL, 10));
        return verdict();
    }

    report_failure("this was asked for something other than `boot`, "
                   "`damages` or `damage <in> <out> <n>`, and so measured "
                   "nothing");
    return verdict();
}
