/* Two fresh boots of the interpreter, and what their virtual memory has
 * in common.
 *
 * A memory image written at start-up and read back in a later process
 * would be worth having only if what it holds is a function of the
 * language rather than of the run that produced it. The gate for that is
 * idempotence -- write an image, read it, write another, require the two
 * to match to the byte -- and the gate rests on something nobody had
 * measured: that two fresh boots produce the same virtual memory in the
 * first place. This is that measurement.
 *
 * The image is taken where a run brings its context up: after the
 * language has loaded and been locked down, and before the run's device
 * is made. That is the last point at which nothing of the run has been
 * decided.
 *
 * WHAT THIS ESTABLISHES. Run as a pair of processes, it says whether
 * their virtual memory differs, and where. It holds the two to being
 * identical in every part that carries no host address at all: the size
 * of each bank, the bookkeeping each carries, every row of both entity
 * tables, and every byte of local memory. In the global bank, where the
 * operator table's rows carry the addresses of the C functions that
 * implement the operators, it holds every difference to being one of
 * those addresses: a word whose two values keep the same offset within
 * a page and differ by the one distance between where the two processes
 * put this code. Anything else -- a count, an index, a length, an
 * ordering, storage nobody wrote -- fails that, because none of those
 * moves by a page-aligned distance and none of them keeps its page
 * offset.
 *
 * WHAT IT DOES NOT ESTABLISH, plainly:
 *
 *   It is a comparison and not a proof of what an image would need. Two
 *   runs that agree say nothing about a third under different arguments,
 *   a different device or a different data directory. The pair here is
 *   deliberately identical in all of those, so what is measured is the
 *   process and nothing else.
 *
 *   It does not say the differing words are unreachable. It says only
 *   that each is where an operator's signature keeps one of this
 *   process's own functions. A word that differs anywhere else is a
 *   failure, whether or not anything can still read it: storage handed
 *   out to a new occupant is cleared of what the last one left, so a
 *   host address outside a live signature has nowhere to have come from
 *   that this run understands.
 *
 *   It says nothing about a context created a second time in a process
 *   that already had one. That is a different question -- a boot from a
 *   live heap rather than a fresh one -- and it is not what an image
 *   would be written from.
 *
 *   It is blind where the two runs put this code at the same address:
 *   the difference it exists to characterise is then not there to see.
 *   That case is recognised rather than passed over -- the whole of both
 *   images is then required to match to the byte, which is the stronger
 *   answer -- and the run says which of the two it gave.
 *
 *   An image of one object width is not comparable with one of the
 *   other: the objects in it are a different size and the structures
 *   around them a different shape. Two images are refused unless they
 *   agree with each other and with this build.
 *
 * Modes:
 *   write <path>        boot to the point above and write the image
 *   compare <a> <b>     read two images and judge them
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stddef.h>
#include <stdint.h>
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

/* Floors. A comparison that finds no operator signature to read is
   reading something other than an image of this interpreter, and would
   report nothing wrong about it. */
#define SIGNATURES_AT_LEAST 100
#define FUNCTIONS_AT_LEAST 50

/* How much of a difference to spell out before the report is just
   volume. The counts above it are the whole population either way. */
#define DETAIL_LINES 8

typedef struct
{
    char name[16];
    unsigned int field[XPOST_VM_IMAGE_BANK_FIELDS];
    const unsigned char *rows;   /* nextent * XPOST_VM_IMAGE_ROW_FIELDS */
    const unsigned char *arena;  /* used bytes */
} Bank;

typedef struct
{
    unsigned char *bytes;
    size_t len;
    unsigned int version;
    unsigned int object_size;
    unsigned int ent_max;
    unsigned int banks;
    Bank bank[XPOST_VM_IMAGE_BANKS];
} Image;

/* Every number in an image is four bytes at whatever offset it fell on:
   an arena is as long as it is, so the bank after one begins wherever
   that leaves off. Read by copy rather than through a pointer of the
   type. */
static unsigned int _u32(const unsigned char *p)
{
    unsigned int v;

    memcpy(&v, p, sizeof v);
    return v;
}

static unsigned int _row(const Bank *b, unsigned int ent, unsigned int field)
{
    return _u32(b->rows + (ent * XPOST_VM_IMAGE_ROW_FIELDS + field)
                          * sizeof(unsigned int));
}

