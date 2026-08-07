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

#include <stdlib.h> /* NULL strtod */
#include <stddef.h>

#include <assert.h>
#include <ctype.h> /* isdigit, isxdigit, isspace */
#include <math.h> /* sqrt */
#include <stdio.h>
#include <string.h>

#include "xpost.h"
#include "xpost_log.h"
#include "xpost_memory.h"
#include "xpost_object.h"
#include "xpost_stack.h"
#include "xpost_font.h"
#include "xpost_strbuf.h"
#include "xpost_file.h"
#include "xpost_save.h"
#include "xpost_context.h"
#include "xpost_error.h"
#include "xpost_name.h"
#include "xpost_string.h"
#include "xpost_array.h"
#include "xpost_dict.h"

//#include "xpost_interpreter.h"
#include "xpost_operator.h"
#include "xpost_op_font.h"
#include "xpost_dev_generic.h" /* pdfwrite accumulator access for glyph outlines */
#include "xpost_dev_driver.h" /* the device grid the clip region's bounds sit on */

/*
 * FIXME: check if we can factorize show, ashow and kshow a bit.
 * These codes seem quite similar
 */

typedef struct fontdata
{
    void *face;
} fontdata;

/* One pixel-row band of the clip region: the columns [lo, hi) of row
   band the region covers. This is the region as .regionmeet resolves
   it, read back from the array the clip's cache holder keeps. */
typedef struct clipband
{
    int band;
    int lo, hi;
} clipband;

/* How the clip region in force meets device pixels. The glyph raster
   route paints pixels straight through the device, so it meets the
   region itself rather than through the fill pipeline; what it meets is
   the same set of pixels, worked out by .showclip in data/font.ps and
   left in the clip's own cache holder. */
enum
{
    CLIP_ALL,    /* nothing worked out: the raster is not narrowed */
    CLIP_BOX,    /* the region is a rectangle: the pixel bounds below */
    CLIP_BANDS   /* the region resolved to the bands below */
};

/* per-text-operator rendering configuration, gathered once from the
   font dictionary, the device dictionary and the graphics state */
typedef struct textstate
{
    Xpost_Object encoding;  /* the font's /Encoding array, or invalid */
    Xpost_Object charstrings; /* the font's /CharStrings dict, or invalid */
    Xpost_Object metrics;   /* the font's /Metrics dict, or invalid */
    real cdmat[4];          /* character space -> device space (FontMatrix o CTM) */
    int cdmat_ok;           /* the matrix above is usable */
    Xpost_Object blendpix;  /* the device's BlendPix method, or invalid */
    int blend;              /* anti-alias: TextAlphaBits > 1 and BlendPix present */
    int vector;             /* the device consumes glyph outlines, not bitmaps */
    int extents;            /* the device consumes glyph ink extents, not marks */
    Xpost_Object fillrect;  /* the device's FillRect, for extent reporting */
    int sepindex;           /* separation registered with the device, or -1 */
    double septint;         /* the separation's tint */
    int clipkind;           /* one of the CLIP_ constants above */
    int cx0, cy0, cx1, cy1; /* the region's pixel bounds, half-open */
    const clipband *bands;  /* the region's bands, ascending, when CLIP_BANDS */
    int nbands;
} textstate;

/* The linear part of a six-element matrix object: the four numbers that
   map a direction, ahead of the translation the last two describe. Every
   element is written before any is read. */
static
void _matrix_linear_part(Xpost_Context *ctx, Xpost_Object psmat, real m[4])
{
    m[0] = xpost_object_number(xpost_array_get(ctx, psmat, 0));
    m[1] = xpost_object_number(xpost_array_get(ctx, psmat, 1));
    m[2] = xpost_object_number(xpost_array_get(ctx, psmat, 2));
    m[3] = xpost_object_number(xpost_array_get(ctx, psmat, 3));
}

/* the linear part of character space -> device space: the font
   dictionary's FontMatrix composed with the CTM (row convention:
   x' = e0 x + e2 y, y' = e1 x + e3 y). Returns 0 when either matrix
   is unusable. */
static
int _char_device_matrix(Xpost_Context *ctx,
                        Xpost_Object gs,
                        Xpost_Object fontdict,
                        real e[4])
{
    Xpost_Object psmat;
    real fm[4] = { 1.0, 0.0, 0.0, 1.0 };
    real cm[4];
    int i;

    psmat = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "FontMatrix"));
    if (xpost_object_get_type(psmat) == arraytype && psmat.comp_.sz == 6)
    {
        for (i = 0; i < 4; i++)
        {
            Xpost_Object el = xpost_array_get(ctx, psmat, i);
            if (xpost_object_get_type(el) == realtype)
                fm[i] = el.real_.val;
            else if (xpost_object_get_type(el) == integertype)
                fm[i] = (real)el.int_.val;
        }
    }
    psmat = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currmatrix"));
    if (xpost_object_get_type(psmat) != arraytype || psmat.comp_.sz != 6)
        return 0;
    _matrix_linear_part(ctx, psmat, cm);
    e[0] = fm[0] * cm[0] + fm[1] * cm[2];
    e[1] = fm[0] * cm[1] + fm[1] * cm[3];
    e[2] = fm[2] * cm[0] + fm[3] * cm[2];
    e[3] = fm[2] * cm[1] + fm[3] * cm[3];
    return 1;
}

/* the glyph name the font's /Encoding assigns a character code, or the
   invalid object (codes past the array, or entries that are not names) */
static
Xpost_Object _encoded_name(Xpost_Context *ctx,
                           Xpost_Object encoding,
                           unsigned int ch)
{
    if (xpost_object_get_type(encoding) == arraytype
     && ch < (unsigned int)encoding.comp_.sz)
    {
        Xpost_Object en = xpost_array_get(ctx, encoding, ch);
        if (xpost_object_get_type(en) == nametype)
            return en;
    }
    return invalid;
}

/* A /Metrics entry for this glyph name overrides its width (PLRM 5.9.2):
   a number is a new x width, a two-element array carries the width in its
   second element, a four-element array carries the width vector in its
   last two. The values are in character space; deliver the device-space
   advance in 16.16, y-up, the convention the face's advances arrive in.
   (The sidebearing the array forms also carry is not applied.) */
static
int _metrics_advance(Xpost_Context *ctx,
                     const textstate *ts,
                     Xpost_Object glyphname,
                     long *ax,
                     long *ay)
{
    Xpost_Object v;
    real wx, wy = 0.0;

    if (!ts->cdmat_ok
     || xpost_object_get_type(ts->metrics) != dicttype
     || xpost_object_get_type(glyphname) != nametype)
        return 0;
    v = xpost_dict_get(ctx, ts->metrics, glyphname);
    if (xpost_object_get_type(v) == integertype)
        wx = (real)v.int_.val;
    else if (xpost_object_get_type(v) == realtype)
        wx = v.real_.val;
    else if (xpost_object_get_type(v) == arraytype
          && (v.comp_.sz == 2 || v.comp_.sz == 4))
    {
        Xpost_Object el = xpost_array_get(ctx, v, v.comp_.sz == 2 ? 1 : 2);
        if (xpost_object_get_type(el) == realtype)
            wx = el.real_.val;
        else if (xpost_object_get_type(el) == integertype)
            wx = (real)el.int_.val;
        else
            return 0;
        if (v.comp_.sz == 4)
        {
            el = xpost_array_get(ctx, v, 3);
            if (xpost_object_get_type(el) == realtype)
                wy = el.real_.val;
            else if (xpost_object_get_type(el) == integertype)
                wy = (real)el.int_.val;
            else
                return 0;
        }
    }
    else
        return 0;
    *ax = (long)((ts->cdmat[0] * wx + ts->cdmat[2] * wy) * 65536.0);
    *ay = (long)(-(ts->cdmat[1] * wx + ts->cdmat[3] * wy) * 65536.0);
    return 1;
}

/* Extract the CharStrings of a Type 1 font program on disk: the
   values a font dictionary built by running the program would hold,
   the charstring bytes as the RD procedure reads them, still under
   their own charstring encryption. PFB segment headers unwrap, the
   eexec layer decrypts from its hexadecimal or raw form, and the
   entries parse as /name length RD <bytes> ND, whatever pair of
   names the program chose for RD and ND. Returns a read-only
   dictionary in global VM, or the invalid object. */
static Xpost_Object
_t1_charstrings_from_file(Xpost_Context *ctx, const char *path)
{
    Xpost_Object result = null;
    unsigned char *raw = NULL, *flat = NULL, *plain = NULL;
    size_t rawlen = 0, flatlen = 0, plainlen = 0;
    size_t i, ee;
    int ferrcode = 0;
    FILE *fp;

    fp = xpost_diskfile_fopen(path, "rb", 1, &ferrcode);
    if (!fp)
        return null;
    fseek(fp, 0, SEEK_END);
    {
        long l = ftell(fp);

        if (l <= 0 || l > (16L << 20))
        {
            fclose(fp);
            return null;
        }
        rawlen = (size_t)l;
    }
    fseek(fp, 0, SEEK_SET);
    raw = malloc(rawlen);
    if (!raw || fread(raw, 1, rawlen, fp) != rawlen)
    {
        free(raw);
        fclose(fp);
        return null;
    }
    fclose(fp);

    if (raw[0] == 0x80)
    {
        /* PFB: 0x80, type, little-endian length, payload; type 3 ends */
        size_t off = 0;

        flat = malloc(rawlen);
        if (!flat)
            goto out;
        while (off + 6 <= rawlen && raw[off] == 0x80 && raw[off + 1] != 3)
        {
            size_t seg = (size_t)raw[off + 2]
                       | ((size_t)raw[off + 3] << 8)
                       | ((size_t)raw[off + 4] << 16)
                       | ((size_t)raw[off + 5] << 24);

            off += 6;
            if (off + seg > rawlen)
                goto out;
            memcpy(flat + flatlen, raw + off, seg);
            flatlen += seg;
            off += seg;
        }
    }
    else
    {
        flat = raw;
        flatlen = rawlen;
        raw = NULL;
    }

    /* the encrypted portion follows the eexec token's white space */
    for (ee = 0; ee + 5 < flatlen; ee++)
        if (memcmp(flat + ee, "eexec", 5) == 0)
            break;
    if (ee + 5 >= flatlen)
        goto out;
    ee += 5;
    while (ee < flatlen && (flat[ee] == '\r' || flat[ee] == '\n'
                         || flat[ee] == ' ' || flat[ee] == '\t'))
        ee++;
    {
        int ishex = 1;
        unsigned short r = 55665;
        size_t n = 0;

        for (i = 0; i < 4 && ee + i < flatlen; i++)
            if (!isxdigit(flat[ee + i]))
                ishex = 0;
        plain = malloc(flatlen);
        if (!plain)
            goto out;
        if (ishex)
        {
            int hi = -1;

            for (i = ee; i < flatlen; i++)
            {
                int c = flat[i], v;

                if (isdigit(c)) v = c - '0';
                else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                else continue;
                if (hi < 0)
                    hi = v;
                else
                {
                    plain[n++] = (unsigned char)((hi << 4) | v);
                    hi = -1;
                }
            }
        }
        else
        {
            memcpy(plain, flat + ee, flatlen - ee);
            n = flatlen - ee;
        }
        for (i = 0; i < n; i++)
        {
            unsigned char c = plain[i];

            plain[i] = (unsigned char)(c ^ (r >> 8));
            r = (unsigned short)((unsigned int)(c + r) * 52845u + 22719u);
        }
        if (n <= 4)
            goto out;
        memmove(plain, plain + 4, n - 4);
        plainlen = n - 4;
    }

    /* /CharStrings, then entries until the closing end */
    for (i = 0; i + 12 < plainlen; i++)
        if (memcmp(plain + i, "/CharStrings", 12) == 0)
            break;
    if (i + 12 >= plainlen)
        goto out;
    i += 12;
    {
        unsigned int oldmode = ctx->vmmode;
        Xpost_Object csdict;
        int entries = 0;

        ctx->vmmode = GLOBAL;
        csdict = xpost_dict_cons(ctx, 256);
        while (i < plainlen && entries < 20000)
        {
            char namebuf[128];
            size_t nb = 0;
            long len = 0;

            while (i < plainlen && plain[i] != '/')
            {
                if (i + 3 < plainlen && memcmp(plain + i, "end", 3) == 0
                 && (i == 0 || isspace(plain[i - 1]))
                 && (i + 3 == plainlen || isspace(plain[i + 3])))
                    goto done;
                i++;
            }
            if (i >= plainlen)
                break;
            i++;
            while (i < plainlen && !isspace(plain[i]) && plain[i] != '('
                && plain[i] != '/' && plain[i] != '{'
                && nb + 1 < sizeof namebuf)
                namebuf[nb++] = (char)plain[i++];
            namebuf[nb] = 0;
            while (i < plainlen && isspace(plain[i]))
                i++;
            while (i < plainlen && isdigit(plain[i]))
                len = len * 10 + (plain[i++] - '0');
            while (i < plainlen && isspace(plain[i]))
                i++;
            while (i < plainlen && !isspace(plain[i]))
                i++;                       /* the RD name of the day */
            i++;                           /* the single separator */
            if (len <= 0 || len > 65535 || i + (size_t)len > plainlen
             || nb == 0)
                break;
            {
                Xpost_Object str = xpost_string_cons(ctx, (unsigned int)len,
                                                     (char *)plain + i);

                str = xpost_object_set_access(ctx, str,
                          XPOST_OBJECT_TAG_ACCESS_EXECUTE_ONLY);
                if (xpost_dict_put(ctx, csdict, xpost_name_cons(ctx, namebuf),
                                   str))
                    break;
            }
            i += (size_t)len;
            entries++;
        }
done:
        if (entries > 0)
        {
            csdict = xpost_object_set_access(ctx, csdict,
                          XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
            result = csdict;
        }
        ctx->vmmode = oldmode;
    }
out:
    free(raw);
    free(flat);
    free(plain);
    return result;
}

/* CFF INDEX and DICT walking, enough to reach the CharStrings INDEX
   of a bare CFF or an OpenType CFF table: the glyph names come from
   the face, so neither the charset nor the string index is read. */
static unsigned long
_cff_u(const unsigned char *p, int n)
{
    unsigned long v = 0;
    int i;

    for (i = 0; i < n; i++)
        v = (v << 8) | p[i];
    return v;
}

/* an INDEX at off: sets *count and *first (offset of the offset
   array's data area); returns the offset just past the INDEX, or 0 */
static size_t
_cff_index(const unsigned char *d, size_t len, size_t off,
           unsigned long *count, size_t *dataoff, int *offsz)
{
    unsigned long c;
    int osz;
    unsigned long last;

    if (off + 2 > len)
        return 0;
    c = _cff_u(d + off, 2);
    if (c == 0)
    {
        *count = 0;
        return off + 2;
    }
    if (off + 3 > len)
        return 0;
    osz = d[off + 2];
    if (osz < 1 || osz > 4)
        return 0;
    if (off + 3 + (c + 1) * osz > len)
        return 0;
    last = _cff_u(d + off + 3 + c * osz, osz);
    *count = c;
    *offsz = osz;
    *dataoff = off + 3 + (c + 1) * osz - 1;
    if (*dataoff + last > len)
        return 0;
    return *dataoff + last;
}

/* the CharStrings offset out of the first Top DICT (operator 17) */
static unsigned long
_cff_charstrings_offset(const unsigned char *d, size_t len)
{
    unsigned long count;
    size_t dataoff = 0, off;
    int osz = 0;
    size_t dstart, dend;
    double operands[48];
    int nops = 0;

    if (len < 4)
        return 0;
    off = d[2];                          /* header size */
    off = _cff_index(d, len, off, &count, &dataoff, &osz);   /* Name */
    if (!off)
        return 0;
    if (_cff_index(d, len, off, &count, &dataoff, &osz) == 0 || count == 0)
        return 0;                                            /* Top DICT */
    dstart = dataoff + _cff_u(d + off + 3, osz);
    dend = dataoff + _cff_u(d + off + 3 + osz, osz);
    while (dstart < dend && dstart < len)
    {
        int b = d[dstart];

        if (b <= 21)
        {
            int op = b;

            dstart++;
            if (b == 12)
            {
                if (dstart >= len)
                    return 0;
                op = 1200 + d[dstart];
                dstart++;
            }
            if (op == 17 && nops >= 1)
                return (unsigned long)operands[nops - 1];
            nops = 0;
        }
        else if (b >= 32 && b <= 246)
        {
            if (nops < 48) operands[nops++] = b - 139;
            dstart++;
        }
        else if (b >= 247 && b <= 250)
        {
            if (dstart + 1 >= len)   /* the operand's trailing byte */
                return 0;
            if (nops < 48) operands[nops++] =
                (b - 247) * 256 + d[dstart + 1] + 108;
            dstart += 2;
        }
        else if (b >= 251 && b <= 254)
        {
            if (dstart + 1 >= len)
                return 0;
            if (nops < 48) operands[nops++] =
                -((int)(b - 251) * 256) - (int)d[dstart + 1] - 108;
            dstart += 2;
        }
        else if (b == 28)
        {
            if (dstart + 2 >= len)   /* two trailing operand bytes */
                return 0;
            if (nops < 48) operands[nops++] =
                (short)_cff_u(d + dstart + 1, 2);
            dstart += 3;
        }
        else if (b == 29)
        {
            if (dstart + 4 >= len)   /* four trailing operand bytes */
                return 0;
            if (nops < 48) operands[nops++] =
                (long)_cff_u(d + dstart + 1, 4);
            dstart += 5;
        }
        else if (b == 30)
        {
            /* a real number: nibbles to the stop code */
            dstart++;
            while (dstart < len)
            {
                int lo = d[dstart] & 15, hi = d[dstart] >> 4;

                dstart++;
                if (hi == 15 || lo == 15)
                    break;
            }
            if (nops < 48) operands[nops++] = 0;
        }
        else
            return 0;
    }
    return 0;
}

/* Publish the CharStrings of a CFF-backed face: the Type 2
   charstring of every glyph, keyed by the face's glyph names. A bare
   CFF file reads whole; an OpenType wrapper locates its CFF table.
   Returns a read-only dictionary in global VM, or null. */
static Xpost_Object
_cff_charstrings_from_file(Xpost_Context *ctx, const char *path, void *face)
{
    Xpost_Object result = null;
    unsigned char *raw = NULL;
    const unsigned char *cff;
    size_t rawlen = 0, cfflen;
    unsigned long csoff, count;
    size_t dataoff = 0;
    int osz = 0;
    int ferrcode = 0;
    FILE *fp;

    fp = xpost_diskfile_fopen(path, "rb", 1, &ferrcode);
    if (!fp)
        return null;
    fseek(fp, 0, SEEK_END);
    {
        long l = ftell(fp);

        if (l <= 0 || l > (32L << 20))
        {
            fclose(fp);
            return null;
        }
        rawlen = (size_t)l;
    }
    fseek(fp, 0, SEEK_SET);
    raw = malloc(rawlen);
    if (!raw || fread(raw, 1, rawlen, fp) != rawlen)
    {
        free(raw);
        fclose(fp);
        return null;
    }
    fclose(fp);

    cff = raw;
    cfflen = rawlen;
    if (rawlen > 12 && memcmp(raw, "OTTO", 4) == 0)
    {
        unsigned long ntab = _cff_u(raw + 4, 2), t;

        cff = NULL;
        for (t = 0; t < ntab && 12 + t * 16 + 16 <= rawlen; t++)
        {
            const unsigned char *e = raw + 12 + t * 16;

            if (memcmp(e, "CFF ", 4) == 0)
            {
                unsigned long toff = _cff_u(e + 8, 4);
                unsigned long tlen = _cff_u(e + 12, 4);

                /* keep the table within the file, without a 32-bit
                   toff+tlen wrap */
                if (toff <= rawlen && tlen <= rawlen - toff)
                {
                    cff = raw + toff;
                    cfflen = tlen;
                }
                break;
            }
        }
        if (!cff)
            goto out;
    }
    else if (raw[0] != 1)     /* not a bare CFF either */
        goto out;

    csoff = _cff_charstrings_offset(cff, cfflen);
    if (!csoff || _cff_index(cff, cfflen, csoff, &count, &dataoff, &osz) == 0
     || count == 0)
        goto out;

    {
        unsigned int oldmode = ctx->vmmode;
        Xpost_Object csdict;
        unsigned long gid;
        int entries = 0;

        ctx->vmmode = GLOBAL;
        csdict = xpost_dict_cons(ctx, count < 4096 ? (unsigned int)count : 4096);
        for (gid = 0; gid < count && gid < 65535; gid++)
        {
            char nbuf[128];
            unsigned long a = _cff_u(cff + csoff + 3 + gid * osz, osz);
            unsigned long b = _cff_u(cff + csoff + 3 + (gid + 1) * osz, osz);

            /* keep the glyph's data span within the font buffer: interior
               INDEX offsets are otherwise unchecked, so an oversized or
               non-monotonic offset must not address memory outside it */
            if (b <= a || b - a > 65535 || b > cfflen - dataoff)
                continue;
            if (!xpost_font_face_glyph_name_get(face, (unsigned int)gid,
                                                nbuf, sizeof nbuf))
                continue;
            {
                Xpost_Object str = xpost_string_cons(ctx,
                    (unsigned int)(b - a), (char *)cff + dataoff + a);

                str = xpost_object_set_access(ctx, str,
                          XPOST_OBJECT_TAG_ACCESS_EXECUTE_ONLY);
                if (xpost_dict_put(ctx, csdict, xpost_name_cons(ctx, nbuf), str))
                    break;
                entries++;
            }
        }
        if (entries > 0)
        {
            csdict = xpost_object_set_access(ctx, csdict,
                          XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
            result = csdict;
        }
        ctx->vmmode = oldmode;
    }
out:
    free(raw);
    return result;
}

static
int _findfont(Xpost_Context *ctx,
              Xpost_Object fontname)
{
#ifdef HAVE_FREETYPE2
    Xpost_Object fontstr;
    Xpost_Object fontdict;
    Xpost_Object privatestr;
    struct fontdata data;
    char *fname;
    Xpost_Object fontbbox;
    Xpost_Object fontbboxarray[4];
    Xpost_Object sfnts_obj = null;
    int istt = 0;
    int cffreal = 0;
    int ret;

    if (xpost_object_get_type(fontname) == nametype)
        fontstr = xpost_name_get_string(ctx, fontname);
    else
        fontstr = fontname;
    fname = xpost_string_allocate_cstring(ctx, fontstr);

    fontdict = xpost_dict_cons (ctx, 10);
    privatestr = xpost_string_cons(ctx, sizeof data, NULL);
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "Private"), privatestr);
    if (ret)
    {
        free(fname);
        return ret;
    }
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "FontName"), fontname);
    if (ret)
    {
        free(fname);
        return ret;
    }

    /* initialize font data, with x-scale and y-scale set to 1.
       Faces are cached per name: each face maps the font file and
       holds FreeType state, so creating one per findfont grows the
       process by a mapping per lookup. The face is shared between
       font dictionaries exactly as a FontDirectory-cached dictionary
       already shares it. */
    {
        static struct { char *name; void *face; Xpost_Object charstrings;
                        Xpost_Object sfnts; char *file; int csreal; }
            face_cache[32];
        static int face_cache_n = 0;
        int fi, slot = -1;
        data.face = NULL;
        for (fi = 0; fi < face_cache_n; fi++)
        {
            if (strcmp(face_cache[fi].name, fname) == 0)
            {
                data.face = face_cache[fi].face;
                slot = fi;
                break;
            }
        }
        if (data.face == NULL)
        {
            data.face = xpost_font_face_new_from_name(fname);
            if (data.face != NULL && face_cache_n < 32)
            {
                const char *ff = xpost_font_face_last_file();

                face_cache[face_cache_n].file = ff ? strdup(ff) : NULL;
                face_cache[face_cache_n].sfnts = null;
                face_cache[face_cache_n].name = strdup(fname);
                face_cache[face_cache_n].face = data.face;
                slot = face_cache_n++;
            }
        }
        if (data.face == NULL){
            free(fname);
            return invalidfont;
        }

        /* a base font publishes its glyph complement: programs size
           tables from /CharStrings, test membership with known, and
           re-encode from its keys. Synthesize the name-to-glyph-index
           dictionary from the face's glyph names, once per face and
           shared read-only between every dictionary the name produces,
           in global VM so a restore cannot unwind it from under the
           cache. The values are glyph indices, which the text
           machinery accepts directly; a face without glyph names
           publishes nothing. */
        istt = xpost_font_face_is_truetype(data.face);
        /* a TrueType-backed dictionary is a Type 42 font outright:
           publish the program as sfnts, chunked under the string
           limit, read once per face and shared between every
           dictionary the name produces. Only a plain sfnt file
           qualifies: a compressed wrapper or a collection is not
           the program a Type 42 dictionary carries, and such a
           face keeps the Type 1 presentation */
        if (istt && slot >= 0)
        {
            if (xpost_object_get_type(face_cache[slot].sfnts) != arraytype
             && face_cache[slot].file)
            {
                int ferrcode = 0;
                FILE *fp = xpost_diskfile_fopen(face_cache[slot].file,
                                                "rb", 1, &ferrcode);

                if (fp)
                {
                    long len;
                    unsigned char magic[4] = { 0, 0, 0, 0 };
                    int plain;

                    plain = fread(magic, 1, 4, fp) == 4
                         && ((magic[0] == 0 && magic[1] == 1
                           && magic[2] == 0 && magic[3] == 0)
                          || memcmp(magic, "true", 4) == 0);
                    fseek(fp, 0, SEEK_END);
                    len = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    if (plain && len > 0)
                    {
                        int nchunks = (int)((len + 65531) / 65532);
                        unsigned int oldmode = ctx->vmmode;
                        Xpost_Object arr;
                        int ci;
                        unsigned char *cbuf = malloc(65532);

                        ctx->vmmode = GLOBAL;
                        arr = xpost_array_cons(ctx, nchunks);
                        for (ci = 0; ci < nchunks && cbuf; ci++)
                        {
                            long rem = len - (long)ci * 65532;
                            size_t want = rem > 65532 ? 65532 : (size_t)rem;
                            Xpost_Object str;

                            if (fread(cbuf, 1, want, fp) != want)
                                break;
                            str = xpost_string_cons(ctx, (unsigned int)want,
                                                    (char *)cbuf);
                            if (xpost_array_put(ctx, arr, ci, str) != 0)
                                break;
                        }
                        free(cbuf);
                        if (cbuf && ci == nchunks)
                            face_cache[slot].sfnts = arr;
                        ctx->vmmode = oldmode;
                    }
                    fclose(fp);
                }
            }
            if (xpost_object_get_type(face_cache[slot].sfnts) != arraytype)
                istt = 0;
            sfnts_obj = face_cache[slot].sfnts;
        }

        if (slot >= 0
         && xpost_object_get_type(face_cache[slot].charstrings) == dicttype)
        {
            if (face_cache[slot].csreal && xpost_font_face_is_cff(data.face))
                cffreal = 1;
            ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "CharStrings"),
                               face_cache[slot].charstrings);
            if (ret)
            {
                free(fname);
                return ret;
            }
        }
        else
        {
            unsigned int nglyphs;

            /* a Type 1 program on disk yields the genuine article:
               the charstring bytes its RD procedure reads, so the
               dictionary holds what running the program would build;
               a CFF face likewise publishes its Type 2 charstrings,
               and the dictionary then states FontType 2 */
            if (slot >= 0 && face_cache[slot].file
             && xpost_font_face_is_type1(data.face))
            {
                Xpost_Object cs =
                    _t1_charstrings_from_file(ctx, face_cache[slot].file);

                if (xpost_object_get_type(cs) == dicttype)
                {
                    face_cache[slot].charstrings = cs;
                    face_cache[slot].csreal = 1;
                    ret = xpost_dict_put(ctx, fontdict,
                                       xpost_name_cons(ctx, "CharStrings"), cs);
                    if (ret)
                    {
                        free(fname);
                        return ret;
                    }
                    goto have_charstrings;
                }
            }
            if (slot >= 0 && face_cache[slot].file
             && xpost_font_face_is_cff(data.face))
            {
                Xpost_Object cs =
                    _cff_charstrings_from_file(ctx, face_cache[slot].file,
                                               data.face);

                if (xpost_object_get_type(cs) == dicttype)
                {
                    face_cache[slot].charstrings = cs;
                    face_cache[slot].csreal = 1;
                    cffreal = 1;
                    ret = xpost_dict_put(ctx, fontdict,
                                       xpost_name_cons(ctx, "CharStrings"), cs);
                    if (ret)
                    {
                        free(fname);
                        return ret;
                    }
                    goto have_charstrings;
                }
            }
            nglyphs = xpost_font_face_glyph_name_count(data.face);
            if (nglyphs)
            {
                Xpost_Object csdict;
                char nbuf[128];
                unsigned int gi;
                unsigned int oldmode = ctx->vmmode;
                ctx->vmmode = GLOBAL;
                csdict = xpost_dict_cons(ctx, nglyphs);
                for (gi = 0; gi < nglyphs; gi++)
                {
                    if (!xpost_font_face_glyph_name_get(data.face, gi, nbuf,
                                                        sizeof nbuf))
                        continue;
                    ret = xpost_dict_put(ctx, csdict,
                                         xpost_name_cons(ctx, nbuf),
                                         xpost_int_cons((integer)gi));
                    if (ret)
                    {
                        ctx->vmmode = oldmode;
                        free(fname);
                        return ret;
                    }
                }
                ctx->vmmode = oldmode;
                csdict = xpost_object_set_access(ctx, csdict,
                                                 XPOST_OBJECT_TAG_ACCESS_READ_ONLY);
                if (slot >= 0)
                    face_cache[slot].charstrings = csdict;
                ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "CharStrings"),
                                   csdict);
                if (ret)
                {
                    free(fname);
                    return ret;
                }
            }
have_charstrings: ;
        }
    }

    /* executable, as the reference interpreters answer it */
    fontbbox = xpost_object_cvx(xpost_array_cons(ctx, 4));
    xpost_font_face_get_bbox(data.face, fontbboxarray, istt ? 1.0 : 1000.0);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, fontbbox),
                          xpost_object_get_ent(fontbbox),
                          0, 4 * sizeof(Xpost_Object), fontbboxarray))
    {
        free(fname);
        return VMerror;
    }
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "FontBBox"), fontbbox);
    if (ret)
    {
        free(fname);
        return ret;
    }

    /* the dictionary states what backs it: a TrueType program makes
       a Type 42 font, its character space one unit to the em and its
       CharStrings naming glyph indices, as the type defines; any
       other face keeps the Type 1 dictionary conventions, character
       space a thousand units to the em. FontBBox shares the
       character space, and scalefont and makefont concatenate onto
       FontMatrix in dictionary copies */
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "FontType"),
                       xpost_int_cons(istt ? 42 : cffreal ? 2 : 1));
    if (ret)
    {
        free(fname);
        return ret;
    }
    {
        /* the constructors answer executable objects; a font's matrix
           is data, so it says so at its construction, as
           doc/NEWINTERNALS asks of every composite made here */
        Xpost_Object fontmatrix = xpost_object_cvlit(xpost_array_cons(ctx, 6));
        real diag = istt ? 1.0f : 0.001f;
        int mi;
        for (mi = 0; mi < 6; mi++)
        {
            int mret = xpost_array_put(ctx, fontmatrix, mi,
                            xpost_real_cons(mi == 0 || mi == 3 ? diag : 0.0f));
            if (mret)
            {
                free(fname);
                return mret;
            }
        }
        ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "FontMatrix"), fontmatrix);
        if (ret)
        {
            free(fname);
            return ret;
        }
    }
    if (istt && xpost_object_get_type(sfnts_obj) == arraytype)
    {
        ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "sfnts"),
                             sfnts_obj);
        if (ret)
        {
            free(fname);
            return ret;
        }
    }

    if (!xpost_memory_put(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0,
                          sizeof data, &data))
    {
        free(fname);
        return VMerror;
    }
    xpost_stack_push(ctx->lo, ctx->os, fontdict);
    free(fname);
    return 0;
#else
    (void)ctx;
    (void)fontname;
    return invalidfont;
#endif
}

/* Load a Type 42 font program: reassemble the /sfnts strings into one
   malloc'd buffer, open it as a memory face, and stash the face in the
   dict's /Private exactly as findfont does for a file face. The buffer
   backs the face for the face's lifetime; like the findfont face cache,
   defined fonts live for the process. */
static
int _loadfont42(Xpost_Context *ctx,
                Xpost_Object fontdict)
{
#ifdef HAVE_FREETYPE2
    Xpost_Object sfnts;
    Xpost_Object privatestr;
    Xpost_Object fontbbox;
    Xpost_Object fontbboxarray[4];
    struct fontdata data;
    unsigned char *buf;
    size_t total;
    word i;
    int ret;

    sfnts = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "sfnts"));
    if (xpost_object_get_type(sfnts) != arraytype)
        return invalidfont;
    total = 0;
    for (i = 0; i < sfnts.comp_.sz; i++)
    {
        Xpost_Object s = xpost_array_get(ctx, sfnts, i);
        if (xpost_object_get_type(s) != stringtype)
            return invalidfont;
        total += s.comp_.sz;
    }
    if (total == 0)
        return invalidfont;
    buf = malloc(total);
    if (!buf)
        return VMerror;
    total = 0;
    for (i = 0; i < sfnts.comp_.sz; i++)
    {
        Xpost_Object s = xpost_array_get(ctx, sfnts, i);
        memcpy(buf + total, xpost_string_get_pointer(ctx, s), s.comp_.sz);
        total += s.comp_.sz;
    }

    data.face = xpost_font_face_new_from_memory(buf, total);
    if (data.face == NULL)
    {
        free(buf);
        return invalidfont;
    }

    /* executable, as the reference interpreters answer it */
    fontbbox = xpost_object_cvx(xpost_array_cons(ctx, 4));
    /* a Type 42 dictionary maps one em to one character-space unit */
    xpost_font_face_get_bbox(data.face, fontbboxarray, 1.0);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, fontbbox),
                          xpost_object_get_ent(fontbbox),
                          0, 4 * sizeof(Xpost_Object), fontbboxarray))
        return VMerror;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "FontBBox"), fontbbox);
    if (ret)
        return ret;

    privatestr = xpost_string_cons(ctx, sizeof data, NULL);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0,
                          sizeof data, &data))
        return VMerror;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "Private"), privatestr);
    if (ret)
        return ret;
    return 0;
#else
    (void)ctx;
    (void)fontdict;
    return invalidfont;
#endif
}

/* scalefont and makefont are implemented in font.ps: each returns a
   fresh dictionary with the requested transform concatenated onto the
   font's FontMatrix. No operator mutates the shared face; the text
   operators size it from FontMatrix and the CTM at use time
   (_face_setup below). */



static
int _setfont(Xpost_Context *ctx,
             Xpost_Object fontdict)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    int ret;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));

    ret = xpost_dict_put(ctx, gs, xpost_name_cons(ctx, "currfont"), fontdict);
    if (ret)
        return ret;

    return 0;
}

/* The bands last read back, kept against the array they were read from
   and the serial of the clip that array belongs to. Reading the array
   costs five object fetches per band of the whole region, and every
   text operator under one region would pay it again; narrowing a glyph
   against the bands, which is what the reading is for, costs the rows
   the glyph covers. The serial never repeats within a run, so an array
   that has since been reused for something else cannot answer for it.
   Nothing here holds a reference into VM: the entity number is
   compared, never followed. */
static struct
{
    int serial;
    int ent;
    unsigned int off, sz;
    clipband *band;
    int n;
} _clip_memo;

static int _band_comp(const void *a, const void *b)
{
    const clipband *p = a, *q = b;

    if (p->band != q->band)
        return p->band < q->band ? -1 : 1;
    if (p->lo != q->lo)
        return p->lo < q->lo ? -1 : 1;
    return 0;
}

static int _clip_number(Xpost_Context *ctx, Xpost_Object arr, int i, double *v)
{
    Xpost_Object o = xpost_array_get(ctx, arr, i);

    if (xpost_object_get_type(o) == realtype)
        *v = o.real_.val;
    else if (xpost_object_get_type(o) == integertype)
        *v = (double)o.int_.val;
    else
        return 0;
    return 1;
}

/* A device bound as the pixel index it names. The region's own bounds
   are page-sized, but they are read from a path a program may have put
   any number into, and a raster is indexed by int: a bound past the
   range is taken to the end of it, which is outside every raster either
   way and so narrows exactly as the true value would. */
#define CLIP_COORD_MAX 16777216.0
static
int _clip_floor(double v)
{
    v = floor(v);
    return v < -CLIP_COORD_MAX ? (int)-CLIP_COORD_MAX
         : v > CLIP_COORD_MAX ? (int)CLIP_COORD_MAX : (int)v;
}
static
int _clip_ceil(double v)
{
    v = ceil(v);
    return v < -CLIP_COORD_MAX ? (int)-CLIP_COORD_MAX
         : v > CLIP_COORD_MAX ? (int)CLIP_COORD_MAX : (int)v;
}

/* Read the region's resolved form into the band table above. The form
   is an array of tiles, each a null-separated array of pixel-band
   rectangles of five objects apiece, as .regionmeet returns one. The
   tiles meet along row boundaries and no two describe the same row, so
   the bands only need ordering.
   Answers the band count, or -1 when the array is not in that form. */