static unsigned int _bank_used(const Bank *b)
{
    return b->field[XPOST_VM_IMAGE_BANK_USED];
}

static unsigned int _bank_nextent(const Bank *b)
{
    return b->field[XPOST_VM_IMAGE_BANK_NEXTENT];
}

/* An address the arena holds, at an offset the caller has already
   established is inside it. */
static uintptr_t _host_address(const unsigned char *arena, unsigned int off)
{
    uintptr_t v;

    memcpy(&v, arena + off, sizeof v);
    return v;
}

static int _read_image(const char *path, Image *im)
{
    FILE *f;
    long len;
    size_t got;
    size_t at;
    unsigned int i;

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
    if (!im->bytes)
    {
        report_failure("cannot hold the image %s in memory", path);
        fclose(f);
        return 0;
    }
    got = fread(im->bytes, 1, im->len, f);
    fclose(f);
    if (got != im->len)
    {
        report_failure("the image %s is shorter than it says", path);
        return 0;
    }

    at = XPOST_VM_IMAGE_MAGIC_LEN + 4 * sizeof(unsigned int);
    if (im->len < at ||
        memcmp(im->bytes, XPOST_VM_IMAGE_MAGIC, XPOST_VM_IMAGE_MAGIC_LEN) != 0)
    {
        report_failure("%s does not begin as an image of virtual memory", path);
        return 0;
    }
    im->version = _u32(im->bytes + XPOST_VM_IMAGE_MAGIC_LEN);
    im->object_size = _u32(im->bytes + XPOST_VM_IMAGE_MAGIC_LEN + 4);
    im->ent_max = _u32(im->bytes + XPOST_VM_IMAGE_MAGIC_LEN + 8);
    im->banks = _u32(im->bytes + XPOST_VM_IMAGE_MAGIC_LEN + 12);

    if (im->version != XPOST_VM_IMAGE_VERSION)
    {
        report_failure("%s is version %u of the layout and this reads "
                       "version %u", path, im->version,
                       (unsigned int)XPOST_VM_IMAGE_VERSION);
        return 0;
    }
    if (im->banks != XPOST_VM_IMAGE_BANKS)
    {
        report_failure("%s holds %u banks and this reads %u", path,
                       im->banks, (unsigned int)XPOST_VM_IMAGE_BANKS);
        return 0;
    }
    /* The comparison below reads the operator table out of the arena as
       this build's structures. An image of the other object width is a
       different shape and would be read as nonsense rather than
       refused. */
    if (im->object_size != (unsigned int)sizeof(Xpost_Object))
    {
        report_failure("%s was written by a build whose objects are %u bytes "
                       "and this build's are %u; an image of one object width "
                       "is not comparable with the other",
                       path, im->object_size,
                       (unsigned int)sizeof(Xpost_Object));
        return 0;
    }

    for (i = 0; i < XPOST_VM_IMAGE_BANKS; i++)
    {
        Bank *b = &im->bank[i];
        unsigned int j;
        size_t rows;

        if (im->len < at + 8 + XPOST_VM_IMAGE_BANK_FIELDS * sizeof(unsigned int)
                        + XPOST_VM_IMAGE_FILE_BIRTHS * sizeof(unsigned int))
        {
            report_failure("%s ends in the middle of the %s bank", path,
                           xpost_vm_image_bank_name(i));
            return 0;
        }
        memcpy(b->name, im->bytes + at, 8);
        b->name[8] = '\0';
        at += 8;
        for (j = 0; j < XPOST_VM_IMAGE_BANK_FIELDS; j++)
        {
            b->field[j] = _u32(im->bytes + at);
            at += sizeof(unsigned int);
        }
        at += XPOST_VM_IMAGE_FILE_BIRTHS * sizeof(unsigned int);

        rows = (size_t)_bank_nextent(b) * XPOST_VM_IMAGE_ROW_FIELDS
               * sizeof(unsigned int);
        if (im->len < at + rows + _bank_used(b))
        {
            report_failure("%s says its %s bank holds %u entities over %u "
                           "bytes and the file is too short for them",
                           path, xpost_vm_image_bank_name(i),
                           _bank_nextent(b), _bank_used(b));
            return 0;
        }
        b->rows = im->bytes + at;
        at += rows;
        b->arena = im->bytes + at;
        at += _bank_used(b);
    }

    if (at != im->len)
    {
        report_failure("%s carries %lu bytes past what it describes", path,
                       (unsigned long)(im->len - at));
        return 0;
    }
    return 1;
}