static
int _clip_bands_get(Xpost_Context *ctx, Xpost_Object spans, int serial)
{
    clipband *b;
    int nslice = (int)spans.comp_.sz;
    int n = 0;
    int i, k, at;

    if (_clip_memo.band
     && _clip_memo.serial == serial
     && _clip_memo.ent == xpost_object_get_ent(spans)
     && _clip_memo.off == spans.comp_.off
     && _clip_memo.sz == spans.comp_.sz)
        return _clip_memo.n;

    for (k = 0; k < nslice; k++)
    {
        Xpost_Object sl = xpost_array_get(ctx, spans, k);

        if (xpost_object_get_type(sl) != arraytype || sl.comp_.sz % 5)
            return -1;
        n += (int)(sl.comp_.sz / 5);
    }
    /* an empty region resolves to no bands at all, and covers no pixel */
    b = NULL;
    if (n)
    {
        b = malloc((size_t)n * sizeof *b);
        if (!b)
            return -1;
    }
    at = 0;
    for (k = 0; k < nslice; k++)
    {
        Xpost_Object sl = xpost_array_get(ctx, spans, k);
        int m = (int)(sl.comp_.sz / 5);

        /* the table was sized by a first pass over the same array, and
           the write stays inside what that pass counted; the count the
           region is described by is what this pass wrote, so no band
           beyond it is ever read */
        for (i = 0; i < m && at < n; i++)
        {
            Xpost_Object p0 = xpost_array_get(ctx, sl, 5 * i);
            Xpost_Object p1 = xpost_array_get(ctx, sl, 5 * i + 1);
            double lo, hi, band;

            if (xpost_object_get_type(p0) != arraytype || p0.comp_.sz != 2
             || xpost_object_get_type(p1) != arraytype || p1.comp_.sz != 2
             || !_clip_number(ctx, p0, 0, &lo)
             || !_clip_number(ctx, p0, 1, &band)
             || !_clip_number(ctx, p1, 0, &hi))
            {
                free(b);
                return -1;
            }
            /* the rectangles sit on pixel boundaries: they are the
               columns and the row a fill of the region covers */
            b[at].band = _clip_floor(band + 0.5);
            b[at].lo = _clip_floor(lo + 0.5);
            b[at].hi = _clip_floor(hi + 0.5);
            at++;
        }
    }
    if (at > 1)
        qsort(b, (size_t)at, sizeof *b, _band_comp);
    free(_clip_memo.band);
    _clip_memo.serial = serial;
    _clip_memo.ent = xpost_object_get_ent(spans);
    _clip_memo.off = spans.comp_.off;
    _clip_memo.sz = spans.comp_.sz;
    _clip_memo.band = b;
    _clip_memo.n = at;
    return at;
}

/* The pixels the clip region covers, as the glyph raster route meets
   them. .showclip in data/font.ps leaves the region's description in
   the clip's own cache holder before any text operator reaches here:
   /clipbox when the region is a rectangle, its device bounds, and
   /clipspans otherwise, the region resolved through the same span
   intersection every fill meets a region by, a slice of rows per
   element. A holder carrying neither describes no region and narrows
   nothing. */
static
void _text_clip_get(Xpost_Context *ctx, Xpost_Object gs, textstate *ts)
{
    Xpost_Object cache, o;
    int serial = 0;

    ts->clipkind = CLIP_ALL;
    ts->bands = NULL;
    ts->nbands = 0;
    cache = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "clipcache"));
    if (xpost_object_get_type(cache) != dicttype)
        return;
    o = xpost_dict_get(ctx, cache, xpost_name_cons(ctx, "serial"));
    if (xpost_object_get_type(o) == integertype)
        serial = o.int_.val;

    o = xpost_dict_get(ctx, cache, xpost_name_cons(ctx, "clipbox"));
    if (xpost_object_get_type(o) == arraytype && o.comp_.sz == 4)
    {
        double c[4];

        if (!_clip_number(ctx, o, 0, &c[0])
         || !_clip_number(ctx, o, 1, &c[1])
         || !_clip_number(ctx, o, 2, &c[2])
         || !_clip_number(ctx, o, 3, &c[3]))
            return;
        /* the bounds meet the pixel grid under the any-part-of-pixel
           rule of PLRM 7.5.1, on the quantized coordinates the scan
           conversion reads a region's vertices as */
        ts->cx0 = _clip_floor(xpost_dev_line_quantize(c[0]));
        ts->cy0 = _clip_floor(xpost_dev_line_quantize(c[1]));
        ts->cx1 = _clip_ceil(xpost_dev_line_quantize(c[2]));
        ts->cy1 = _clip_ceil(xpost_dev_line_quantize(c[3]));
        ts->clipkind = CLIP_BOX;
        return;
    }

    o = xpost_dict_get(ctx, cache, xpost_name_cons(ctx, "clipspans"));
    if (xpost_object_get_type(o) == arraytype)
    {
        int n = _clip_bands_get(ctx, o, serial);

        if (n < 0)
            return;
        ts->bands = _clip_memo.band;
        ts->nbands = n;
        ts->cy0 = n ? _clip_memo.band[0].band : 0;
        ts->cy1 = n ? _clip_memo.band[n - 1].band + 1 : 0;
        ts->clipkind = CLIP_BANDS;
    }
}

/* the index of the first band of row y, or the band count when the
   region covers no part of that row */
static
int _clip_band_row(const textstate *ts, int y)
{
    int lo = 0, hi = ts->nbands;

    while (lo < hi)
    {
        int mid = lo + (hi - lo) / 2;

        if (ts->bands[mid].band < y)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static
textstate _text_state_get(Xpost_Context *ctx,
                          Xpost_Object gs,
                          Xpost_Object fontdict,
                          Xpost_Object devdic)
{
    textstate ts;
    Xpost_Object tab, vec, sep;

    ts.encoding = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Encoding"));
    ts.charstrings = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "CharStrings"));
    ts.metrics = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Metrics"));
    ts.cdmat_ok = xpost_object_get_type(ts.metrics) == dicttype
               && _char_device_matrix(ctx, gs, fontdict, ts.cdmat);
    ts.blendpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "BlendPix"));
    tab = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "TextAlphaBits"));
    ts.blend = (xpost_object_get_type(ts.blendpix) == operatortype
             || (xpost_object_get_type(ts.blendpix) == arraytype
              && xpost_object_is_exe(ts.blendpix)))
            && xpost_object_get_type(tab) == integertype
            && tab.int_.val > 1;
    vec = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "VectorGlyphs"));
    ts.vector = xpost_object_get_type(vec) == booleantype && vec.int_.val;
    /* an extent-tracking device (the bbox device) needs no glyph
       rasterization: each glyph contributes its ink box through the
       device's FillRect instead */
    vec = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "GlyphExtents"));
    ts.extents = xpost_object_get_type(vec) == booleantype && vec.int_.val;
    memset(&ts.fillrect, 0, sizeof ts.fillrect);  /* invalidtype */
    if (ts.extents)
        ts.fillrect = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "FillRect"));
    /* a separation the graphics state registered with the device:
       glyph outlines fill in the separation, not the process colour */
    ts.sepindex = -1;
    ts.septint = 0.0;
    sep = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "sepindex"));
    if (xpost_object_get_type(sep) == integertype)
    {
        Xpost_Object tint = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "septint"));
        ts.sepindex = sep.int_.val;
        if (xpost_object_get_type(tint) == realtype)
            ts.septint = tint.real_.val;
        else if (xpost_object_get_type(tint) == integertype)
            ts.septint = (double)tint.int_.val;
    }
    _text_clip_get(ctx, gs, &ts);
    return ts;
}

/* Map a character code to a glyph index. When the font carries an
   /Encoding array with a glyph name at this code, the name selects the
   glyph. A Type 42 font's /CharStrings dictionary maps glyph names to
   glyph indices and is authoritative when it holds an integer for the
   name: subset fonts strip the sfnt's own name and character-map
   tables and carry the name-to-index mapping only here. Otherwise the
   name is resolved against the face's glyph names; codes whose entry
   is not a name (the findfont wrapper fills /Encoding with nulls), or
   whose name resolves nowhere, fall back to the face's character map,
   preserving the plain-text behaviour of an unencoded font. */
static
unsigned int _glyph_index_for_char(Xpost_Context *ctx,
                                   Xpost_Object encoding,
                                   Xpost_Object charstrings,
                                   void *face,
                                   unsigned int ch)
{
    if (xpost_object_get_type(encoding) == arraytype
     && ch < (unsigned int)encoding.comp_.sz)
    {
        Xpost_Object en = xpost_array_get(ctx, encoding, ch);
        if (xpost_object_get_type(en) == nametype)
        {
            Xpost_Object str;
            char *cname;
            unsigned int gi = 0;

            if (xpost_object_get_type(charstrings) == dicttype)
            {
                Xpost_Object gid = xpost_dict_get(ctx, charstrings,
                                                  xpost_object_cvlit(en));
                if (xpost_object_get_type(gid) == integertype
                 && gid.int_.val >= 0)
                    return (unsigned int)gid.int_.val;
            }
            str = xpost_name_get_string(ctx, en);
            cname = xpost_string_allocate_cstring(ctx, str);
            if (cname)
            {
                if (strcmp(cname, ".notdef") == 0)
                {
                    free(cname);
                    return 0;
                }
                gi = xpost_font_face_glyph_name_index_get(face, cname);
                free(cname);
            }
            if (gi)
                return gi;
        }
    }
    return xpost_font_face_glyph_index_get(face, (char)ch);
}

/* Map a glyph name to a glyph index without passing through a
   character code, as glyphshow selects glyphs: the CharStrings
   dictionary decides when it holds an integer for the name, then the
   face's own glyph names; an unknown name selects the notdef glyph,
   there being no code to fall back to the character map with. */
static
unsigned int _glyph_index_for_name(Xpost_Context *ctx,
                                   Xpost_Object charstrings,
                                   void *face,
                                   Xpost_Object gname)
{
    Xpost_Object str;
    char *cname;
    unsigned int gi = 0;

    if (xpost_object_get_type(charstrings) == dicttype)
    {
        Xpost_Object gid = xpost_dict_get(ctx, charstrings,
                                          xpost_object_cvlit(gname));
        if (xpost_object_get_type(gid) == integertype
         && gid.int_.val >= 0)
            return (unsigned int)gid.int_.val;
    }
    str = xpost_name_get_string(ctx, gname);
    cname = xpost_string_allocate_cstring(ctx, str);
    if (cname)
    {
        if (strcmp(cname, ".notdef") != 0)
            gi = xpost_font_face_glyph_name_index_get(face, cname);
        free(cname);
    }
    return gi;
}

#ifdef HAVE_FREETYPE2
/* Prepare the shared face for use under the current graphics state.
   The font dictionary's FontMatrix carries the size (and any rotation,
   shear or anisotropy concatenated by makefont); the CTM carries the
   device mapping. Neither is sticky on the font: scalefont and
   makefont only build dictionaries, so two sizes of one face coexist
   and the CTM matters when the glyphs are marked, not when the font
   was scaled. Compose the two linear parts, split the result into a
   pixel-per-em scale for the face and a unit-magnitude transform
   (conjugated by the y flip that relates FreeType's y-up glyph space
   to the device's y-down raster), and install both. The face is
   shared through the findfont cache, so every text operator must call
   this before touching glyphs. A missing or malformed FontMatrix
   reads as the identity, serving font programs defined without one. */
static
void _face_setup(Xpost_Context *ctx,
                 Xpost_Object gs,
                 Xpost_Object fontdict,
                 void *face)
{
    real e[4];
    real q;
    real r;
    float mat[6] = { 0 };

    /* text space -> device space: FontMatrix then CTM */
    if (!_char_device_matrix(ctx, gs, fontdict, e))
        return;

    q = (real)sqrt(e[0] * e[0] + e[1] * e[1]);
    if (q == 0)
        q = (real)sqrt(e[2] * e[2] + e[3] * e[3]);
    if (q == 0)
        return;

    /* the em in pixels: FontMatrix maps character space to text space,
       so the composed magnitude q is per character-space unit, and the
       units per em are a convention of the font type (1000 for Type 1
       dictionaries, whose FontMatrix carries the 0.001 factor; one for
       Type 42, whose FontMatrix is an identity over the em) */
    {
        Xpost_Object ft = xpost_dict_get(ctx, fontdict,
                                         xpost_name_cons(ctx, "FontType"));
        Xpost_Object cft = xpost_dict_get(ctx, fontdict,
                                          xpost_name_cons(ctx, "CIDFontType"));
        real qem = q;
        if ((xpost_object_get_type(ft) == integertype
             && (ft.int_.val == 1 || ft.int_.val == 2))
         || (xpost_object_get_type(cft) == integertype && cft.int_.val == 0))
        {
            /* a Type 1 character-space unit is usually a thousandth
               of the em -- the convention findfont dictionaries
               declare whatever their face's native units -- but an
               embedded program keeps its design count (a converted
               2048-unit font arrives with a 1/2048 matrix), recorded
               in the dictionary when its face was assembled */
            Xpost_Object emu = xpost_dict_get(ctx, fontdict,
                                              xpost_name_cons(ctx, ".emunits"));
            int units = xpost_object_get_type(emu) == integertype
                      ? emu.int_.val : 1000;

            qem = q * (units > 0 ? units : 1000);
        }

        /* the face serves a well-conditioned base size (an extreme em
           would fail inside FreeType); the residual ratio to the true
           size rides in the transform, which scales outlines, extents
           and linear advances alike */
        r = qem / xpost_font_face_scale(face, qem);
    }

    mat[0] = (float)( e[0] / q * r);   /* xx */
    mat[1] = (float)( e[2] / q * r);   /* xy */
    mat[2] = (float)(-e[1] / q * r);   /* yx */
    mat[3] = (float)(-e[3] / q * r);   /* yy */
    xpost_font_face_transform(face, mat);
}

/* Resolve the current colour into the device's native space, applying
   the same source-to-destination conversions as the ColorConversion
   table in color.ps (gray by NTSC luminosity, CMYK composed by
   additive complement, RGB to CMYK with full black generation and
   undercolor removal), so glyphs mark in exactly the colour a fill
   under the same graphics state would. A device with the /Process
   native space takes each paint in the space it was set in, so the
   source components pass through unconverted. A source space the
   table does not know passes its raw components through. Returns 0 on
   success. */
static
int _device_color(Xpost_Context *ctx,
                  Xpost_Object gs,
                  Xpost_Object devdic,
                  int *ncomp,
                  Xpost_Object comp[4])
{
#define GSCOMP(name) (o = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, name)), \
                      xpost_object_get_type(o) == realtype ? o.real_.val \
                    : xpost_object_get_type(o) == integertype ? (double)o.int_.val \
                    : 0.0)
#define MIN1(x) ((x) < 1.0 ? (x) : 1.0)
    Xpost_Object colorspace, srcspace, o;
    enum { SRC_GRAY, SRC_RGB, SRC_CMYK, SRC_OTHER } src;
    double v[4];

    srcspace = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "colorspace"));
    if (xpost_dict_compare_objects(ctx, srcspace, xpost_name_cons(ctx, "DeviceGray")) == 0)
        src = SRC_GRAY;
    else if (xpost_dict_compare_objects(ctx, srcspace, xpost_name_cons(ctx, "DeviceRGB")) == 0)
        src = SRC_RGB;
    else if (xpost_dict_compare_objects(ctx, srcspace, xpost_name_cons(ctx, "DeviceCMYK")) == 0)
        src = SRC_CMYK;
    else
        src = SRC_OTHER;
    v[0] = GSCOMP("colorcomp1");
    v[1] = GSCOMP("colorcomp2");
    v[2] = GSCOMP("colorcomp3");
    v[3] = GSCOMP("colorcomp4");

    colorspace = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "nativecolorspace"));
    if (xpost_dict_compare_objects(ctx, colorspace, xpost_name_cons(ctx, "DeviceGray")) == 0)
    {
        double g;

        switch (src)
        {
            case SRC_RGB:
                g = 0.3 * v[0] + 0.59 * v[1] + 0.11 * v[2];
                break;
            case SRC_CMYK:
                g = 1.0 - MIN1(0.3 * v[0] + 0.59 * v[1] + 0.11 * v[2] + v[3]);
                break;
            default: /* gray, or an unknown space's first component */
                g = v[0];
                break;
        }
        *ncomp = 1;
        comp[0] = xpost_real_cons((real)g);
    }
    else if (xpost_dict_compare_objects(ctx, colorspace, xpost_name_cons(ctx, "DeviceRGB")) == 0)
    {
        double r, g, b;

        switch (src)
        {
            case SRC_GRAY:
                r = g = b = v[0];
                break;
            case SRC_CMYK:
                r = 1.0 - MIN1(v[0] + v[3]);
                g = 1.0 - MIN1(v[1] + v[3]);
                b = 1.0 - MIN1(v[2] + v[3]);
                break;
            default:
                r = v[0]; g = v[1]; b = v[2];
                break;
        }
        *ncomp = 3;
        comp[0] = xpost_real_cons((real)r);
        comp[1] = xpost_real_cons((real)g);
        comp[2] = xpost_real_cons((real)b);
    }
    else if (xpost_dict_compare_objects(ctx, colorspace, xpost_name_cons(ctx, "DeviceCMYK")) == 0)
    {
        double c, m, y, k;

        switch (src)
        {
            case SRC_GRAY:
                c = m = y = 0;
                k = 1.0 - v[0];
                break;
            case SRC_RGB:
                c = 1.0 - v[0];
                m = 1.0 - v[1];
                y = 1.0 - v[2];
                k = c < m ? c : m;
                if (y < k) k = y;
                c -= k; m -= k; y -= k;
                break;
            default:
                c = v[0]; m = v[1]; y = v[2]; k = v[3];
                break;
        }
        *ncomp = 4;
        comp[0] = xpost_real_cons((real)c);
        comp[1] = xpost_real_cons((real)m);
        comp[2] = xpost_real_cons((real)y);
        comp[3] = xpost_real_cons((real)k);
    }
    else if (xpost_dict_compare_objects(ctx, colorspace, xpost_name_cons(ctx, "Process")) == 0)
    {
        /* the device takes each paint in the space it was set in:
           deliver the source components unconverted */
        switch (src)
        {
            case SRC_GRAY:
                *ncomp = 1;
                comp[0] = xpost_real_cons((real)v[0]);
                break;
            case SRC_CMYK:
                *ncomp = 4;
                comp[0] = xpost_real_cons((real)v[0]);
                comp[1] = xpost_real_cons((real)v[1]);
                comp[2] = xpost_real_cons((real)v[2]);
                comp[3] = xpost_real_cons((real)v[3]);
                break;
            default: /* RGB, or an unknown space's first three components */
                *ncomp = 3;
                comp[0] = xpost_real_cons((real)v[0]);
                comp[1] = xpost_real_cons((real)v[1]);
                comp[2] = xpost_real_cons((real)v[2]);
                break;
        }
    }
    else
    {
        XPOST_LOG_ERR("unimplemented device colorspace");
        return unregistered;
    }
    return 0;
#undef GSCOMP
#undef MIN1
}

/* Plot a rendered glyph bitmap through the device. An 8-bit coverage
   bitmap is thresholded at half coverage -- the sharp rasterization a
   scan conversion of the outline would produce -- unless the device
   anti-aliases text (ts->blend), in which case fully covered pixels go
   through PutPix and partially covered edge pixels through the
   device's BlendPix with their coverage.
   The raster is narrowed to the pixels the clip region covers, as
   PLRM 7.5.1 has every painting operation meet the region: the whole
   raster is rejected or accepted against the region's bounds first, so
   a glyph clear of the boundary costs the comparison and nothing more,
   and only a glyph the boundary crosses is walked run by run. */
static
void _draw_bitmap(Xpost_Context *ctx,
                  Xpost_Object devdic,
                  Xpost_Object putpix,
                  const textstate *ts,
                  const unsigned char *buffer,
                  int rows,
                  int width,
                  int pitch,
                  char pixel_mode,
                  int xpos,
                  int ypos,
                  int ncomp,
                  Xpost_Object comp1,
                  Xpost_Object comp2,
                  Xpost_Object comp3,
                  Xpost_Object comp4)
{
    int i, j;
    const unsigned char *tmp;
    unsigned int pix;
    Xpost_Object exec_op;
    int i0 = 0, i1 = rows;
    int inside;

    /* The operator itself, not its name: a name pushed for execution is
       resolved against the dictionary stack at that moment, so a program
       that has defined /exec would supply the body instead. Held in a local
       because the glyph loop pushes it for every set pixel when putpix is a
       procedure rather than an operator. */
    exec_op = XPOST_OP(ctx, exec);
    XPOST_LOG_INFO("bitmap rows = %d, bitmap width = %d", rows, width);
    XPOST_LOG_INFO("bitmap pitch = %d", pitch);
    XPOST_LOG_INFO("bitmap pixel_mode = %d", pixel_mode);

    if (ts->clipkind != CLIP_ALL)
    {
        if (ts->cy0 - ypos > i0)
            i0 = ts->cy0 - ypos;
        if (ts->cy1 - ypos < i1)
            i1 = ts->cy1 - ypos;
    }
    /* the raster lies wholly within the region: every run below is the
       whole row, so the walk skips the run machinery altogether */
    inside = ts->clipkind == CLIP_ALL
          || (ts->clipkind == CLIP_BOX
           && i0 == 0 && i1 == rows
           && ts->cx0 <= xpos && ts->cx1 >= xpos + width);

    for (i = i0; i < i1; i++)
    {
        int run = 0;      /* the band cursor, while walking runs */
        int j0 = 0, j1 = width;

        /* the pitch is signed: a raster whose rows run the other way
           steps backwards through its buffer */
        tmp = buffer + (ptrdiff_t)i * pitch;
        if (!inside)
        {
            if (ts->clipkind == CLIP_BOX)
            {
                if (ts->cx0 - xpos > j0)
                    j0 = ts->cx0 - xpos;
                if (ts->cx1 - xpos < j1)
                    j1 = ts->cx1 - xpos;
            }
            else if (ts->clipkind == CLIP_BANDS)
                run = _clip_band_row(ts, ypos + i);
        }
      next_run:
        if (!inside && ts->clipkind == CLIP_BANDS)
        {
            if (run >= ts->nbands || ts->bands[run].band != ypos + i)
                continue;
            j0 = ts->bands[run].lo - xpos;
            j1 = ts->bands[run].hi - xpos;
            run++;
            if (j0 < 0)
                j0 = 0;
            if (j1 > width)
                j1 = width;
        }
        for (j = j0; j < j1; j++)
        {
            int cov = -1;  /* -1 solid, 0 skip, else blend coverage */

            switch (pixel_mode)
            {
                case XPOST_FONT_PIXEL_MODE_MONO:
                    pix = (tmp[j / 8] >> (7 - (j % 8))) & 1;
                    cov = pix ? -1 : 0;
                    break;
                case XPOST_FONT_PIXEL_MODE_GRAY:
                    pix = tmp[j];
                    if (ts->blend)
                        cov = pix == 255 ? -1 : (int)pix;
                    else
                        cov = pix >= 128 ? -1 : 0;
                    break;
                default:
                    XPOST_LOG_ERR("unsupported pixel_mode");
                    return;
            }
            if (cov)
            {
                switch (ncomp)
                {
                    case 1:
                        xpost_stack_push(ctx->lo, ctx->os, comp1);
                        break;
                    case 3:
                        xpost_stack_push(ctx->lo, ctx->os, comp1);
                        xpost_stack_push(ctx->lo, ctx->os, comp2);
                        xpost_stack_push(ctx->lo, ctx->os, comp3);
                        break;
                    case 4:
                        xpost_stack_push(ctx->lo, ctx->os, comp1);
                        xpost_stack_push(ctx->lo, ctx->os, comp2);
                        xpost_stack_push(ctx->lo, ctx->os, comp3);
                        xpost_stack_push(ctx->lo, ctx->os, comp4);
                        break;
                }
                if (cov > 0)
                    xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(cov));
                xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(xpos + j));
                xpost_stack_push(ctx->lo, ctx->os, xpost_int_cons(ypos + i));
                xpost_stack_push(ctx->lo, ctx->os, devdic);
                if (cov > 0)
                    xpost_stack_push(ctx->lo, ctx->es, ts->blendpix);
                else if (xpost_object_get_type(putpix) == operatortype)
                    xpost_stack_push(ctx->lo, ctx->es, putpix);
                else
                {
                    xpost_stack_push(ctx->lo, ctx->os, putpix);
                    xpost_stack_push(ctx->lo, ctx->es, exec_op);
                }
            }
        }
        if (!inside && ts->clipkind == CLIP_BANDS)
            goto next_run;
    }
}

/* Emit one glyph's outline into the pdfwrite device's content
   accumulator as filled path segments: "r g b rg", the contours as
   m/l/c/h operators, and a nonzero-winding fill. Coordinates arrive
   from the face in y-up pixels relative to the pen and are placed at
   the y-down device pen position, exactly where the bitmap path puts
   the rendered glyph. */
typedef struct glyphfrag
{
    Xpost_String_Buffer d;
    double px, py;
    int has;   /* any contour emitted */
    int oom;
    int svg;   /* emit SVG path commands instead of PDF operators */
} glyphfrag;

/* the outline walker aborts on a non-zero return, and oom records that the
   fragment is incomplete for the caller that emits it */
static int _frag_put(glyphfrag *f, const char *s, size_t n)
{
    int ret = xpost_strbuf_append(&f->d, s, n);

    if (ret)
        f->oom = 1;
    return ret;
}

static int _frag_xy(glyphfrag *f, double x, double y)
{
    char t[64];
    int n;

    n = xpost_dev_pdf_fmt_num(t, f->px + x);
    t[n++] = ' ';
    n += xpost_dev_pdf_fmt_num(t + n, f->py - y);
    t[n++] = ' ';
    return _frag_put(f, t, n);
}

/* PDF and SVG spell the same commands differently: PDF postfixes the
   operator ("x y m"), SVG prefixes it ("M x y"). */
static int _frag_cmd_xy(glyphfrag *f, const char *pdfop, const char *svgop, double x, double y)
{
    if (f->svg)
        return _frag_put(f, svgop, 1) || _frag_xy(f, x, y);
    return _frag_xy(f, x, y) || _frag_put(f, pdfop, 2);
}

static int _frag_moveto(void *user, double x, double y)
{
    glyphfrag *f = user;
    f->has = 1;
    return _frag_cmd_xy(f, "m\n", "M", x, y);
}

static int _frag_lineto(void *user, double x, double y)
{
    glyphfrag *f = user;
    return _frag_cmd_xy(f, "l\n", "L", x, y);
}

static int _frag_curveto(void *user, double x1, double y1, double x2, double y2, double x3, double y3)
{
    glyphfrag *f = user;
    if (f->svg)
        return _frag_put(f, "C", 1)
            || _frag_xy(f, x1, y1) || _frag_xy(f, x2, y2) || _frag_xy(f, x3, y3);
    return _frag_xy(f, x1, y1) || _frag_xy(f, x2, y2) || _frag_xy(f, x3, y3)
        || _frag_put(f, "c\n", 2);
}

static int _frag_closepath(void *user)
{
    glyphfrag *f = user;
    if (f->svg)
        return _frag_put(f, "Z", 1);
    return _frag_put(f, "h\n", 2);
}

static
int _show_char_outline(Xpost_Context *ctx,
                       Xpost_Object devdic,
                       const textstate *ts,
                       void *face,
                       unsigned int glyph_index,
                       real xpos,
                       real ypos,
                       int ncomp,
                       Xpost_Object comp1,
                       Xpost_Object comp2,
                       Xpost_Object comp3,
                       Xpost_Object comp4,
                       long *advance_x,
                       long *advance_y)
{
    glyphfrag f;
    Xpost_Font_Outline_Sink sink;
    double r, g, b;
    char t[96];
    int n;

    memset(&f, 0, sizeof f);
    if (xpost_strbuf_init(&f.d, 256))
        f.oom = 1;
    f.px = xpos;
    f.py = ypos;

    /* the target syntax is the device's choice: PDF operators unless the
       device declares /VectorSyntax /svg */
    {
        Xpost_Object syn = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "VectorSyntax"));
        if (xpost_object_get_type(syn) == nametype)
        {
            Xpost_Object ss = xpost_name_get_string(ctx, syn);
            f.svg = ss.comp_.sz == 3
                 && memcmp(xpost_string_get_pointer(ctx, ss), "svg", 3) == 0;
        }
    }

    r = xpost_object_number(comp1);
    g = ncomp >= 3 ? xpost_object_number(comp2) : r;
    b = ncomp >= 3 ? xpost_object_number(comp3) : r;
    if (ts->sepindex >= 0 && !f.svg)
    {
        /* the fill colour is a separation registered with the device:
           paint in its /CS<i> resource space at the recorded tint */
        memcpy(t, "/CS", 3); n = 3;
        n += xpost_dev_pdf_fmt_num(t + n, (double)ts->sepindex);
        memcpy(t + n, " cs ", 4); n += 4;
        n += xpost_dev_pdf_fmt_num(t + n, ts->septint);
        memcpy(t + n, " scn\n", 5); n += 5;
    }
    else if (ncomp == 1 && !f.svg)
    {
        /* the paint's space is DeviceGray: the glyph fills in it */
        n = xpost_dev_pdf_fmt_num(t, r);
        memcpy(t + n, " g\n", 3);
        n += 3;
    }
    else if (ncomp == 4 && !f.svg)
    {
        /* the paint's space is DeviceCMYK: the glyph fills in it */
        n = xpost_dev_pdf_fmt_num(t, r);
        t[n++] = ' ';
        n += xpost_dev_pdf_fmt_num(t + n, g);
        t[n++] = ' ';
        n += xpost_dev_pdf_fmt_num(t + n, b);
        t[n++] = ' ';
        n += xpost_dev_pdf_fmt_num(t + n, xpost_object_number(comp4));
        memcpy(t + n, " k\n", 3);
        n += 3;
    }
    else if (f.svg)
    {
        memcpy(t, "<path fill=\"rgb(", 16); n = 16;
        n += xpost_dev_pdf_fmt_num(t + n, r * 100); t[n++] = '%'; t[n++] = ',';
        n += xpost_dev_pdf_fmt_num(t + n, g * 100); t[n++] = '%'; t[n++] = ',';
        n += xpost_dev_pdf_fmt_num(t + n, b * 100); t[n++] = '%';
        memcpy(t + n, ")\" d=\"", 6); n += 6;
    }
    else
    {
        n = xpost_dev_pdf_fmt_num(t, r);
        t[n++] = ' ';
        n += xpost_dev_pdf_fmt_num(t + n, g);
        t[n++] = ' ';
        n += xpost_dev_pdf_fmt_num(t + n, b);
        memcpy(t + n, " rg\n", 4);
        n += 4;
    }
    _frag_put(&f, t, n);

    sink.moveto = _frag_moveto;
    sink.lineto = _frag_lineto;
    sink.curveto = _frag_curveto;
    sink.closepath = _frag_closepath;
    sink.user = &f;
    if (!xpost_font_face_glyph_outline(face, glyph_index, &sink, advance_x, advance_y))
    {
        xpost_strbuf_free(&f.d);
        return 0;
    }
    /* a blank glyph (e.g. space) decomposes to nothing: advance only */
    if (f.has && !f.oom)
    {
        if (f.svg)
            _frag_put(&f, "\"/>\n", 4);   /* glyphs fill nonzero: SVG's default rule */
        else
            _frag_put(&f, "f\n", 2);
        if (!f.oom && xpost_dev_pdf_append(ctx, devdic, f.d.s, f.d.len))
        {
            /* the glyph's content did not reach the page */
            xpost_strbuf_free(&f.d);
            return 0;
        }
    }
    xpost_strbuf_free(&f.d);
    return 1;
}

#endif

static
int _show_glyph(Xpost_Context *ctx,
                Xpost_Object devdic,
                Xpost_Object putpix,
                struct fontdata data,
                const textstate *ts,
                real *xpos,
                real *ypos,
                unsigned int glyph_index,
                Xpost_Object glyphname,
                int ncomp,
                Xpost_Object comp1,
                Xpost_Object comp2,
                Xpost_Object comp3,
                Xpost_Object comp4)
{
#ifdef HAVE_FREETYPE2
    unsigned char *buffer;
    int rows;
    int width;
    int pitch;
    char pixel_mode;
    int left;
    int top;
    long advance_x;
    long advance_y;
    long bx0, by0, bx1, by1;

    if (ts->vector)
    {
        if (!_show_char_outline(ctx, devdic, ts, data.face, glyph_index,
                                *xpos, *ypos, ncomp, comp1, comp2, comp3, comp4,
                                &advance_x, &advance_y))
            return 0;
    }
    else if (ts->extents
        && xpost_font_face_glyph_extents(data.face, glyph_index,
                                         &bx0, &by0, &bx1, &by1,
                                         &advance_x, &advance_y))
    {
        /* an extent-tracking device needs no glyph rasterization (whose
           cost grows with the square of the resolution): the glyph
           contributes its ink box through the device's FillRect. The
           box is 26.6 glyph space, y-up around the pen; the device is
           y-down. An empty box (a space) advances only. A glyph with
           no outline takes the rendering path below instead. */
        if (bx1 > bx0 && by1 > by0)
        {
            switch (ncomp)
            {
                case 4:
                    xpost_stack_push(ctx->lo, ctx->os, comp1);
                    xpost_stack_push(ctx->lo, ctx->os, comp2);
                    xpost_stack_push(ctx->lo, ctx->os, comp3);
                    xpost_stack_push(ctx->lo, ctx->os, comp4);
                    break;
                case 3:
                    xpost_stack_push(ctx->lo, ctx->os, comp1);
                    xpost_stack_push(ctx->lo, ctx->os, comp2);
                    xpost_stack_push(ctx->lo, ctx->os, comp3);
                    break;
                default:
                    xpost_stack_push(ctx->lo, ctx->os, comp1);
                    break;
            }
            xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons((real)(*xpos + bx0 / 64.0)));
            xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons((real)(*ypos - by1 / 64.0)));
            xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons((real)((bx1 - bx0) / 64.0)));
            xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons((real)((by1 - by0) / 64.0)));
            xpost_stack_push(ctx->lo, ctx->os, devdic);
            if (xpost_object_get_type(ts->fillrect) == operatortype)
                xpost_stack_push(ctx->lo, ctx->es, ts->fillrect);
            else
            {
                xpost_stack_push(ctx->lo, ctx->os, ts->fillrect);
                xpost_stack_push(ctx->lo, ctx->es,
                                 XPOST_OP(ctx, exec));
            }
        }
    }
    else
    {
        if (!xpost_font_face_glyph_render(data.face, glyph_index))
            return 0;
        xpost_font_face_glyph_buffer_get(data.face,
                                         &buffer, &rows, &width, &pitch, &pixel_mode,
                                         &left, &top, &advance_x, &advance_y);
        /* the pen rides at fractional device positions but the glyph
           bitmap sits on the pixel grid: place it at the nearest
           pixel, not the floor, so a pen an epsilon shy of a pixel
           boundary (the linear advance's 16.16 quantization) lands
           where exact arithmetic would put it */
        _draw_bitmap(ctx, devdic, putpix, ts,
                     buffer, rows, width, pitch, pixel_mode,
                     (int)floor(*xpos + left + 0.5),
                     (int)floor(*ypos - top + 0.5),
                     ncomp, comp1, comp2, comp3, comp4);
    }
    /* a /Metrics entry for this glyph overrides the face's advance */
    _metrics_advance(ctx, ts, glyphname, &advance_x, &advance_y);
    /* the face transform leaves the advance in y-up glyph space; the
       pen advances in y-down device space, keeping the fractional part
       (truncating each glyph's advance drifts the line's length) */
    *xpos += (real)(advance_x / 65536.0);
    *ypos -= (real)(advance_y / 65536.0);
#else
    (void)ctx;
    (void)devdic;
    (void)putpix;
    (void)data;
    (void)ts;
    (void)xpos;
    (void)ypos;
    (void)glyph_index;
    (void)glyphname;
    (void)ncomp;
    (void)comp1;
    (void)comp2;
    (void)comp3;
    (void)comp4;
#endif
    return 1;
}

static
int _show_char(Xpost_Context *ctx,
               Xpost_Object devdic,
               Xpost_Object putpix,
               struct fontdata data,
               const textstate *ts,
               real *xpos,
               real *ypos,
               unsigned int ch,
               int ncomp,
               Xpost_Object comp1,
               Xpost_Object comp2,
               Xpost_Object comp3,
               Xpost_Object comp4)
{
    /* show does not kern: pair adjustment in PostScript is the
       program's business (kshow, ashow); the advance is the glyph
       widths alone */
    unsigned int glyph_index = _glyph_index_for_char(ctx, ts->encoding,
                                                     ts->charstrings,
                                                     data.face, ch);
    return _show_glyph(ctx, devdic, putpix, data, ts, xpos, ypos,
                       glyph_index, _encoded_name(ctx, ts->encoding, ch),
                       ncomp, comp1, comp2, comp3, comp4);
}

static
int _get_current_point (Xpost_Context *ctx,
                        Xpost_Object gs,
                        real *xpos,
                        real *ypos)
{
    Xpost_Object path;
    char *p;
    unsigned int used, last;
    real co[6];
    int n;

    /* get the current pen position from the packed path string
       (device coordinates; layout as described in xpost_op_path.c) */
    path = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currpath"));
    if (xpost_object_get_type(path) != stringtype)
        return nocurrentpoint;
    /* currpath sits in a program-reachable dictionary, so its header may be
       forged; bound the extent and the last-element offset against the
       string's own allocation before dereferencing them */
    {
        Xpost_Memory_File *mem = xpost_context_select_memory(ctx, path);
        unsigned int ent = xpost_object_get_ent(path);
        unsigned int entsz = mem->table.tab[ent].sz;
        unsigned int avail = path.comp_.off < entsz ? entsz - path.comp_.off : 0;

        if (avail < 16)
            return nocurrentpoint;
        p = xpost_string_get_pointer(ctx, path);
        memcpy(&used, p, sizeof used);
        if (used <= 16 || used > avail)
            return nocurrentpoint;
        memcpy(&last, p + 8, sizeof last);
        n = last < used && p[last] == 2 ? 6 : 2; /* curve carries three points */
        if (last >= used || last + 1 + n * sizeof(real) > used)
            return nocurrentpoint;
        memcpy(co, p + last + 1, n * sizeof(real));
        *xpos = co[n - 2];
        *ypos = co[n - 1];
    }
    XPOST_LOG_INFO("currentpoint: %f %f", *xpos, *ypos);

    return 0;
}

/* Build the procedure a show operator leaves on the execution stack to
   run once its glyphs are painted: it restores the current point in
   user space and flushes the page. Elements 0 and 1 hold the position,
   rewritten by _show_finalize_pos with the point the operator ended
   at.

   The procedure runs after the show operator has returned, with the
   program's own dictionaries on the dictionary stack, so it holds what
   it means to call rather than the names of it: an executable name here
   would be resolved then, and a program that had defined /moveto would
   have its own procedure called to finish the show. itransform and
   moveto are operators and go in as operator objects; flushpage is a
   procedure, so its value is taken from systemdict and run by the exec
   that follows it. */