/* Where a live operator signature is: the operator table is a special
   entity of global memory whose rows each name a run of signatures by
   its address in the arena. The row's recorded size is zeroed so the
   collector passes it over, so how much of the arena it covers is read
   from what it was allocated. */
typedef struct
{
    unsigned int adr;
    unsigned int opcode;
} Signature;

static unsigned int _signatures(const Bank *g, Signature *sig,
                                unsigned int max)
{
    unsigned int extent;
    unsigned int base;
    unsigned int nops;
    unsigned int k;
    unsigned int n = 0;

    if (_bank_nextent(g) <= XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE)
        return 0;
    base = _row(g, XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE,
                XPOST_VM_IMAGE_ROW_ADR);
    extent = _row(g, XPOST_MEMORY_TABLE_SPECIAL_OPERATOR_TABLE,
                  XPOST_VM_IMAGE_ROW_USED);
    if (extent == 0 || base + extent > _bank_used(g))
        return 0;

    nops = extent / (unsigned int)sizeof(Xpost_Operator);
    for (k = 0; k < nops && n < max; k++)
    {
        unsigned int at = base + k * (unsigned int)sizeof(Xpost_Operator);
        int count;
        unsigned int adr;
        int s;

        memcpy(&count, g->arena + at + offsetof(Xpost_Operator, n),
               sizeof count);
        memcpy(&adr, g->arena + at + offsetof(Xpost_Operator, sigadr),
               sizeof adr);
        if (count <= 0 || adr == 0)
            continue;
        for (s = 0; s < count && n < max; s++)
        {
            unsigned int one = adr + (unsigned int)s
                                     * (unsigned int)sizeof(Xpost_Signature);

            if (one + sizeof(Xpost_Signature) > _bank_used(g))
                break;
            sig[n].adr = one;
            sig[n].opcode = k;
            n++;
        }
    }
    return n;
}

/* Which signature covers an offset, or none. The runs come out in
   opcode order and an operator's signatures move as they are added to,
   so the addresses do not arrive sorted and the search is a walk. */
static const Signature *_signature_at(const Signature *sig, unsigned int n,
                                      unsigned int off)
{
    unsigned int i;

    for (i = 0; i < n; i++)
        if (off >= sig[i].adr && off < sig[i].adr + sizeof(Xpost_Signature))
            return &sig[i];
    return NULL;
}

/* Which entity's allocation covers an offset, or none. */
static int _entity_at(const Bank *b, unsigned int off, unsigned int *ent)
{
    unsigned int e;

    for (e = 0; e < _bank_nextent(b); e++)
    {
        unsigned int adr = _row(b, e, XPOST_VM_IMAGE_ROW_ADR);
        unsigned int used = _row(b, e, XPOST_VM_IMAGE_ROW_USED);
        unsigned int sz = _row(b, e, XPOST_VM_IMAGE_ROW_SZ);
        unsigned int extent = sz > used ? sz : used;

        if (extent && off >= adr && off < adr + extent)
        {
            *ent = e;
            return 1;
        }
    }
    return 0;
}

/* A word carrying a host address that the two runs put in different
   places differs by the distance between those places. Where the low
   bytes of the word have since been written over -- with the same bytes
   in both runs, or the word would not agree in them -- what is left of
   the difference is that distance with those bytes taken off it, give
   or take a carry out of them.
 */
static int _slide_explains(uintptr_t a, uintptr_t b, uintptr_t slide)
{
    uintptr_t d = a - b;
    unsigned int k;

    for (k = 0; k < sizeof(uintptr_t); k++)
    {
        uintptr_t unit = (uintptr_t)1 << (8 * k);
        uintptr_t top = (slide / unit) * unit;

        if (d == top || d == top + unit || d == top - unit)
            return 1;
    }
    return 0;
}

/* A relocation moves an image by whole pages, so an address it moved
   keeps its offset within one. Four kilobytes is the smallest page any
   platform this builds for has; a larger one only makes this hold more
   firmly. */
#define PAGE_OFFSET_MASK 0xfffu