static
int _show_finalize_cons(Xpost_Context *ctx,
                        real xpos,
                        real ypos,
                        Xpost_Object *out)
{
    Xpost_Object f = xpost_object_cvx(xpost_array_cons(ctx, 6));
    Xpost_Object flushpage;
    int ret;

    if (xpost_object_get_type(f) == nulltype)
        return VMerror;
    flushpage = xpost_dict_get(ctx,
                               xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0),
                               xpost_name_cons(ctx, "flushpage"));
    if (xpost_object_get_type(flushpage) == invalidtype
     || xpost_object_get_type(flushpage) == nulltype)
        return undefined;
    if ((ret = xpost_array_put(ctx, f, 0, xpost_real_cons(xpos))) != 0
     || (ret = xpost_array_put(ctx, f, 1, xpost_real_cons(ypos))) != 0
     || (ret = xpost_array_put(ctx, f, 2,
             XPOST_OP(ctx, itransform))) != 0
     || (ret = xpost_array_put(ctx, f, 3,
             XPOST_OP(ctx, moveto))) != 0
     || (ret = xpost_array_put(ctx, f, 4, flushpage)) != 0
     || (ret = xpost_array_put(ctx, f, 5,
             XPOST_OP(ctx, exec))) != 0)
        return ret;
    *out = f;
    return 0;
}

/* Record the point the show ended at in the finalize procedure. */
static
int _show_finalize_pos(Xpost_Context *ctx,
                       Xpost_Object f,
                       real xpos,
                       real ypos)
{
    int ret = xpost_array_put(ctx, f, 0, xpost_real_cons(xpos));

    if (ret)
        return ret;
    return xpost_array_put(ctx, f, 1, xpost_real_cons(ypos));
}

static
int _show(Xpost_Context *ctx,
          Xpost_Object str)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    Xpost_Object fontdict;
    Xpost_Object privatestr;
    struct fontdata data;
    char *cstr;
    real xpos, ypos;
    char *ch;
    Xpost_Object devdic;
    Xpost_Object putpix;
    textstate ts;
    int ncomp;
    Xpost_Object comp[4];
    Xpost_Object finalize;
    int ret;


    /* load the graphicsdict, current graphics state, and current font */
    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    fontdict = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currfont"));
    if (xpost_object_get_type(fontdict) == invalidtype)
        return invalidfont;
    XPOST_LOG_INFO("loaded graphicsdict, graphics state, and current font");

    /* load the device and PutPix member function */
    devdic = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "device"));
    putpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "PutPix"));
    XPOST_LOG_INFO("loaded DEVICE and PutPix");
    ts = _text_state_get(ctx, gs, fontdict, devdic);

    /* get the font data from the font dict */
    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == invalidtype)
        return invalidfont;
    if (!xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0, sizeof data,
                          &data)
     || data.face == NULL)
    {
        XPOST_LOG_ERR("face is NULL");
        return invalidfont;
    }
    _face_setup(ctx, gs, fontdict, data.face);
    XPOST_LOG_INFO("loaded font data from dict");

    /* get a c-style nul-terminated string */
    cstr = xpost_string_allocate_cstring(ctx, str);
    XPOST_LOG_INFO("append nul to string");

    ret = _get_current_point(ctx, gs, &xpos, &ypos);
    if (ret){
        free(cstr);
        return ret;
    }

    if (_device_color(ctx, gs, devdic, &ncomp, comp))
    {
        free(cstr);
        return unregistered;
    }
    XPOST_LOG_INFO("ncomp = %d", ncomp);

    ret = _show_finalize_cons(ctx, xpos, ypos, &finalize);
    if (ret)
    {
        free(cstr);
        return ret;
    }
    xpost_stack_push(ctx->lo, ctx->es, finalize);

    /* render text in char *cstr  with font data  at pen position xpos ypos */
    for (ch = cstr; *ch; ch++) {
        _show_char(ctx, devdic, putpix, data, &ts, &xpos, &ypos, (unsigned char)*ch,
                ncomp, comp[0], comp[1], comp[2], comp[3]);
    }

    /* update current position in the graphics state */
    ret = _show_finalize_pos(ctx, finalize, xpos, ypos);

    free(cstr);
    return ret;
}

/* glyphname  .glyphshow  -
   paint the single glyph the name selects, bypassing the encoding,
   and advance the current point by the glyph's width. The
   PostScript-level glyphshow sends Type 3 fonts to their build
   procedures instead of here. */
static
int _glyphshow_common(Xpost_Context *ctx,
                      Xpost_Object gname,
                      int byname,
                      unsigned int gid)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    Xpost_Object fontdict;
    Xpost_Object privatestr;
    struct fontdata data;
    real xpos, ypos;
    Xpost_Object devdic;
    Xpost_Object putpix;
    textstate ts;
    int ncomp;
    Xpost_Object comp[4];
    Xpost_Object finalize;
    unsigned int glyph_index;
    int ret;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    fontdict = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currfont"));
    if (xpost_object_get_type(fontdict) == invalidtype)
        return invalidfont;

    devdic = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "device"));
    putpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "PutPix"));
    ts = _text_state_get(ctx, gs, fontdict, devdic);

    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == invalidtype)
        return invalidfont;
    if (!xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0, sizeof data,
                          &data)
     || data.face == NULL)
    {
        XPOST_LOG_ERR("face is NULL");
        return invalidfont;
    }
    _face_setup(ctx, gs, fontdict, data.face);

    ret = _get_current_point(ctx, gs, &xpos, &ypos);
    if (ret)
        return ret;

    if (_device_color(ctx, gs, devdic, &ncomp, comp))
        return unregistered;

    ret = _show_finalize_cons(ctx, xpos, ypos, &finalize);
    if (ret)
        return ret;
    xpost_stack_push(ctx->lo, ctx->es, finalize);

    glyph_index = byname
        ? _glyph_index_for_name(ctx, ts.charstrings, data.face, gname)
        : gid;
    _show_glyph(ctx, devdic, putpix, data, &ts, &xpos, &ypos,
                glyph_index, byname ? gname : invalid,
                ncomp, comp[0], comp[1], comp[2], comp[3]);

    return _show_finalize_pos(ctx, finalize, xpos, ypos);
}

static
int _glyphshow(Xpost_Context *ctx,
               Xpost_Object gname)
{
    return _glyphshow_common(ctx, gname, 1, 0);
}

/* index  .glyphshowidx  -
   paint the single glyph at the given index in the current font's
   face and advance the current point by its width; the composite
   font machinery reaches glyphs by index once a CMap has resolved
   the character code */
static
int _glyphshowidx(Xpost_Context *ctx,
                  Xpost_Object gidx)
{
    if (gidx.int_.val < 0)
        return rangecheck;
    return _glyphshow_common(ctx, null, 0,
                             (unsigned int)gidx.int_.val);
}

/* big-endian field readers over the assembled font program */
static unsigned int _sfnt_u16(const unsigned char *p)
{
    return p[0] << 8 | p[1];
}
static unsigned int _sfnt_u32(const unsigned char *p)
{
    return (unsigned int)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static void _sfnt_put16(unsigned char *p, unsigned int v)
{
    p[0] = (v >> 8) & 0xff; p[1] = v & 0xff;
}
static void _sfnt_put32(unsigned char *p, unsigned int v)
{
    p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff; p[3] = v & 0xff;
}


/* ciddict  .loadcidfont0  -
   assemble a working face for a CIDFontType 0 dictionary. The glyph
   programs arrived through StartData as the /GlyphData binary block
   -- the CIDMap, then Type 1 charstrings the FDArray's private
   dictionaries describe. The dictionary is written back out as a
   CIDFont resource file around that block and opened as a memory
   face, which serves glyphs directly by CID. */

static int
_cid_emit_num(Xpost_Context *ctx, Xpost_String_Buffer *b, Xpost_Object v)
{
    (void)ctx;
    if (xpost_object_get_type(v) == integertype)
        return xpost_strbuf_appendf(b, "%d", v.int_.val);
    if (xpost_object_get_type(v) == realtype)
        return xpost_strbuf_appendf(b, "%g", v.real_.val);
    if (xpost_object_get_type(v) == booleantype)
        return xpost_strbuf_appendf(b, "%s", v.int_.val ? "true" : "false");
    return invalidfont;
}

/* emit "/key value def" for one dictionary entry, an array value as a
   bracketed list. A key the dictionary does not hold emits nothing. */
static int
_cid_emit_entry(Xpost_Context *ctx, Xpost_String_Buffer *b,
                Xpost_Object d, const char *key)
{
    Xpost_Object v = xpost_dict_get(ctx, d, xpost_name_cons(ctx, key));
    word i;
    int ret;

    if (xpost_object_get_type(v) == invalidtype)
        return 0;
    if (xpost_object_get_type(v) == arraytype)
    {
        ret = xpost_strbuf_appendf(b, "/%s [", key);
        if (ret)
            return ret;
        for (i = 0; i < v.comp_.sz; i++)
        {
            if (i)
            {
                ret = xpost_strbuf_append(b, " ", 1);
                if (ret)
                    return ret;
            }
            ret = _cid_emit_num(ctx, b, xpost_array_get(ctx, v, i));
            if (ret)
                return ret;
        }
        return xpost_strbuf_appendf(b, "] def\n");
    }
    ret = xpost_strbuf_appendf(b, "/%s ", key);
    if (ret)
        return ret;
    ret = _cid_emit_num(ctx, b, v);
    if (ret)
        return ret;
    return xpost_strbuf_appendf(b, " def\n");
}

static const char *_cid_private_keys[] = {
    "lenIV", "BlueValues", "OtherBlues", "FamilyBlues", "FamilyOtherBlues",
    "BlueScale", "BlueShift", "BlueFuzz", "StdHW", "StdVW",
    "StemSnapH", "StemSnapV", "LanguageGroup", "ForceBold", "RndStemUp",
    "SubrMapOffset", "SDBytes", "SubrCount",
};

static
int _loadcidfont0(Xpost_Context *ctx,
                  Xpost_Object fontdict)
{
#ifdef HAVE_FREETYPE2
    Xpost_Object gdata, fdarray, privatestr, fontbbox;
    Xpost_Object fontbboxarray[4];
    struct fontdata data;
    Xpost_String_Buffer buf;
    unsigned char *whole;
    size_t glen, gpos, wlen;
    int i;
    unsigned int k;
    int ret;

    gdata = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "GlyphData"));
    if (xpost_object_get_type(gdata) == stringtype)
        glen = gdata.comp_.sz;
    else if (xpost_object_get_type(gdata) == arraytype)
    {
        glen = 0;
        /* the emitted program names this index in its own text, so the
           walk counts in the signed type the emitter takes and each
           array's element count is widened into it to be compared */
        for (i = 0; i < (integer)gdata.comp_.sz; i++)
        {
            Xpost_Object s = xpost_array_get(ctx, gdata, i);
            if (xpost_object_get_type(s) != stringtype)
                return invalidfont;
            glen += s.comp_.sz;
        }
    }
    else
        return invalidfont;
    fdarray = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "FDArray"));
    if (xpost_object_get_type(fdarray) != arraytype)
        return invalidfont;

    if (xpost_strbuf_init(&buf, 8192))
        return VMerror;
    if (xpost_strbuf_appendf(&buf,
        "%%!PS-Adobe-3.0 Resource-CIDFont\n"
        "%%%%DocumentNeededResources: ProcSet (CIDInit)\n"
        "%%%%IncludeResource: ProcSet (CIDInit)\n"
        "/CIDInit /ProcSet findresource begin\n"
        "20 dict begin\n"
        "/CIDFontName /X def\n"
        "/CIDFontVersion 1 def\n"
        "/CIDFontType 0 def\n"
        "/CIDSystemInfo 3 dict dup begin\n"
        "  /Registry (Adobe) def\n"
        "  /Ordering (Identity) def\n"
        "  /Supplement 0 def\n"
        "end def\n")) goto fail;
    if (_cid_emit_entry(ctx, &buf, fontdict, "FontMatrix")) goto fail;
    if (_cid_emit_entry(ctx, &buf, fontdict, "FontBBox")) goto fail;
    if (_cid_emit_entry(ctx, &buf, fontdict, "CIDCount")) goto fail;
    if (_cid_emit_entry(ctx, &buf, fontdict, "FDBytes")) goto fail;
    if (_cid_emit_entry(ctx, &buf, fontdict, "GDBytes")) goto fail;
    if (_cid_emit_entry(ctx, &buf, fontdict, "CIDMapOffset")) goto fail;
    if (xpost_strbuf_appendf(&buf, "/FDArray %d array\n", fdarray.comp_.sz))
        goto fail;
    for (i = 0; i < (integer)fdarray.comp_.sz; i++)
    {
        Xpost_Object fd = xpost_array_get(ctx, fdarray, i);
        Xpost_Object priv;

        Xpost_Object topfm, fdfm;
        double m[6] = { 0.001, 0, 0, 0.001, 0, 0 };

        if (xpost_object_get_type(fd) != dicttype)
            goto fail2;
        if (xpost_strbuf_appendf(&buf,
            "%%ADOBeginFontDict\n"
            "dup %d 10 dict begin\n/FontType 1 def\n", i)) goto fail;
        /* the face carries one matrix per dictionary: the product of
           the font's matrix and the dictionary's own, so the glyph
           space the charstrings draw in reaches CID font space the
           way the two-level dictionary said it should */
        topfm = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "FontMatrix"));
        fdfm = xpost_dict_get(ctx, fd, xpost_name_cons(ctx, "FontMatrix"));
        if (xpost_object_get_type(topfm) == arraytype && topfm.comp_.sz == 6
         && xpost_object_get_type(fdfm) == arraytype && fdfm.comp_.sz == 6)
        {
            double a[6], b[6];
            int j;

            for (j = 0; j < 6; j++)
            {
                Xpost_Object v = xpost_array_get(ctx, fdfm, j);
                a[j] = xpost_object_number(v);
                v = xpost_array_get(ctx, topfm, j);
                b[j] = xpost_object_number(v);
            }
            m[0] = a[0]*b[0] + a[1]*b[2];
            m[1] = a[0]*b[1] + a[1]*b[3];
            m[2] = a[2]*b[0] + a[3]*b[2];
            m[3] = a[2]*b[1] + a[3]*b[3];
            m[4] = a[4]*b[0] + a[5]*b[2] + b[4];
            m[5] = a[4]*b[1] + a[5]*b[3] + b[5];
        }
        if (xpost_strbuf_appendf(&buf,
            "/FontMatrix [%g %g %g %g %g %g] def\n",
            m[0], m[1], m[2], m[3], m[4], m[5])) goto fail;
        if (xpost_strbuf_appendf(&buf, "/PaintType 0 def\n/Private 32 dict begin\n"))
            goto fail;
        priv = xpost_dict_get(ctx, fd, xpost_name_cons(ctx, "Private"));
        if (xpost_object_get_type(priv) == dicttype)
            for (k = 0; k < sizeof _cid_private_keys / sizeof *_cid_private_keys; k++)
                if (_cid_emit_entry(ctx, &buf, priv,
                                    _cid_private_keys[k])) goto fail;
        if (xpost_strbuf_appendf(&buf,
            "currentdict end def\ncurrentdict end put\n"
            "%%ADOEndFontDict\n")) goto fail;
    }
    if (xpost_strbuf_appendf(&buf, "def\n(Binary) %lu StartData ",
                             (unsigned long)glen)) goto fail;

    wlen = buf.len + glen;
    whole = malloc(wlen);
    if (!whole)
        goto fail2;
    memcpy(whole, buf.s, buf.len);
    gpos = buf.len;
    if (xpost_object_get_type(gdata) == stringtype)
    {
        memcpy(whole + gpos, xpost_string_get_pointer(ctx, gdata), glen);
    }
    else
    {
        for (i = 0; i < (integer)gdata.comp_.sz; i++)
        {
            Xpost_Object s = xpost_array_get(ctx, gdata, i);
            memcpy(whole + gpos, xpost_string_get_pointer(ctx, s), s.comp_.sz);
            gpos += s.comp_.sz;
        }
    }
    xpost_strbuf_free(&buf);

    /* a rebuilt descendant releases its previous face */
    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == stringtype)
    {
        /* a read that refuses leaves data untouched, and releasing off
           an uninitialised struct is worse than leaking the old face */
        if (xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                             xpost_object_get_ent(privatestr), 0,
                             sizeof data, &data)
         && data.face)
            xpost_font_face_free(data.face);
    }

    data.face = xpost_font_face_new_from_memory(whole, wlen);
    if (data.face == NULL)
    {
        free(whole);
        return invalidfont;
    }

    /* executable, as the reference interpreters answer it */
    fontbbox = xpost_object_cvx(xpost_array_cons(ctx, 4));
    xpost_font_face_get_bbox(data.face, fontbboxarray, 1000.0);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, fontbbox),
                          xpost_object_get_ent(fontbbox),
                          0, 4 * sizeof(Xpost_Object), fontbboxarray))
        return VMerror;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "FontBBox"), fontbbox);
    if (ret)
        return ret;

    privatestr = xpost_string_cons(ctx, sizeof data, NULL);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0,
                          sizeof data, &data))
        return VMerror;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "Private"), privatestr);
    if (ret)
        return ret;
    return 0;
fail:
fail2:
    xpost_strbuf_free(&buf);
    return invalidfont;
#else
    (void)ctx;
    (void)fontdict;
    return invalidfont;
#endif
}


/* fontdict charstrings-flat subrs  .loadfont1  -
   assemble a working face for a Type 1 font defined by an embedded
   program. The interpreted dictionary is written back out as a font
   program -- cleartext header, then an eexec section carrying the
   private dictionary, the subroutines and the charstrings, whose
   own charstring-level encryption the strings still carry -- and
   opened as a memory face. The charstrings arrive flattened as
   name, string pairs, since only the interpreter can walk its
   dictionaries. */

static void
_t1_encrypt(unsigned char *data, size_t n)
{
    unsigned short r = 55665;
    size_t i;

    for (i = 0; i < n; i++)
    {
        unsigned char p = data[i];
        unsigned char c = p ^ (r >> 8);

        data[i] = c;
        r = (unsigned short)((unsigned int)(c + r) * 52845u + 22719u);
    }
}

static int
_t1_emit_bin(Xpost_Context *ctx, Xpost_String_Buffer *b, Xpost_Object s)
{
    return xpost_strbuf_append(b, xpost_string_get_pointer(ctx, s),
                               s.comp_.sz);
}