static void _compare_banks(const Image *a, const Image *b)
{
    unsigned int i;

    for (i = 0; i < XPOST_VM_IMAGE_BANKS; i++)
    {
        const Bank *x = &a->bank[i];
        const Bank *y = &b->bank[i];
        const char *who = xpost_vm_image_bank_name(i);
        unsigned int j;
        unsigned int ndiff = 0;

        if (memcmp(x->name, y->name, 8) != 0)
            report_failure("the two images name their bank %u differently: "
                           "%s and %s", i, x->name, y->name);

        for (j = 0; j < XPOST_VM_IMAGE_BANK_FIELDS; j++)
            if (x->field[j] != y->field[j])
                report_failure("%s bank: %s is %u in one boot and %u in the "
                               "other", who,
                               xpost_vm_image_bank_field_name(j),
                               x->field[j], y->field[j]);

        if (_bank_nextent(x) != _bank_nextent(y))
            continue;   /* the rows are not the same population to compare */

        for (j = 0; j < _bank_nextent(x); j++)
        {
            unsigned int f;

            for (f = 0; f < XPOST_VM_IMAGE_ROW_FIELDS; f++)
            {
                if (_row(x, j, f) == _row(y, j, f))
                    continue;
                ndiff++;
                if (ndiff <= DETAIL_LINES)
                    report_failure("%s bank: entity %u has %s %u in one boot "
                                   "and %u in the other", who, j,
                                   xpost_vm_image_row_field_name(f),
                                   _row(x, j, f), _row(y, j, f));
            }
        }
        if (ndiff > DETAIL_LINES)
            report_failure("%s bank: %u entity table fields differ in all",
                           who, ndiff);
    }
}

/* The local bank carries no operator table and nothing else that names
   a host address, so nothing in it may differ at all. */
static void _compare_local(const Image *a, const Image *b)
{
    const Bank *x = &a->bank[1];
    const Bank *y = &b->bank[1];
    unsigned int used = _bank_used(x);
    unsigned int off;
    unsigned int ndiff = 0;

    if (used != _bank_used(y))
        return;     /* already reported as a field difference */

    for (off = 0; off < used; off++)
        if (x->arena[off] != y->arena[off])
        {
            ndiff++;
            if (ndiff <= DETAIL_LINES)
                report_failure("local bank: byte %u is %02x in one boot and "
                               "%02x in the other", off,
                               x->arena[off], y->arena[off]);
        }
    if (ndiff > DETAIL_LINES)
        report_failure("local bank: %u bytes differ in all, where nothing in "
                       "local memory names this process", ndiff);
    if (ndiff == 0)
        printf("local bank: %u bytes, identical in both boots\n", used);
}