static
int _loadfont1(Xpost_Context *ctx,
               Xpost_Object fontdict,
               Xpost_Object csflat)
{
#ifdef HAVE_FREETYPE2
    Xpost_Object priv, privatestr, fontbbox, subrs;
    Xpost_Object fontbboxarray[4];
    struct fontdata data;
    Xpost_String_Buffer hdr, sec;
    unsigned char *whole;
    size_t wlen;
    int i;
    unsigned int k;
    int ret;

    if (xpost_object_get_type(csflat) != arraytype)
        return invalidfont;
    priv = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(priv) != dicttype)
        return invalidfont;
    /* The subroutine array lives in the Private dictionary, which a Type 1
       font seals no-access. Read it here in C, where the access attribute does
       not apply, rather than from the PostScript loader, where it would forbid
       the read. An absent or non-array Subrs means no subroutines. */
    subrs = xpost_dict_get(ctx, priv, xpost_name_cons(ctx, "Subrs"));

    if (xpost_strbuf_init(&hdr, 2048))
        return VMerror;
    if (xpost_strbuf_appendf(&hdr,
        "%%!PS-AdobeFont-1.0: X 001.001\n"
        "11 dict begin\n"
        "/FontName /X def\n"
        "/FontType 1 def\n"
        "/PaintType 0 def\n")) goto failh;
    if (_cid_emit_entry(ctx, &hdr, fontdict, "FontMatrix")) goto failh;
    if (_cid_emit_entry(ctx, &hdr, fontdict, "FontBBox")) goto failh;
    if (xpost_strbuf_appendf(&hdr,
        "/Encoding StandardEncoding def\n"
        "currentdict end\n"
        "currentfile eexec\n")) goto failh;

    if (xpost_strbuf_init(&sec, 16384))
        goto failh;
    /* four salt bytes ahead of the program proper */
    if (xpost_strbuf_appendf(&sec, "XPT1"
        "dup /Private 16 dict dup begin\n"
        "/RD {string currentfile exch readstring pop} executeonly def\n"
        "/ND {noaccess def} executeonly def\n"
        "/NP {noaccess put} executeonly def\n"
        "/password 5839 def\n"
        "/MinFeature {16 16} def\n")) goto fails;
    for (k = 0; k < sizeof _cid_private_keys / sizeof *_cid_private_keys; k++)
        if (_cid_emit_entry(ctx, &sec, priv,
                            _cid_private_keys[k])) goto fails;
    if (xpost_object_get_type(subrs) == arraytype && subrs.comp_.sz > 0)
    {
        if (xpost_strbuf_appendf(&sec, "/Subrs %d array\n", subrs.comp_.sz))
            goto fails;
        for (i = 0; i < (integer)subrs.comp_.sz; i++)
        {
            Xpost_Object s = xpost_array_get(ctx, subrs, i);

            /* A Type 1 font may over-allocate its Subrs array and leave slots
               unfilled (null): CMR10 declares 38 and fills only 15. Emit a
               minimal charstring-encrypted "return" for such a slot, so a
               callsubr on it is a harmless no-op the way the reference
               interpreters treat an empty subroutine, rather than rejecting the
               whole font (or leaving a hole freetype faults on). */
            if (xpost_object_get_type(s) != stringtype)
            {
                unsigned char cs[5];
                unsigned short rr = 4330;
                int j;

                cs[0] = cs[1] = cs[2] = cs[3] = 0; /* lenIV skip bytes */
                cs[4] = 11;                        /* charstring: return */
                for (j = 0; j < 5; j++)
                {
                    unsigned char c = (unsigned char)(cs[j] ^ (rr >> 8));
                    cs[j] = c;
                    rr = (unsigned short)(((unsigned int)(c + rr)) * 52845u + 22719u);
                }
                if (xpost_strbuf_appendf(&sec, "dup %d 5 RD ", i)) goto fails;
                if (xpost_strbuf_append(&sec, cs, 5)) goto fails;
                if (xpost_strbuf_appendf(&sec, " NP\n")) goto fails;
                continue;
            }
            if (xpost_strbuf_appendf(&sec, "dup %d %u RD ", i,
                                     (unsigned int)s.comp_.sz)) goto fails;
            if (_t1_emit_bin(ctx, &sec, s)) goto fails;
            if (xpost_strbuf_appendf(&sec, " NP\n")) goto fails;
        }
        if (xpost_strbuf_appendf(&sec, "ND\n")) goto fails;
    }
    if (xpost_strbuf_appendf(&sec, "end put\n"
        "dup /CharStrings %d dict dup begin\n", csflat.comp_.sz / 2 + 1))
        goto fails;
    for (i = 0; i + 1 < (integer)csflat.comp_.sz; i += 2)
    {
        Xpost_Object nm = xpost_array_get(ctx, csflat, i);
        Xpost_Object s = xpost_array_get(ctx, csflat, i + 1);
        Xpost_Object nstr;
        char nbuf[128];

        if (xpost_object_get_type(s) != stringtype)
            continue;
        if (xpost_object_get_type(nm) != nametype)
            continue;
        nstr = xpost_name_get_string(ctx, nm);
        if (nstr.comp_.sz >= sizeof nbuf)
            continue;
        memcpy(nbuf, xpost_string_get_pointer(ctx, nstr), nstr.comp_.sz);
        nbuf[nstr.comp_.sz] = 0;
        if (xpost_strbuf_appendf(&sec, "/%s %u RD ", nbuf,
                                 (unsigned int)s.comp_.sz)) goto fails;
        if (_t1_emit_bin(ctx, &sec, s)) goto fails;
        if (xpost_strbuf_appendf(&sec, " ND\n")) goto fails;
    }
    if (xpost_strbuf_appendf(&sec,
        "end end put put\n"
        "dup /FontName get exch definefont pop\n"
        "mark currentfile closefile\n")) goto fails;

    /* a reader decides hex against the first four cipher bytes, so
       the salt must not encrypt to four hexadecimal characters */
    for (;;)
    {
        unsigned char t[4];
        unsigned short r = 55665;
        int j, allhex = 1;

        for (j = 0; j < 4; j++)
        {
            unsigned char cc = (unsigned char)(sec.s[j] ^ (r >> 8));

            t[j] = cc;
            r = (unsigned short)((unsigned int)(cc + r) * 52845u + 22719u);
        }
        for (j = 0; j < 4; j++)
            if (!( (t[j] >= '0' && t[j] <= '9')
                || (t[j] >= 'a' && t[j] <= 'f')
                || (t[j] >= 'A' && t[j] <= 'F') ))
                allhex = 0;
        if (!allhex)
            break;
        sec.s[0]++;   /* different salt, different ciphertext */
    }

    wlen = hdr.len + sec.len + 30;
    whole = malloc(wlen + 4);
    if (!whole)
        goto fails;
    memcpy(whole, hdr.s, hdr.len);
    memcpy(whole + hdr.len, sec.s, sec.len);
    _t1_encrypt(whole + hdr.len, sec.len);
    memcpy(whole + hdr.len + sec.len, "\n0000000000000000\ncleartomark\n", 30);
    xpost_strbuf_free(&hdr);
    xpost_strbuf_free(&sec);

    data.face = xpost_font_face_new_from_memory(whole, wlen);
    if (data.face == NULL)
    {
        free(whole);
        return invalidfont;
    }

    /* executable, as the reference interpreters answer it */
    fontbbox = xpost_object_cvx(xpost_array_cons(ctx, 4));
    xpost_font_face_get_bbox(data.face, fontbboxarray, 1000.0);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, fontbbox),
                          xpost_object_get_ent(fontbbox),
                          0, 4 * sizeof(Xpost_Object), fontbboxarray))
        return VMerror;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "FontBBox"), fontbbox);
    if (ret)
        return ret;

    privatestr = xpost_string_cons(ctx, sizeof data, NULL);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0,
                          sizeof data, &data))
        return VMerror;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "Private"), privatestr);
    if (ret)
        return ret;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, ".emunits"),
                       xpost_int_cons(xpost_font_face_units(data.face)));
    if (ret)
        return ret;
    return 0;
fails:
    xpost_strbuf_free(&sec);
failh:
    xpost_strbuf_free(&hdr);
    return invalidfont;
#else
    (void)ctx; (void)fontdict; (void)csflat;
    return invalidfont;
#endif
}


/* maskdict  .stencilaa  bool
   paint a small stencil mask with coverage-blended edges, the way
   glyph bitmaps paint: an axis-aligned transform lets each device
   pixel take the box-filtered coverage of the mask cells it spans,
   fully covered pixels going through the device's solid path and
   partial ones through its blend. Anything else -- a rotated or
   skewed matrix, an oversized result, a device without the blending
   machinery -- answers false and the caller keeps the bilevel path. */
static
int _stencilaa(Xpost_Context *ctx,
               Xpost_Object dict)
{
    Xpost_Object userdict, gd, gs, devdic, putpix;
    Xpost_Object buf, mat, o;
    textstate ts;
    int w, h, ink, ncomp, interp = 0;
    Xpost_Object comp[4];
    double m[6];
    double fx0, fx1, fy0, fy1, xa, xb, ya, yb, full;
    int ix0, iy0, devw, devh, px, py, i;
    int rowbytes;
    unsigned char *bits, *cov;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        goto refuse;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    devdic = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "device"));
    if (xpost_object_get_type(devdic) != dicttype)
        goto refuse;

    memset(&ts, 0, sizeof ts);
    ts.blendpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "BlendPix"));
    o = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "TextAlphaBits"));
    ts.blend = (xpost_object_get_type(ts.blendpix) == operatortype
             || (xpost_object_get_type(ts.blendpix) == arraytype
              && xpost_object_is_exe(ts.blendpix)))
            && xpost_object_get_type(o) == integertype
            && o.int_.val > 1;
    if (!ts.blend)
        goto refuse;
    putpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "PutPix"));

    buf = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "buf"));
    if (xpost_object_get_type(buf) != stringtype)
        goto refuse;
    o = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "width"));
    if (xpost_object_get_type(o) != integertype) goto refuse;
    w = o.int_.val;
    o = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "height"));
    if (xpost_object_get_type(o) != integertype) goto refuse;
    h = o.int_.val;
    o = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "ink"));
    if (xpost_object_get_type(o) != integertype) goto refuse;
    ink = o.int_.val;
    mat = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "mat"));
    if (xpost_object_get_type(mat) != arraytype || mat.comp_.sz != 6)
        goto refuse;
    o = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, "interp"));
    interp = xpost_object_get_type(o) == booleantype && o.int_.val;
    for (i = 0; i < 6; i++)
    {
        o = xpost_array_get(ctx, mat, i);
        m[i] = xpost_object_get_type(o) == realtype ? o.real_.val
             : xpost_object_get_type(o) == integertype ? (double)o.int_.val
             : 0.0;
    }
    if (w <= 0 || h <= 0)
        goto refuse;
    rowbytes = (w + 7) / 8;
    /* the mask has to fit its buffer, and the buffer's capacity counted
       in rows answers that: the byte count the two dimensions multiply
       to need not itself stay within the integer range, and a mask
       whose product wraps must be refused rather than sampled */
    if ((integer)(buf.comp_.sz / (dword)rowbytes) < h)
        goto refuse;
    /* coverage integrates separably only over an axis-aligned map */
    if (fabs(m[1]) > 1e-4 || fabs(m[2]) > 1e-4
     || fabs(m[0]) < 1e-6 || fabs(m[3]) < 1e-6)
        goto refuse;

    xa = m[4]; xb = m[0] * w + m[4];
    fx0 = xa < xb ? xa : xb; fx1 = xa < xb ? xb : xa;
    ya = m[5]; yb = m[3] * h + m[5];
    fy0 = ya < yb ? ya : yb; fy1 = ya < yb ? yb : ya;
    ix0 = (int)floor(fx0); iy0 = (int)floor(fy0);
    devw = (int)ceil(fx1) - ix0;
    devh = (int)ceil(fy1) - iy0;
    if (devw <= 0 || devh <= 0 || devw > 4096 || devh > 4096
     || devw * devh > (1 << 20))
        goto refuse;

    cov = malloc((size_t)devw * devh);
    if (!cov)
        goto refuse;
    bits = (unsigned char *)xpost_string_get_pointer(ctx, buf);
    full = (1.0 / fabs(m[0])) * (1.0 / fabs(m[3]));

    for (py = 0; py < devh; py++)
    {
        double dy0 = iy0 + py, dy1 = dy0 + 1;
        double my0 = (dy0 - m[5]) / m[3], my1 = (dy1 - m[5]) / m[3];
        double t;
        int yi, yi0, yi1;

        if (my0 > my1) { t = my0; my0 = my1; my1 = t; }
        if (my0 < 0) my0 = 0;
        if (my1 > h) my1 = h;
        yi0 = (int)floor(my0); yi1 = (int)ceil(my1);
        for (px = 0; px < devw; px++)
        {
            double dx0 = ix0 + px, dx1 = dx0 + 1;
            double mx0 = (dx0 - m[4]) / m[0], mx1 = (dx1 - m[4]) / m[0];
            double acc = 0.0;
            int xi, xi0, xi1;

            if (mx0 > mx1) { t = mx0; mx0 = mx1; mx1 = t; }
            if (mx0 < 0) mx0 = 0;
            if (mx1 > w) mx1 = w;
            /* an interpolated mask magnified past its cells ramps
               between them: sample the field bilinearly at the pixel
               centre instead of box-filtering within one cell */
            if (interp && mx1 - mx0 < 1.0 && my1 - my0 < 1.0
             && mx1 > mx0 && my1 > my0)
            {
                double cx = (mx0 + mx1) * 0.5 - 0.5;
                double cy = (my0 + my1) * 0.5 - 0.5;
                int bx = (int)floor(cx), by = (int)floor(cy);
                double fx = cx - bx, fy = cy - by;
                double v = 0.0;
                int dx, dy;

                for (dy = 0; dy < 2; dy++)
                    for (dx = 0; dx < 2; dx++)
                    {
                        int sx = bx + dx, sy = by + dy;
                        double wt = (dx ? fx : 1.0 - fx)
                                  * (dy ? fy : 1.0 - fy);
                        int bit;

                        if (sx < 0 || sx >= w || sy < 0 || sy >= h)
                            continue;
                        bit = (bits[sy * rowbytes + sx / 8]
                               >> (7 - (sx % 8))) & 1;
                        if (bit == ink)
                            v += wt;
                    }
                acc = v * 255.0 + 0.5;
                cov[py * devw + px] = acc >= 255.0 ? 255
                                    : acc <= 0.0 ? 0 : (unsigned char)acc;
                continue;
            }
            xi0 = (int)floor(mx0); xi1 = (int)ceil(mx1);
            for (yi = yi0; yi < yi1; yi++)
            {
                double wy = (yi + 1 < my1 ? yi + 1 : my1)
                          - (yi > my0 ? yi : my0);

                if (wy <= 0)
                    continue;
                for (xi = xi0; xi < xi1; xi++)
                {
                    double wx = (xi + 1 < mx1 ? xi + 1 : mx1)
                              - (xi > mx0 ? xi : mx0);
                    int bit;

                    if (wx <= 0)
                        continue;
                    bit = (bits[yi * rowbytes + xi / 8] >> (7 - (xi % 8))) & 1;
                    if (bit == ink)
                        acc += wx * wy;
                }
            }
            acc = acc / full * 255.0 + 0.5;
            cov[py * devw + px] = acc >= 255.0 ? 255
                                : acc <= 0.0 ? 0 : (unsigned char)acc;
        }
    }

    if (_device_color(ctx, gs, devdic, &ncomp, comp))
    {
        free(cov);
        goto refuse;
    }
    /* the answer goes under the queue: the painter stacks one entry
       per pixel above it, and each entry consumes its own operands
       before the caller sees the boolean */
    xpost_stack_push(ctx->lo, ctx->os, xpost_bool_cons(1));
    _draw_bitmap(ctx, devdic, putpix, &ts, cov, devh, devw, devw,
                 XPOST_FONT_PIXEL_MODE_GRAY, ix0, iy0,
                 ncomp, comp[0], comp[1], comp[2], comp[3]);
    free(cov);
    return 0;
refuse:
    xpost_stack_push(ctx->lo, ctx->os, xpost_bool_cons(0));
    return 0;
}

/* Type 3 glyphs cached through setcachedevice: the key is the font's
   serial and the character code under the exact text-to-device
   transform, quantized as the face transforms are. The store is the
   glyph cache; the raster is a coverage mask captured from the build
   procedure's marks. */

static int
_mask_key(Xpost_Context *ctx, Xpost_Object key,
          Xpost_Object mat, unsigned long *k2, long m[4])
{
    int i;

    /* The caller says what the mask is of. A glyph combines its font's
       serial with the character code; anything else combines whatever
       distinguishes one of its masks from another. The cache neither
       knows nor needs to know which. */
    if (xpost_object_get_type(key) != integertype)
        return 0;
    *k2 = (unsigned long)key.int_.val;
    if (xpost_object_get_type(mat) != arraytype || mat.comp_.sz != 6)
        return 0;
    for (i = 0; i < 4; i++)
    {
        Xpost_Object el = xpost_array_get(ctx, mat, i);
        double v = xpost_object_get_type(el) == realtype ? el.real_.val
                 : xpost_object_get_type(el) == integertype ? (double)el.int_.val
                 : 0.0;

        m[i] = (long)(v * 0x10000L);
    }
    return 1;
}

/* x y mat key cliparr  .maskcachehit  n0 n1 true
                                       false
   Paint a cached mask at the device origin (x y) in the current colour,
   and answer the two numbers filed with it. A mask whose raster leaves
   the clip rectangle [x0 y0 x1 y1] answers false, and the caller paints
   it the long way, clipped.

   The mask is coverage, painted in whatever colour is current: that is
   what makes one cache serve a glyph and an uncoloured pattern cell
   alike. What the two numbers mean is the caller's business -- a glyph
   files its advances there. */
static
int _maskcachehit(Xpost_Context *ctx,
                  Xpost_Object x,
                  Xpost_Object y,
                  Xpost_Object mat,
                  Xpost_Object key,
                  Xpost_Object cliparr)
{
    Xpost_Object userdict, gd, gs, devdic, putpix, o;
    textstate ts;
    unsigned long k2;
    long m[4];
    unsigned char *bits;
    int rows, width, pitch, left, top;
    long ax, ay;
    int ncomp;
    Xpost_Object comp[4];
    double dx, dy;

    if (!_mask_key(ctx, key, mat, &k2, m))
        goto refuse;
    if (!xpost_mask_cache_lookup(NULL, k2, m, 0,
                                      &bits, &rows, &width, &pitch,
                                      &left, &top, &ax, &ay))
        goto refuse;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        goto refuse;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    devdic = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "device"));
    if (xpost_object_get_type(devdic) != dicttype)
        goto refuse;

    memset(&ts, 0, sizeof ts);
    ts.blendpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "BlendPix"));
    o = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "TextAlphaBits"));
    ts.blend = (xpost_object_get_type(ts.blendpix) == operatortype
             || (xpost_object_get_type(ts.blendpix) == arraytype
              && xpost_object_is_exe(ts.blendpix)))
            && xpost_object_get_type(o) == integertype
            && o.int_.val > 1;
    putpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "PutPix"));

    if (_device_color(ctx, gs, devdic, &ncomp, comp))
        goto refuse;

    dx = xpost_object_number(x);
    dy = xpost_object_number(y);

    {
        double c[4];
        int i;
        int gx = (int)floor(dx + 0.5) + left;
        int gy = (int)floor(dy + 0.5) - top;

        if (xpost_object_get_type(cliparr) != arraytype
         || cliparr.comp_.sz != 4)
            goto refuse;
        for (i = 0; i < 4; i++)
        {
            Xpost_Object el = xpost_array_get(ctx, cliparr, i);

            c[i] = xpost_object_get_type(el) == realtype ? el.real_.val
                 : xpost_object_get_type(el) == integertype
                 ? (double)el.int_.val : 0.0;
        }
        if (gx < c[0] || gy < c[1]
         || gx + width > c[2] || gy + rows > c[3])
            goto refuse;
    }

    /* the answers go under the queue: the painter stacks one entry
       per pixel above them, and each entry consumes its own operands
       before the caller sees the boolean */
    xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons((real)(ax / 65536.0)));
    xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons((real)(ay / 65536.0)));
    xpost_stack_push(ctx->lo, ctx->os, xpost_bool_cons(1));
    _draw_bitmap(ctx, devdic, putpix, &ts,
                 bits, rows, width, pitch,
                 XPOST_FONT_PIXEL_MODE_GRAY,
                 (int)floor(dx + 0.5) + left,
                 (int)floor(dy + 0.5) - top,
                 ncomp, comp[0], comp[1], comp[2], comp[3]);
    return 0;