static void _compare_global(const Image *a, const Image *b)
{
    const Bank *x = &a->bank[0];
    const Bank *y = &b->bank[0];
    unsigned int used = _bank_used(x);
    Signature *sig;
    unsigned int nsig;
    unsigned int i;
    unsigned int nfp = 0;
    uintptr_t slide = 0;
    int slide_known = 0;
    int slide_disagreed = 0;
    unsigned int off;
    unsigned int ndiff = 0;
    unsigned int nword = 0;
    unsigned int n_fp = 0;
    unsigned int n_check = 0;
    unsigned int n_residue = 0;
    unsigned int reported = 0;
    unsigned int nseen = 0;

    if (used != _bank_used(y))
        return;     /* already reported as a field difference */

    sig = malloc(sizeof *sig * 65536);
    if (!sig)
    {
        report_failure("cannot hold the operator table's signatures");
        return;
    }
    nsig = _signatures(x, sig, 65536);

    /* A reading that finds no signature would report nothing wrong about
       an image full of host addresses. */
    if (nsig < SIGNATURES_AT_LEAST)
    {
        report_failure("only %u operator signatures were found in the image; "
                       "this comparison is not reading the operator table and "
                       "would report nothing about what it carries", nsig);
        free(sig);
        return;
    }

    /* The distance between where the two runs put this code, read off
       the one thing in the image that is unambiguously a host address:
       the function implementing an operator. Every one of them must
       give the same answer, or they are not all the same kind of
       thing. */
    for (i = 0; i < nsig; i++)
    {
        unsigned int at = sig[i].adr + (unsigned int)offsetof(Xpost_Signature, fp);
        uintptr_t fa = _host_address(x->arena, at);
        uintptr_t fb = _host_address(y->arena, at);

        /* an operator whose body is a procedure rather than a C
           function states its operands and carries no function */
        if (fa == 0 && fb == 0)
            continue;
        nfp++;
        if (!slide_known)
        {
            slide = fa - fb;
            slide_known = 1;
        }
        else if (fa - fb != slide)
            slide_disagreed++;
    }

    if (nfp < FUNCTIONS_AT_LEAST)
    {
        report_failure("only %u of %u signatures carry an operator function; "
                       "there is nothing here to read this process's own "
                       "addresses from", nfp, nsig);
        free(sig);
        return;
    }
    if (slide_disagreed)
        report_failure("%u of %u operator functions moved between the two "
                       "boots by a different distance from the rest; the "
                       "operator table does not hold one relocated image",
                       slide_disagreed, nfp);

    /* The operands a signature states are the language's and not this
       process's: they must be the same whatever the addresses beside
       them do. */
    for (i = 0; i < nsig; i++)
    {
        unsigned int in_at = sig[i].adr + (unsigned int)offsetof(Xpost_Signature, in);
        unsigned int t_at = sig[i].adr + (unsigned int)offsetof(Xpost_Signature, t);

        if (memcmp(x->arena + in_at, y->arena + in_at,
                   sizeof(((Xpost_Signature *)0)->in)) != 0 ||
            memcmp(x->arena + t_at, y->arena + t_at,
                   sizeof(((Xpost_Signature *)0)->t)) != 0)
        {
            if (nseen++ < DETAIL_LINES)
                report_failure("the signature at %u of the operator at opcode "
                               "%u states different operands in the two boots",
                               sig[i].adr, sig[i].opcode);
        }
    }
    if (nseen > DETAIL_LINES)
        report_failure("%u signatures state different operands in the two "
                       "boots", nseen);

    if (slide == 0)
    {
        /* The two runs put this code in the same place, so the
           difference this exists to characterise is not there to be
           seen -- and the whole of the image has to match instead. */
        for (off = 0; off < used; off++)
            if (x->arena[off] != y->arena[off])
            {
                ndiff++;
                if (ndiff <= DETAIL_LINES)
                    report_failure("global bank: byte %u is %02x in one boot "
                                   "and %02x in the other, where the two boots "
                                   "put this process's code at the same "
                                   "address and nothing may differ", off,
                                   x->arena[off], y->arena[off]);
            }
        if (ndiff > DETAIL_LINES)
            report_failure("global bank: %u bytes differ in all", ndiff);
        if (ndiff == 0)
            printf("global bank: %u bytes, identical in both boots (the two "
                   "boots loaded this process's code at the same address, so "
                   "this run does not speak for what a relocated one would "
                   "differ in)\n", used);
        free(sig);
        return;
    }

    /* Every difference has to be a host address that moved with the
       rest. The arena is read a word at a time rather than a byte at a
       time: an address is written and read whole, so a byte of one that
       differs is a byte of the word it belongs to, and the word is what
       there is anything to say about. */
    for (off = 0; off + sizeof(uintptr_t) <= used; off += sizeof(uintptr_t))
    {
        unsigned int at = off;
        unsigned int k;
        uintptr_t va;
        uintptr_t vb;
        const Signature *s;

        if (memcmp(x->arena + at, y->arena + at, sizeof(uintptr_t)) == 0)
            continue;
        for (k = 0; k < sizeof(uintptr_t); k++)
            if (x->arena[at + k] != y->arena[at + k])
                ndiff++;

        nword++;
        va = _host_address(x->arena, at);
        vb = _host_address(y->arena, at);

        s = _signature_at(sig, nsig, at);
        if (s)
        {
            unsigned int within = at - s->adr;

            if (within == offsetof(Xpost_Signature, fp))
                n_fp++;
            else if (within == offsetof(Xpost_Signature, checkstack))
                n_check++;
            else
                report_failure("global bank: the signature at %u of the "
                               "operator at opcode %u differs %u bytes in, "
                               "where neither of its host functions lies",
                               s->adr, s->opcode, within);
        }
        else
            n_residue++;

        if ((va & PAGE_OFFSET_MASK) != (vb & PAGE_OFFSET_MASK))
        {
            if (reported++ < DETAIL_LINES)
                report_failure("global bank: the word at %u holds %llx in one "
                               "boot and %llx in the other, which do not share "
                               "an offset within a page and so are not one "
                               "address relocated", at,
                               (unsigned long long)va, (unsigned long long)vb);
            continue;
        }
        if (!_slide_explains(va, vb, slide))
        {
            if (reported++ < DETAIL_LINES)
                report_failure("global bank: the word at %u holds %llx in one "
                               "boot and %llx in the other, a difference the "
                               "distance between the two loads (%llx) does not "
                               "account for", at,
                               (unsigned long long)va, (unsigned long long)vb,
                               (unsigned long long)slide);
        }
    }

    /* The arena ends where the last allocation ended, which need not be
       a whole number of words. Nothing an address is written in reaches
       into that tail -- every allocation begins on a word boundary --
       so a difference there is not one of these. */
    for (off = used - (used % (unsigned int)sizeof(uintptr_t));
         off < used; off++)
        if (x->arena[off] != y->arena[off])
        {
            ndiff++;
            report_failure("global bank: byte %u differs, in the tail past "
                           "the last whole word of the arena, where no host "
                           "address is written", off);
        }

    printf("global bank: %u of %u bytes differ, in %u words: %u an operator's "
           "function, %u a signature's stack check, %u in storage no live "
           "signature covers\n",
           ndiff, used, nword, n_fp, n_check, n_residue);
    printf("global bank: %u signatures read from the operator table, %u of "
           "them carrying an operator function; the two boots put this "
           "process's code %llx apart\n",
           nsig, nfp, (unsigned long long)slide);
    if (n_residue)
        report_failure("global bank: %u differing word(s) lie in no live "
                       "operator signature. They are host addresses left in "
                       "storage that has been handed out again -- the part of "
                       "an allocation its occupant does not cover -- which is "
                       "cleared as the storage is handed out, so there is "
                       "nowhere for one to be", n_residue);

    /* Say where a few of them are, so the report names storage rather
       than only counting it. */
    reported = 0;
    for (off = 0; off + sizeof(uintptr_t) <= used && reported < DETAIL_LINES;
         off += sizeof(uintptr_t))
    {
        unsigned int at = off;
        unsigned int ent;

        if (memcmp(x->arena + at, y->arena + at, sizeof(uintptr_t)) == 0)
            continue;
        if (_signature_at(sig, nsig, at))
            continue;
        if (_entity_at(x, at, &ent))
            printf("NOTE:   the word at %u is %u bytes into entity %u, which "
                   "was allocated %u bytes and holds %u\n", at,
                   at - _row(x, ent, XPOST_VM_IMAGE_ROW_ADR), ent,
                   _row(x, ent, XPOST_VM_IMAGE_ROW_SZ),
                   _row(x, ent, XPOST_VM_IMAGE_ROW_USED));
        else
            printf("NOTE:   the word at %u lies in no entity of the table\n",
                   at);
        reported++;
    }

    free(sig);
}