refuse:
    xpost_stack_push(ctx->lo, ctx->os, xpost_bool_cons(0));
    return 0;
}

/* capdict  .maskcacheput  -
   insert a captured glyph mask: buf holds width x height coverage
   bytes whose raster origin sits at device (bx0 by0), the glyph
   origin was at device (ox oy), advances are character-space, and
   mat and key identify the entry as .maskcachehit reads it */
static
int _maskcacheput(Xpost_Context *ctx,
                Xpost_Object dict)
{
    Xpost_Object o, buf, mat, key;
    unsigned long k2;
    long m[4];
    int w, h, bx0, by0, left, top;
    double ox, oy, advx, advy;
    unsigned char *bytes;

#define DGET(name, var, want) do {     o = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, name));     if (xpost_object_get_type(o) != want) return typecheck;     var = o; } while (0)
#define DNUM(name, var) do {     o = xpost_dict_get(ctx, dict, xpost_name_cons(ctx, name));     if (xpost_object_get_type(o) == realtype) var = o.real_.val;     else if (xpost_object_get_type(o) == integertype) var = (double)o.int_.val;     else return typecheck; } while (0)
    DGET("buf", buf, stringtype);
    DGET("mat", mat, arraytype);
    DGET("key", key, integertype);
    { double t; DNUM("w", t); w = (int)t; }
    { double t; DNUM("h", t); h = (int)t; }
    { double t; DNUM("bx0", t); bx0 = (int)t; }
    { double t; DNUM("by0", t); by0 = (int)t; }
    DNUM("ox", ox);
    DNUM("oy", oy);
    DNUM("advx", advx);
    DNUM("advy", advy);
#undef DGET
#undef DNUM
    /* the mask is read as h rows of w coverage bytes out of buf, and
       the dimensions come out of a dictionary a program can build, so
       buf has to hold that many bytes; the row count is compared
       against the length divided by the row width, which holds for
       every pair of dimensions rather than only those whose product
       fits the type a multiplication would form it in */
    if (w <= 0 || h <= 0 || (unsigned int)h > buf.comp_.sz / (unsigned int)w)
        return rangecheck;
    if (!_mask_key(ctx, key, mat, &k2, m))
        return 0;
    bytes = (unsigned char *)xpost_string_get_pointer(ctx, buf);
    left = bx0 - (int)floor(ox + 0.5);
    top = (int)floor(oy + 0.5) - by0;
    (void)xpost_mask_cache_insert(NULL, k2, m, 0,
                                       bytes, h, w, w, left, top,
                                       (long)(advx * 65536.0),
                                       (long)(advy * 65536.0));
    return 0;
}

/* ciddict glypharray  .loadcidfont2  -
   assemble a working TrueType face for a CIDFontType 2 dictionary.
   The /sfnts strings supply every table but the outlines: the glyphs
   arrive in /GlyphDirectory, delivered incrementally by glyph index,
   and the caller flattens the directory into an array indexed by
   glyph (null where none has arrived). A fresh glyf table and a
   long-format loca are synthesized around the delivered outlines,
   maxp's glyph count and head's loca format patched to match, and
   the whole reassembled program opened as a memory face stored in
   /Private. Called again after the directory has grown, the previous
   face is released and rebuilt around the larger complement. */
static
int _loadcidfont2(Xpost_Context *ctx,
                  Xpost_Object fontdict,
                  Xpost_Object glyphs)
{
#ifdef HAVE_FREETYPE2
    Xpost_Object sfnts;
    Xpost_Object privatestr;
    Xpost_Object fontbbox;
    Xpost_Object fontbboxarray[4];
    struct fontdata data;
    unsigned char *buf = NULL, *out = NULL;
    size_t total, glyftotal, outtotal, pos;
    unsigned int ntab, nglyphs;
    unsigned int headoff = 0, maxpoff = 0;
    int i;
    int ret;

    sfnts = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "sfnts"));
    if (xpost_object_get_type(sfnts) != arraytype)
        return invalidfont;
    total = 0;
    /* the walk counts in the signed type the table indexing below uses,
       so the array's element count is widened into it to be compared */
    for (i = 0; i < (integer)sfnts.comp_.sz; i++)
    {
        Xpost_Object s = xpost_array_get(ctx, sfnts, i);
        if (xpost_object_get_type(s) != stringtype)
            return invalidfont;
        total += s.comp_.sz;
    }
    if (total < 12)
        return invalidfont;
    buf = malloc(total);
    if (!buf)
        return VMerror;
    total = 0;
    for (i = 0; i < (integer)sfnts.comp_.sz; i++)
    {
        Xpost_Object s = xpost_array_get(ctx, sfnts, i);
        memcpy(buf + total, xpost_string_get_pointer(ctx, s), s.comp_.sz);
        total += s.comp_.sz;
    }

    ntab = _sfnt_u16(buf + 4);
    if (12 + 16 * (size_t)ntab > total)
    {
        free(buf);
        return invalidfont;
    }

    nglyphs = glyphs.comp_.sz;
    glyftotal = 0;
    for (i = 0; i < (int)nglyphs; i++)
    {
        Xpost_Object g = xpost_array_get(ctx, glyphs, i);
        if (xpost_object_get_type(g) == stringtype)
            glyftotal += (g.comp_.sz + 1) & ~(size_t)1;
    }

    /* rebuild the directory: glyf and loca are synthesized (the
       template may omit them entirely, carrying a gdir placeholder
       for the incremental download instead), the hinting programs
       and the placeholder are dropped -- a subset template's
       bytecode does not survive its stripping and fails every glyph
       under the bytecode interpreter -- and everything else is
       carried over */
    {
        int has_glyf = 0, has_loca = 0;
        unsigned int newntab = 0, w = 0;

        out = NULL;
        for (i = 0; i < (int)ntab; i++)
        {
            unsigned int tag = _sfnt_u32(buf + 12 + 16 * i);
            if (tag == 0x63767420 || tag == 0x6670676d
             || tag == 0x70726570 || tag == 0x67646972)
                continue;
            if (tag == 0x676c7966) has_glyf = 1;
            if (tag == 0x6c6f6361) has_loca = 1;
            newntab++;
        }
        newntab += !has_glyf + !has_loca;

        outtotal = 12 + 16 * (size_t)newntab;
        for (i = 0; i < (int)ntab; i++)
        {
            unsigned char *e = buf + 12 + 16 * i;
            unsigned int tag = _sfnt_u32(e);
            size_t len;
            if (tag == 0x63767420 || tag == 0x6670676d
             || tag == 0x70726570 || tag == 0x67646972)
                continue;
            if (tag == 0x676c7966)
                len = glyftotal;
            else if (tag == 0x6c6f6361)
                len = 4 * ((size_t)nglyphs + 1);
            else
                len = _sfnt_u32(e + 12);
            outtotal = (outtotal + 3) & ~(size_t)3;
            outtotal += len;
        }
        if (!has_glyf)
        {
            outtotal = (outtotal + 3) & ~(size_t)3;
            outtotal += glyftotal;
        }
        if (!has_loca)
        {
            outtotal = (outtotal + 3) & ~(size_t)3;
            outtotal += 4 * ((size_t)nglyphs + 1);
        }
        out = malloc(outtotal);
        if (!out)
        {
            free(buf);
            return VMerror;
        }
        memcpy(out, buf, 12);
        _sfnt_put16(out + 4, newntab);
        for (i = 0; i < (int)ntab; i++)
        {
            unsigned char *e = buf + 12 + 16 * i;
            unsigned int tag = _sfnt_u32(e);
            if (tag == 0x63767420 || tag == 0x6670676d
             || tag == 0x70726570 || tag == 0x67646972)
                continue;
            memcpy(out + 12 + 16 * w, e, 16);
            w++;
        }
        pos = 12 + 16 * (size_t)w;
        if (!has_glyf)
        {
            memset(out + pos, 0, 16);
            _sfnt_put32(out + pos, 0x676c7966);
            pos += 16;
            w++;
        }
        if (!has_loca)
        {
            memset(out + pos, 0, 16);
            _sfnt_put32(out + pos, 0x6c6f6361);
            pos += 16;
            w++;
        }
        ntab = newntab;
    }

    pos = 12 + 16 * (size_t)ntab;
    for (i = 0; i < (int)ntab; i++)
    {
        unsigned char *e = out + 12 + 16 * i;
        unsigned int tag = _sfnt_u32(e);
        unsigned int srcoff = _sfnt_u32(e + 8);
        unsigned int srclen = _sfnt_u32(e + 12);

        pos = (pos + 3) & ~(size_t)3;
        if (tag == 0x676c7966)
        {
            size_t gp = 0;
            int gi;
            for (gi = 0; gi < (int)nglyphs; gi++)
            {
                Xpost_Object g = xpost_array_get(ctx, glyphs, gi);
                if (xpost_object_get_type(g) == stringtype)
                {
                    memcpy(out + pos + gp,
                           xpost_string_get_pointer(ctx, g), g.comp_.sz);
                    if (g.comp_.sz & 1)
                        out[pos + gp + g.comp_.sz] = 0;
                    gp += (g.comp_.sz + 1) & ~(size_t)1;
                }
            }
            _sfnt_put32(e + 8, (unsigned int)pos);
            _sfnt_put32(e + 12, (unsigned int)glyftotal);
            pos += glyftotal;
        }
        else if (tag == 0x6c6f6361)
        {
            size_t gp = 0;
            int gi;
            for (gi = 0; gi <= (int)nglyphs; gi++)
            {
                _sfnt_put32(out + pos + 4 * gi, (unsigned int)gp);
                if (gi < (int)nglyphs)
                {
                    Xpost_Object g = xpost_array_get(ctx, glyphs, gi);
                    if (xpost_object_get_type(g) == stringtype)
                        gp += (g.comp_.sz + 1) & ~(size_t)1;
                }
            }
            _sfnt_put32(e + 8, (unsigned int)pos);
            _sfnt_put32(e + 12, 4 * (nglyphs + 1));
            pos += 4 * ((size_t)nglyphs + 1);
        }
        else
        {
            if ((size_t)srcoff + srclen > total)
            {
                free(buf); free(out);
                return invalidfont;
            }
            memcpy(out + pos, buf + srcoff, srclen);
            if (tag == 0x68656164) headoff = (unsigned int)pos;
            if (tag == 0x6d617870) maxpoff = (unsigned int)pos;
            _sfnt_put32(e + 8, (unsigned int)pos);
            pos += srclen;
        }
    }
    free(buf);
    if (headoff && headoff + 52 <= outtotal)
        _sfnt_put16(out + headoff + 50, 1);   /* long loca offsets */
    if (maxpoff && maxpoff + 6 <= outtotal)
        _sfnt_put16(out + maxpoff + 4, nglyphs);

    /* replace any previous face: the directory grows between shows */
    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == stringtype)
    {
        /* a read that refuses leaves data untouched, and releasing off
           an uninitialised struct is worse than leaking the old face */
        if (xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                             xpost_object_get_ent(privatestr), 0,
                             sizeof data, &data)
         && data.face)
            xpost_font_face_free(data.face);
    }

    data.face = xpost_font_face_new_from_memory(out, outtotal);
    if (data.face == NULL)
    {
        free(out);
        return invalidfont;
    }

    /* executable, as the reference interpreters answer it */
    fontbbox = xpost_object_cvx(xpost_array_cons(ctx, 4));
    xpost_font_face_get_bbox(data.face, fontbboxarray, 1.0);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, fontbbox),
                          xpost_object_get_ent(fontbbox),
                          0, 4 * sizeof(Xpost_Object), fontbboxarray))
        return VMerror;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "FontBBox"), fontbbox);
    if (ret)
        return ret;

    privatestr = xpost_string_cons(ctx, sizeof data, NULL);
    if (!xpost_memory_put(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0,
                          sizeof data, &data))
        return VMerror;
    ret = xpost_dict_put(ctx, fontdict, xpost_name_cons(ctx, "Private"), privatestr);
    if (ret)
        return ret;
    return 0;
#else
    (void)ctx;
    (void)fontdict;
    (void)glyphs;
    return invalidfont;
#endif
}

static
int _ashow(Xpost_Context *ctx,
           Xpost_Object dx,
           Xpost_Object dy,
           Xpost_Object str)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    Xpost_Object fontdict;
    Xpost_Object privatestr;
    struct fontdata data;
    char *cstr;
    real xpos, ypos;
    char *ch;
    Xpost_Object devdic;
    Xpost_Object putpix;
    textstate ts;
    int ncomp;
    Xpost_Object comp[4];
    Xpost_Object finalize;
    int ret;


    /* load the graphicsdict, current graphics state, and current font */
    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    fontdict = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currfont"));
    if (xpost_object_get_type(fontdict) == invalidtype)
        return invalidfont;
    XPOST_LOG_INFO("loaded graphicsdict, graphics state, and current font");

    /* load the device and PutPix member function */
    devdic = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "device"));
    putpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "PutPix"));
    XPOST_LOG_INFO("loaded DEVICE and PutPix");
    ts = _text_state_get(ctx, gs, fontdict, devdic);

    /* get the font data from the font dict */
    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == invalidtype)
        return invalidfont;
    if (!xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0, sizeof data,
                          &data)
     || data.face == NULL)
    {
        XPOST_LOG_ERR("face is NULL");
        return invalidfont;
    }
    _face_setup(ctx, gs, fontdict, data.face);
    XPOST_LOG_INFO("loaded font data from dict");

    /* get a c-style nul-terminated string */
    cstr = xpost_string_allocate_cstring(ctx, str);
    XPOST_LOG_INFO("append nul to string");

    ret = _get_current_point(ctx, gs, &xpos, &ypos);
    if (ret){
        free(cstr);
        return ret;
    }

    if (_device_color(ctx, gs, devdic, &ncomp, comp))
    {
        free(cstr);
        return unregistered;
    }
    XPOST_LOG_INFO("ncomp = %d", ncomp);

    ret = _show_finalize_cons(ctx, xpos, ypos, &finalize);
    if (ret)
    {
        free(cstr);
        return ret;
    }
    xpost_stack_push(ctx->lo, ctx->es, finalize);

    /* render text in char *cstr  with font data  at pen position xpos ypos */
    for (ch = cstr; *ch; ch++)
    {
        _show_char(ctx, devdic, putpix, data, &ts, &xpos, &ypos, (unsigned char)*ch,
                   ncomp, comp[0], comp[1], comp[2], comp[3]);
        xpos += dx.real_.val;
        ypos += dy.real_.val;
    }

    /* update current position in the graphics state */
    ret = _show_finalize_pos(ctx, finalize, xpos, ypos);

    free(cstr);
    return ret;
}

static
int _widthshow(Xpost_Context *ctx,
               Xpost_Object cx,
               Xpost_Object cy,
               Xpost_Object charcode,
               Xpost_Object str)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    Xpost_Object fontdict;
    Xpost_Object privatestr;
    struct fontdata data;
    char *cstr;
    real xpos, ypos;
    char *ch;
    Xpost_Object devdic;
    Xpost_Object putpix;
    textstate ts;
    int ncomp;
    Xpost_Object comp[4];
    Xpost_Object finalize;
    int ret;


    /* load the graphicsdict, current graphics state, and current font */
    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    fontdict = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currfont"));
    if (xpost_object_get_type(fontdict) == invalidtype)
        return invalidfont;
    XPOST_LOG_INFO("loaded graphicsdict, graphics state, and current font");

    /* load the device and PutPix member function */
    devdic = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "device"));
    putpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "PutPix"));
    XPOST_LOG_INFO("loaded DEVICE and PutPix");
    ts = _text_state_get(ctx, gs, fontdict, devdic);

    /* get the font data from the font dict */
    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == invalidtype)
        return invalidfont;
    if (!xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0, sizeof data,
                          &data)
     || data.face == NULL)
    {
        XPOST_LOG_ERR("face is NULL");
        return invalidfont;
    }
    _face_setup(ctx, gs, fontdict, data.face);
    XPOST_LOG_INFO("loaded font data from dict");

    /* get a c-style nul-terminated string */
    cstr = xpost_string_allocate_cstring(ctx, str);
    XPOST_LOG_INFO("append nul to string");

    ret = _get_current_point(ctx, gs, &xpos, &ypos);
    if (ret){
        free(cstr);
        return ret;
    }

    if (_device_color(ctx, gs, devdic, &ncomp, comp))
    {
        free(cstr);
        return unregistered;
    }
    XPOST_LOG_INFO("ncomp = %d", ncomp);

    ret = _show_finalize_cons(ctx, xpos, ypos, &finalize);
    if (ret)
    {
        free(cstr);
        return ret;
    }
    xpost_stack_push(ctx->lo, ctx->es, finalize);

    /* render text in char *cstr  with font data  at pen position xpos ypos */
    for (ch = cstr; *ch; ch++)
    {
        _show_char(ctx, devdic, putpix, data, &ts, &xpos, &ypos, (unsigned char)*ch,
                   ncomp, comp[0], comp[1], comp[2], comp[3]);
        if ((unsigned char)*ch == charcode.int_.val)
        {
            xpos += cx.real_.val;
            ypos += cy.real_.val;
        }
    }

    /* update current position in the graphics state */
    ret = _show_finalize_pos(ctx, finalize, xpos, ypos);

    free(cstr);
    return ret;
}

static
int _awidthshow(Xpost_Context *ctx,
                Xpost_Object cx,
                Xpost_Object cy,
                Xpost_Object charcode,
                Xpost_Object dx,
                Xpost_Object dy,
                Xpost_Object str)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    Xpost_Object fontdict;
    Xpost_Object privatestr;
    struct fontdata data;
    char *cstr;
    real xpos, ypos;
    char *ch;
    Xpost_Object devdic;
    Xpost_Object putpix;
    textstate ts;
    int ncomp;
    Xpost_Object comp[4];
    Xpost_Object finalize;
    int ret;


    /* load the graphicsdict, current graphics state, and current font */
    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    fontdict = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currfont"));
    if (xpost_object_get_type(fontdict) == invalidtype)
        return invalidfont;
    XPOST_LOG_INFO("loaded graphicsdict, graphics state, and current font");

    /* load the device and PutPix member function */
    devdic = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "device"));
    putpix = xpost_dict_get(ctx, devdic, xpost_name_cons(ctx, "PutPix"));
    XPOST_LOG_INFO("loaded DEVICE and PutPix");
    ts = _text_state_get(ctx, gs, fontdict, devdic);

    /* get the font data from the font dict */
    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == invalidtype)
        return invalidfont;
    if (!xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0, sizeof data,
                          &data)
     || data.face == NULL)
    {
        XPOST_LOG_ERR("face is NULL");
        return invalidfont;
    }
    _face_setup(ctx, gs, fontdict, data.face);
    XPOST_LOG_INFO("loaded font data from dict");

    /* get a c-style nul-terminated string */
    cstr = xpost_string_allocate_cstring(ctx, str);
    XPOST_LOG_INFO("append nul to string");

    ret = _get_current_point(ctx, gs, &xpos, &ypos);
    if (ret){
        free(cstr);
        return ret;
    }

    if (_device_color(ctx, gs, devdic, &ncomp, comp))
    {
        free(cstr);
        return unregistered;
    }
    XPOST_LOG_INFO("ncomp = %d", ncomp);

    ret = _show_finalize_cons(ctx, xpos, ypos, &finalize);
    if (ret)
    {
        free(cstr);
        return ret;
    }
    xpost_stack_push(ctx->lo, ctx->es, finalize);

    /* render text in char *cstr  with font data  at pen position xpos ypos */
    for (ch = cstr; *ch; ch++)
    {
        _show_char(ctx, devdic, putpix, data, &ts, &xpos, &ypos, (unsigned char)*ch,
                ncomp, comp[0], comp[1], comp[2], comp[3]);
        xpos += dx.real_.val;
        ypos += dy.real_.val;
        if ((unsigned char)*ch == charcode.int_.val)
        {
            xpos += cx.real_.val;
            ypos += cy.real_.val;
        }
    }

    /* update current position in the graphics state */
    ret = _show_finalize_pos(ctx, finalize, xpos, ypos);

    free(cstr);
    return ret;
}

static
int _stringwidth(Xpost_Context *ctx,
                 Xpost_Object str)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    Xpost_Object fontdict;
    Xpost_Object privatestr;
    struct fontdata data;
    char *cstr;
    real xpos = 0, ypos = 0;
    char *ch;
    Xpost_Object encoding;
    Xpost_Object charstrings;
    textstate mts;


    /* load the graphicsdict, current graphics state, and current font */
    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    fontdict = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currfont"));
    if (xpost_object_get_type(fontdict) == invalidtype)
        return invalidfont;
    XPOST_LOG_INFO("loaded graphicsdict, graphics state, and current font");

    /* get the font data from the font dict */
    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == invalidtype)
        return invalidfont;
    if (!xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0, sizeof data,
                          &data)
     || data.face == NULL)
    {
        XPOST_LOG_ERR("face is NULL");
        return invalidfont;
    }
    _face_setup(ctx, gs, fontdict, data.face);
    encoding = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Encoding"));
    charstrings = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "CharStrings"));
    /* only the /Metrics fields matter here: stringwidth accumulates the
       same per-glyph advances show would take */
    memset(&mts, 0, sizeof mts);
    mts.encoding = encoding;
    mts.metrics = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Metrics"));
    mts.cdmat_ok = xpost_object_get_type(mts.metrics) == dicttype
                && _char_device_matrix(ctx, gs, fontdict, mts.cdmat);
    XPOST_LOG_INFO("loaded font data from dict");

    /* get a c-style nul-terminated string */
    cstr = xpost_string_allocate_cstring(ctx, str);
    XPOST_LOG_INFO("append nul to string");

    /* accumulate the advances without rendering: the outline metrics
       carry the advance; a glyph with no outline (a bitmap strike)
       renders as a fallback */
    for (ch = cstr; *ch; ch++)
    {
#ifdef HAVE_FREETYPE2
        unsigned int glyph_index;
        long bx0, by0, bx1, by1;
        long advance_x;
        long advance_y;

        glyph_index = _glyph_index_for_char(ctx, encoding, charstrings,
                                            data.face, (unsigned char)*ch);
        if (!xpost_font_face_glyph_extents(data.face, glyph_index,
                                           &bx0, &by0, &bx1, &by1,
                                           &advance_x, &advance_y))
        {
            unsigned char *buffer;
            int rows, width, pitch, left, top;
            char pixel_mode;

            if (!xpost_font_face_glyph_render(data.face, glyph_index))
            {
                free(cstr);
                return unregistered;
            }
            xpost_font_face_glyph_buffer_get(data.face, &buffer, &rows, &width,
                                             &pitch, &pixel_mode, &left, &top,
                                             &advance_x, &advance_y);
        }
        /* a /Metrics entry for this glyph overrides the face's advance */
        _metrics_advance(ctx, &mts,
                         _encoded_name(ctx, encoding, (unsigned char)*ch),
                         &advance_x, &advance_y);
        xpos += (real)(advance_x / 65536.0);
        ypos += (real)(advance_y / 65536.0);
#endif

    }

    /* the advances accumulate in the face's y-up glyph space, sized and
       oriented through the CTM; stringwidth must report the distance in
       user space, so flip to the device's y-down convention and map back
       through the inverse of the CTM's linear part */
    ypos = -ypos;
    {
        Xpost_Object psmat = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currmatrix"));
        if (xpost_object_get_type(psmat) == arraytype && psmat.comp_.sz == 6)
        {
            real m[4], det;

            _matrix_linear_part(ctx, psmat, m);
            det = m[0] * m[3] - m[1] * m[2];
            if (det != 0)
            {
                real ux = (m[3] * xpos - m[2] * ypos) / det;
                real uy = (-m[1] * xpos + m[0] * ypos) / det;
                xpos = ux;
                ypos = uy;
            }
        }
    }

    xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons(xpos));
    xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons(ypos));

    free(cstr);
    return 0;
}

/* str  .stringoutline  array
   the string's glyph outlines as a flat array of path segments in the
   face's y-up glyph space (device-magnitude pixels, oriented by the
   face transform), relative to the pen start: coordinates followed by
   a tag, /m /l /c (cubic) or /h. charpath (in font.ps) maps each
   point to user space about the current point and appends it to the
   current path. Blank glyphs contribute advance only. */
typedef struct outlinecollect
{
    Xpost_Context *ctx;
    Xpost_Object *objs;
    size_t len, cap;
    double px, py;
    int err;
    Xpost_Object nm, nl, nc, nh;
} outlinecollect;

static
int _oc_push(outlinecollect *oc, Xpost_Object o)
{
    if (oc->len == oc->cap)
    {
        Xpost_Object *tmp;
        size_t ncap = oc->cap ? oc->cap * 2 : 256;

        tmp = realloc(oc->objs, ncap * sizeof *tmp);
        if (!tmp)
        {
            oc->err = VMerror;
            return 1;
        }
        oc->objs = tmp;
        oc->cap = ncap;
    }
    oc->objs[oc->len++] = o;
    return 0;
}

static
int _oc_xy(outlinecollect *oc, double x, double y)
{
    return _oc_push(oc, xpost_real_cons((real)(oc->px + x)))
        || _oc_push(oc, xpost_real_cons((real)(oc->py + y)));
}

static
int _oc_moveto(void *user, double x, double y)
{
    outlinecollect *oc = user;
    return _oc_xy(oc, x, y) || _oc_push(oc, oc->nm);
}

static
int _oc_lineto(void *user, double x, double y)
{
    outlinecollect *oc = user;
    return _oc_xy(oc, x, y) || _oc_push(oc, oc->nl);
}

static
int _oc_curveto(void *user, double x1, double y1, double x2, double y2, double x3, double y3)
{
    outlinecollect *oc = user;
    return _oc_xy(oc, x1, y1) || _oc_xy(oc, x2, y2) || _oc_xy(oc, x3, y3)
        || _oc_push(oc, oc->nc);
}

static
int _oc_closepath(void *user)
{
    outlinecollect *oc = user;
    return _oc_push(oc, oc->nh);
}

/* The collected segments as the array the outline operators answer
   with. The collector is given up either way. */
static
int _oc_array(Xpost_Context *ctx, outlinecollect *oc, Xpost_Object *arr)
{
    size_t i;

    if (oc->len > 65535)
    {
        free(oc->objs);
        return limitcheck;
    }
    *arr = xpost_object_cvlit(xpost_array_cons(ctx, (unsigned int)oc->len));
    if (xpost_object_get_type(*arr) == nulltype)
    {
        free(oc->objs);
        return VMerror;
    }
    for (i = 0; i < oc->len; i++)
    {
        int ret = xpost_array_put(ctx, *arr, (integer)i, oc->objs[i]);

        if (ret)
        {
            free(oc->objs);
            return ret;
        }
    }
    free(oc->objs);
    return 0;
}

static
int _stringoutline(Xpost_Context *ctx,
                   Xpost_Object str)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    Xpost_Object fontdict;
    Xpost_Object privatestr;
    struct fontdata data;
    Xpost_Object encoding;
    Xpost_Object charstrings;
    char *cstr;
    char *ch;
    outlinecollect oc;
    Xpost_Object arr;
    int ret;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    fontdict = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currfont"));
    if (xpost_object_get_type(fontdict) == invalidtype)
        return invalidfont;

    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == invalidtype)
        return invalidfont;
    if (!xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0, sizeof data,
                          &data)
     || data.face == NULL)
    {
        XPOST_LOG_ERR("face is NULL");
        return invalidfont;
    }
    _face_setup(ctx, gs, fontdict, data.face);
    encoding = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Encoding"));
    charstrings = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "CharStrings"));

    cstr = xpost_string_allocate_cstring(ctx, str);
    if (!cstr)
        return VMerror;

    memset(&oc, 0, sizeof oc);
    oc.ctx = ctx;
    oc.nm = xpost_object_cvlit(xpost_name_cons(ctx, "m"));
    oc.nl = xpost_object_cvlit(xpost_name_cons(ctx, "l"));
    oc.nc = xpost_object_cvlit(xpost_name_cons(ctx, "c"));
    oc.nh = xpost_object_cvlit(xpost_name_cons(ctx, "h"));

    for (ch = cstr; *ch; ch++)
    {
#ifdef HAVE_FREETYPE2
        unsigned int glyph_index;
        long advance_x, advance_y;
        Xpost_Font_Outline_Sink sink;

        glyph_index = _glyph_index_for_char(ctx, encoding, charstrings,
                                            data.face, (unsigned char)*ch);
        sink.moveto = _oc_moveto;
        sink.lineto = _oc_lineto;
        sink.curveto = _oc_curveto;
        sink.closepath = _oc_closepath;
        sink.user = &oc;
        if (!xpost_font_face_glyph_outline(data.face, glyph_index, &sink, &advance_x, &advance_y))
        {
            /* a glyph without an outline leaves no path; skip it */
            free(oc.objs);
            free(cstr);
            return invalidfont;
        }
        if (oc.err)
        {
            free(oc.objs);
            free(cstr);
            return oc.err;
        }
        oc.px += advance_x / 65536.0;
        oc.py += advance_y / 65536.0;
#endif
    }
    free(cstr);

    ret = _oc_array(ctx, &oc, &arr);
    if (ret)
        return ret;
    xpost_stack_push(ctx->lo, ctx->os, arr);
    return 0;
}

/* One glyph's outline, in the form .stringoutline gives a string's,
   and the advance it moves the pen by, in the same y-up glyph space
   the outline's points are in. The glyph is selected the way the
   caller selects one, by name or by index, and the advance comes with
   it either way because a string's comes from stringwidth, which has
   no form that names a glyph or numbers one. */
static
int _glyphoutline_common(Xpost_Context *ctx,
                         Xpost_Object gname,
                         int byname,
                         unsigned int gid)
{
    Xpost_Object userdict;
    Xpost_Object gd;
    Xpost_Object gs;
    Xpost_Object fontdict;
    Xpost_Object devdic;
    Xpost_Object privatestr;
    struct fontdata data;
    textstate ts;
    outlinecollect oc;
    Xpost_Object arr;
    long advance_x = 0, advance_y = 0;
    int ret;

    userdict = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 2);
    if (xpost_object_get_type(userdict) != dicttype)
        return dictstackunderflow;
    gd = xpost_dict_get(ctx, ctx->privatedict, xpost_name_cons(ctx, ".graphicsdict"));
    gs = xpost_dict_get(ctx, gd, xpost_name_cons(ctx, "currgstate"));
    fontdict = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "currfont"));
    if (xpost_object_get_type(fontdict) == invalidtype)
        return invalidfont;
    devdic = xpost_dict_get(ctx, gs, xpost_name_cons(ctx, "device"));
    ts = _text_state_get(ctx, gs, fontdict, devdic);

    privatestr = xpost_dict_get(ctx, fontdict, xpost_name_cons(ctx, "Private"));
    if (xpost_object_get_type(privatestr) == invalidtype)
        return invalidfont;
    if (!xpost_memory_get(xpost_context_select_memory(ctx, privatestr),
                          xpost_object_get_ent(privatestr), 0, sizeof data,
                          &data)
     || data.face == NULL)
    {
        XPOST_LOG_ERR("face is NULL");
        return invalidfont;
    }
    _face_setup(ctx, gs, fontdict, data.face);

    memset(&oc, 0, sizeof oc);
    oc.ctx = ctx;
    oc.nm = xpost_object_cvlit(xpost_name_cons(ctx, "m"));
    oc.nl = xpost_object_cvlit(xpost_name_cons(ctx, "l"));
    oc.nc = xpost_object_cvlit(xpost_name_cons(ctx, "c"));
    oc.nh = xpost_object_cvlit(xpost_name_cons(ctx, "h"));
#ifdef HAVE_FREETYPE2
    {
        Xpost_Font_Outline_Sink sink;
        unsigned int glyph_index;

        glyph_index = byname
            ? _glyph_index_for_name(ctx, ts.charstrings, data.face, gname)
            : gid;
        sink.moveto = _oc_moveto;
        sink.lineto = _oc_lineto;
        sink.curveto = _oc_curveto;
        sink.closepath = _oc_closepath;
        sink.user = &oc;
        if (!xpost_font_face_glyph_outline(data.face, glyph_index, &sink,
                                           &advance_x, &advance_y))
        {
            free(oc.objs);
            return invalidfont;
        }
        if (oc.err)
        {
            free(oc.objs);
            return oc.err;
        }
    }
#endif
    /* a /Metrics entry for this glyph overrides the face's advance,
       as it does on the raster route */
    _metrics_advance(ctx, &ts, byname ? gname : invalid, &advance_x, &advance_y);

    ret = _oc_array(ctx, &oc, &arr);
    if (ret)
        return ret;
    xpost_stack_push(ctx->lo, ctx->os, arr);
    xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons((real)(advance_x / 65536.0)));
    xpost_stack_push(ctx->lo, ctx->os, xpost_real_cons((real)(advance_y / 65536.0)));
    return 0;
}

/* name  .glyphoutline  array advx advy
   The named glyph's outline and advance. glyphshow selects a glyph by
   name rather than by code, so the outline is taken by name here as
   well. */
static
int _glyphoutline(Xpost_Context *ctx,
                  Xpost_Object gname)
{
    return _glyphoutline_common(ctx, gname, 1, 0);
}

/* index  .glyphoutlineidx  array advx advy
   The outline and advance of the glyph at the given index in the
   current font's face. The composite font machinery reaches glyphs by
   index once a CMap has resolved the character code, and reaches
   their outlines the same way. */
static
int _glyphoutlineidx(Xpost_Context *ctx,
                     Xpost_Object gidx)
{
    if (gidx.int_.val < 0)
        return rangecheck;
    return _glyphoutline_common(ctx, null, 0,
                                (unsigned int)gidx.int_.val);
}

/* -  .cachestatus  bsize bmax msize mmax csize cmax blimit
   the glyph cache's actual figures */
static
int _cachestatus(Xpost_Context *ctx)
{
    long v[7];

    xpost_font_cache_status(&v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
    {
        int i;

        for (i = 0; i < 7; i++)
            if (!xpost_stack_push(ctx->lo, ctx->os,
                                  xpost_int_cons((integer)v[i])))
                return stackoverflow;
    }
    return 0;
}

/* num  .setcachelimit  -
   the byte ceiling above which a glyph renders uncached */
static
int _setcachelimit(Xpost_Context *ctx, Xpost_Object n)
{
    (void)ctx;
    if (n.int_.val < 0)
        return rangecheck;
    xpost_font_cache_setlimit((long)n.int_.val);
    return 0;
}

/* size lower upper  .setcacheparams  -
   the cache's byte capacity and per-glyph ceiling; the middle
   operand, a compression threshold, is accepted and recorded nowhere
   since rasters stay flat */
static
int _setcacheparams(Xpost_Context *ctx,
                     Xpost_Object size,
                     Xpost_Object lower,
                     Xpost_Object upper)
{
    (void)ctx;
    xpost_font_cache_setparams((long)size.int_.val,
                               (long)lower.int_.val,
                               (long)upper.int_.val);
    return 0;
}

int xpost_oper_init_font_ops(Xpost_Context *ctx,
                             Xpost_Object sd)
{
    Xpost_Operator *optab;
    Xpost_Object n,op;

    assert(ctx->gl->base);

    op = xpost_operator_cons(ctx, "findfont", (Xpost_Op_Func)_findfont, 1, nametype);
    INSTALL;
    op = xpost_operator_cons(ctx, "findfont", (Xpost_Op_Func)_findfont, 1, stringtype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".loadfont42", (Xpost_Op_Func)_loadfont42, 1, dicttype);
    INSTALL;
    op = xpost_operator_cons(ctx, "setfont", (Xpost_Op_Func)_setfont, 1, dicttype);
    INSTALL;

    op = xpost_operator_cons(ctx, "show", (Xpost_Op_Func)_show, 1, stringtype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".glyphshow", (Xpost_Op_Func)_glyphshow, 1, nametype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".glyphshowidx", (Xpost_Op_Func)_glyphshowidx, 1, integertype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".loadcidfont0", (Xpost_Op_Func)_loadcidfont0, 1, dicttype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".stencilaa", (Xpost_Op_Func)_stencilaa, 1, dicttype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".loadfont1", (Xpost_Op_Func)_loadfont1, 2,
            dicttype, arraytype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".loadcidfont2", (Xpost_Op_Func)_loadcidfont2, 2,
            dicttype, arraytype);
    INSTALL;
    op = xpost_operator_cons(ctx, "ashow", (Xpost_Op_Func)_ashow, 3,
        floattype, floattype, stringtype);
    INSTALL;
    op = xpost_operator_cons(ctx, "widthshow", (Xpost_Op_Func)_widthshow, 4,
        floattype, floattype, integertype, stringtype);
    INSTALL;
    op = xpost_operator_cons(ctx, "awidthshow", (Xpost_Op_Func)_awidthshow, 6,
        floattype, floattype, integertype,
        floattype, floattype, stringtype);
    INSTALL;
    op = xpost_operator_cons(ctx, "stringwidth", (Xpost_Op_Func)_stringwidth, 1, stringtype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".stringoutline", (Xpost_Op_Func)_stringoutline, 1, stringtype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".glyphoutline", (Xpost_Op_Func)_glyphoutline, 1, nametype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".glyphoutlineidx", (Xpost_Op_Func)_glyphoutlineidx, 1, integertype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".cachestatus", (Xpost_Op_Func)_cachestatus, 0);
    INSTALL;
    op = xpost_operator_cons(ctx, ".maskcachehit", (Xpost_Op_Func)_maskcachehit, 5,
        floattype, floattype, arraytype, integertype, arraytype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".maskcacheput", (Xpost_Op_Func)_maskcacheput, 1, dicttype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".setcachelimit", (Xpost_Op_Func)_setcachelimit, 1, integertype);
    INSTALL;
    op = xpost_operator_cons(ctx, ".setcacheparams", (Xpost_Op_Func)_setcacheparams, 3,
        integertype, integertype, integertype);
    INSTALL;

    /* xpost_dict_dump_memory (ctx->gl, sd); fflush(NULL);
    xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "mark"), mark); */

    return 0;
}