static void _write(const char *path)
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
        return;
    }

    /* The language, and then nothing: the run's device is what would be
       made next, and it is what an image must not carry. */
    xpost_interpreter_load_language(ctx);

    if (!xpost_vm_image_write(ctx, path))
        report_failure("cannot write the image to %s", path);

    xpost_destroy(ctx);
    xpost_quit();
}

static void _compare(const char *pa, const char *pb)
{
    Image a;
    Image b;

    if (!_read_image(pa, &a) || !_read_image(pb, &b))
    {
        free(a.bytes);
        free(b.bytes);
        return;
    }

    if (a.object_size != b.object_size || a.ent_max != b.ent_max)
    {
        report_failure("the two images were written by builds of different "
                       "object widths (%u/%u bytes, %u/%u entity numbers) and "
                       "are not comparable",
                       a.object_size, b.object_size, a.ent_max, b.ent_max);
        free(a.bytes);
        free(b.bytes);
        return;
    }
    if (a.len != b.len)
        report_failure("the two boots wrote %lu and %lu bytes of virtual "
                       "memory", (unsigned long)a.len, (unsigned long)b.len);

    _compare_banks(&a, &b);
    _compare_local(&a, &b);
    _compare_global(&a, &b);

    free(a.bytes);
    free(b.bytes);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "write") == 0)
    {
        _write(argv[2]);
        return verdict();
    }
    if (argc == 4 && strcmp(argv[1], "compare") == 0)
    {
        _compare(argv[2], argv[3]);
        return verdict();
    }

    report_failure("this was asked for something other than `write <path>` "
                   "or `compare <a> <b>`, and so measured nothing");
    return verdict();
}
