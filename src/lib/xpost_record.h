/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 * (BSD 3-clause; see COPYING)
 */

#ifndef XPOST_RECORD_H
#define XPOST_RECORD_H

#include <stddef.h>

#include "xpost_object.h"   /* real: what a coordinate arrives as */

/**
 * @file xpost_record.h
 * @brief What a page was asked to paint, kept to be painted again.
 *
 * A record holds the marking calls a page received, in the order they
 * were made, and gives them back for any run of rows that is asked for.
 * It holds no pixels: what it costs follows the number of marks, not
 * the size of the page, which is what lets a page be painted in a
 * raster smaller than itself.
 *
 * The marking methods are five and their arities are fixed by the
 * driver contract, so a mark is a kind, one colour value per component
 * of the device's space, and that kind's own operands. A record is
 * therefore a flat run of those and not a tree.
 *
 * A sampled image is held whole, as one entry the run names, and is the
 * one place a record is higher-level than the calls a device receives.
 * It has to be: a device holding no rows is painted an image a
 * rectangle at a time, so the five alone would hold a picture at tens
 * of bytes a sample against the one to three bytes a pixel of the page
 * the record exists to avoid holding.
 *
 * Every mark says where it reaches in y when it is written down, and a
 * replay is given a row range, so a replay visits the marks that reach
 * into that range and steps over the rest. Playing a page of n marks
 * into b ranges then costs the marks each range meets rather than n
 * times b, which is the difference between this being useful on a large
 * page and being useless on one.
 *
 * The five are the whole of what a page can be asked to mark WHERE THE
 * DEVICE DOING THE RECORDING DECLARES NO FillPath. That condition is
 * what makes a record right about clipping, and it is not incidental:
 * .devtakespath (data/paint.ps) hands a whole path, and the clip shape
 * beside it, to any device that says it can fill a path for itself. A
 * device that says nothing gets the clip resolved above it instead, and
 * what reaches it is already cut to the region -- so a record of those
 * marks needs to hold no clip at all, and a replay cannot lose one.
 *
 * A recorder that later declared FillPath to save the cutting would
 * start receiving paths and clips it does not write down, and every
 * clipped page would replay wrong. Declining it is the design.
 *
 * A row range is the caller's to choose and nothing here assumes what
 * it is for. Successive strips of a page paint it in a raster the size
 * of a strip; the rows a window shows paint what someone is looking at;
 * the whole page paints the whole page. A record can be replayed as
 * many times as the caller likes, which is what lets a page already
 * drawn be shown again without running the program that drew it.
 */

/** The marking calls a record holds, and their operand counts after
    the colour. The order and arities follow xpost_dev_driver.h.

    A polygon's pairs are its vertices with its subpath separators among
    them, a separator being the pair a subpath break is written as in
    the packed path this tree already keeps a polygon in (XPOST_PATH_BREAK
    in both coordinates, xpost_op_path.h). The separators are part of the
    shape and not decoration: the interior of a path with a hole is
    settled by scanning its subpaths together, so a polygon written down
    without them replays as a different region.

    A sampled image is the sixth and is not one of the marking calls: a
    device without rows of its own is painted an image one rectangle per
    sample, so a record built from the five alone would hold a thousand
    by thousand image as a million marks of tens of bytes each against
    the one to three bytes a pixel the page it is escaping costs. It
    carries an index into the images the record holds instead of
    operands of its own. */
typedef enum
{
    XPOST_RECORD_PUTPIX,   /**< x y */
    XPOST_RECORD_BLENDPIX, /**< cov x y */
    XPOST_RECORD_DRAWLINE, /**< x1 y1 x2 y2 */
    XPOST_RECORD_FILLRECT, /**< x y w h */
    XPOST_RECORD_FILLPOLY, /**< n, then n pairs of x y */
    XPOST_RECORD_IMAGE     /**< which of the record's images */
} Xpost_Record_Kind;

typedef struct _Xpost_Record Xpost_Record;

/**
 * @brief A record for a device whose colour takes @p ncomp values.
 * @return the record, or NULL where there is no memory for one
 */
Xpost_Record *xpost_record_new(int ncomp);

/**
 * @brief Give up a record and everything it holds.
 */
void xpost_record_free(Xpost_Record *rec);

/**
 * @brief Write one mark down.
 *
 * @param[in] rec the record
 * @param[in] kind which marking call
 * @param[in] colour ncomp values, as the call received them
 * @param[in] ops the call's own operands, in the order above
 * @param[in] nops how many, which the kind settles except for a polygon
 * @return 1, or 0 where there is no memory to hold it
 *
 * A record that could not hold a mark is short of one, and a page
 * played back from it would be missing what it could not hold. So the
 * failure sticks: the record remembers it, every later mark is refused
 * as well, and every replay refuses. A caller that ignores this return
 * therefore cannot go on to emit a page that is quietly wrong -- it
 * gets nothing rather than something short.
 *
 * The values are kept in the type a coordinate arrives in rather than
 * a wider one: a record exists to be smaller than the page it draws,
 * and widening every value would halve how much page a record buys.
 *
 * The operands are kept as they arrived. Rounding a coordinate to a
 * pixel is the painting device's business and is done when the mark is
 * played, so that a record made once can be played into rasters that
 * differ in where their rows begin.
 */
int xpost_record_mark(Xpost_Record *rec, Xpost_Record_Kind kind,
                      const real *colour, const real *ops, int nops);

/**
 * @brief A sampled image, where it is put, and what decodes it.
 *
 * The samples are the normalized rows the image collectors produce --
 * one byte per component per sample, whatever depth the program's data
 * source had -- and everything beside them is the result of the colour
 * setup the painter bakes before it writes a row, not the state that
 * setup was derived from. A replay happens when the page is put out,
 * by which time the graphics state that decoded the image is gone:
 * transfer functions, the current colour space and the space's
 * conversion have all moved on, and there is no re-deriving them. What
 * is kept is what a row write reads, which is these tables.
 *
 *   samples   the one block the record made of the rows it was handed:
 *             height runs of width x ncomp bytes, or, where the rows
 *             are planar, height x ncomp runs of width bytes, a row's
 *             planes together. It is the record's and is filled in by
 *             the record; what a caller hands over is the rows.
 *   lut       one-component spaces bake decode, conversion and transfer
 *             into 256 entries of nat bytes, and nothing else is read.
 *   dluts     otherwise, ncomp runs of 256 decode entries, converted at
 *             the write and passed through tlut (and, where the device
 *             takes three, through the three channel transfers) after.
 *   mbits     one bit per sample in rows of mrowb bytes, a set bit
 *             leaving the pixel alone; mranges pairs of raw sample
 *             values, a pixel inside every one of them left alone.
 *   cspans    quads of x0 y0 x1 y1 in device space: the region resolved
 *             above the device where it was not a rectangle.
 *
 * A pointer that is not given is NULL and the thing it names is not
 * read. Nothing here is held by reference: what a caller hands over is
 * copied, since a record outlives the job that made it.
 */
typedef struct
{
    int width, height;   /**< the sample grid the rows hold */
    int ncomp;           /**< components a sample carries */
    int nat;             /**< values a device pixel takes: 1 or 3 */
    int planar;          /**< rows hold planes rather than pixels */
    int rgbrows;         /**< the device row is three colour planes */
    int cmyk;            /**< four components convert by complement */
    int interp;          /**< blend between samples where magnified */
    real xoff, xscale;   /**< where a sample column lands, and how wide */
    real yoff, yscale;   /**< where a sample row lands, and how tall */
    real cx0, cy0;       /**< the region the rows are written through */
    real cx1, cy1;
    const unsigned char *samples;
    const unsigned char *lut;      /**< 256 entries of nat bytes */
    const unsigned char *dluts;    /**< ncomp runs of 256 */
    const unsigned char *tlut;     /**< 256 */
    const unsigned char *tlutrgb;  /**< three runs of 256 */
    const unsigned char *mbits;    /**< height runs of mrowb bytes */
    int mrowb;
    const int *mranges;            /**< nranges raw values */
    int nranges;
    const real *cspans;            /**< nspan quads */
    int nspan;
} Xpost_Record_Image;

/**
 * @brief Write one sampled image down, and a mark naming it.
 *
 * @param[in] img everything about the image but its samples; its
 *                @c samples field is the record's to fill and is not
 *                read here
 * @param[in] rows the sample rows, @p nrows runs of width bytes where
 *                 the rows are planar and width x ncomp bytes where
 *                 they are not, a row's planes adjacent
 * @param[in] nrows height, or height x ncomp where the rows are planar
 * @return 1, or 0 where there is no memory to hold it, on the same
 *         terms as a mark: the record is then short of a mark and every
 *         replay of it refuses.
 *
 * Everything handed here is copied. A record outlives the job that made
 * it -- pages either side of the one on screen are held so that moving
 * back is as cheap as moving forward -- so it may hold nothing
 * belonging to the run, and the rows in particular are the painter's
 * own scratch buffers, refilled for the row after.
 *
 * What that costs is the image, at one byte per component per sample:
 * bounded by the picture the job is holding anyway rather than by the
 * page, which is the whole reason an image is one entry here.
 */
int xpost_record_image(Xpost_Record *rec, const Xpost_Record_Image *img,
                       const unsigned char *const *rows, int nrows);

/**
 * @brief How many images a record holds.
 */
size_t xpost_record_image_count(const Xpost_Record *rec);

/**
 * @brief The image at @p i, as it was written down, or NULL.
 *
 * What comes back points into the record and is good until the next
 * image is written down.
 */
const Xpost_Record_Image *xpost_record_image_get(const Xpost_Record *rec,
                                                 size_t i);

/**
 * @brief Which of an image's rows reach device rows @p lo to @p hi.
 *
 * @param[out] y0 the first sample row to write, @p y1 one past the last
 * @return 1 where some row reaches the range, 0 where none does
 *
 * An image is clipped to a run of rows the way a shape is: by choosing
 * what to paint rather than by trimming what was recorded. Which rows
 * to choose is what the placing transform decides, and this is that
 * question asked of it. The answer errs outward -- a row written that
 * the region then rejects costs a pass over it, a row not written is
 * missing from the page.
 */
int xpost_record_image_rows(const Xpost_Record_Image *img,
                            real lo, real hi, int *y0, int *y1);

/**
 * @brief How many marks a record holds.
 */
size_t xpost_record_count(const Xpost_Record *rec);

/**
 * @brief What a record costs, in bytes.
 *
 * The marks, their values, and the images they name. It is the quantity
 * the whole mechanism is judged on: a record is worth holding while it
 * is smaller than the raster it saves holding, and that is a comparison
 * rather than a guess.
 */
size_t xpost_record_bytes(const Xpost_Record *rec);

/**
 * @brief Whether a mark was ever refused for want of memory.
 *
 * @return 1 where the record is short of a mark it was given, 0 where
 *         it holds everything it was given
 *
 * A record answering 1 describes a page it cannot reproduce, and every
 * replay of it refuses. The answer is asked for by whoever is about to
 * emit, so that a page is refused where it cannot be painted whole.
 */
int xpost_record_failed(const Xpost_Record *rec);

/**
 * @brief The rows a record's marks reach, or zero where it holds none.
 *
 * @param[out] lo the first row any mark reaches
 * @param[out] hi the last
 * @return 1 where the record holds a mark, 0 where it holds none
 */
int xpost_record_extent(const Xpost_Record *rec, real *lo, real *hi);

/**
 * @brief The last mark reaching rows @p lo to @p hi.
 *
 * @param[out] at where it was found
 * @return 1 where some mark reaches those rows, 0 where none does
 *
 * The mark that had the last word over the run, which is what a caller
 * asking what those rows come to has to start from: everything painted
 * before it is painted over wherever it covers, and nothing is painted
 * after it at all. It answers by the rule a replay plays by, so the
 * mark it names is one the replay would play.
 *
 * What a caller does with that is its own. A band loop asks it whether
 * a band comes to nothing but the colour the page was cleared to, since
 * such a band need not be painted: the ground is what a device holding
 * no pixel over a row answers and what an emitted page carries there,
 * so leaving those rows alone puts out the page painting them would
 * have put out. Whether a mark leaves a run like that is a question
 * about colour and about the page's width, which is the asking device's
 * to settle and not this record's.
 */
int xpost_record_last(const Xpost_Record *rec, real lo, real hi, size_t *at);

/**
 * @brief The mark at @p i, as it was written down.
 *
 * @param[out] kind which marking call
 * @param[out] colour the ncomp values it was made with
 * @param[out] ops its own operands, or NULL where the kind has none
 * @param[out] nops how many
 * @return 1, or 0 where the record holds no mark there
 *
 * An image's one operand is which of the record's images it names, and
 * its colour values are zero: what colours it is in its samples.
 *
 * What comes back points into the record and is good until the next
 * mark is written down. A record short of a mark it was given gives
 * none of them back, on the same terms as a replay of one: what would
 * be built from what is left is a page missing something, and a page
 * missing a mark looks like a page.
 *
 * A replay that plays a mark into a device returns to the interpreter
 * to do it -- a device method may be a procedure, and what runs a
 * procedure is the interpreter -- so it cannot be handed the run of
 * marks in one call. It asks for them one at a time instead, and what
 * it keeps between marks is how far it has got.
 */
int xpost_record_get(const Xpost_Record *rec, size_t i,
                     Xpost_Record_Kind *kind, const real **colour,
                     const real **ops, int *nops);

/**
 * @brief The first mark from @p from on that reaches rows @p lo to @p hi.
 *
 * @param[in] rec the record
 * @param[in] from the mark to start looking at
 * @param[out] at where one was found
 * @return 1 where there is one, 0 where no mark from there on reaches
 *         those rows
 *
 * The walk a replay makes when it cannot be a loop. A device method may
 * be a procedure and what runs a procedure is the interpreter, so such a
 * replay returns between marks and is resumed rather than continued;
 * what it keeps is how far it has got, and this is how it gets on. It
 * answers by the rule xpost_record_replay plays by, so the two visit the
 * same marks for the same rows.
 */
int xpost_record_next(const Xpost_Record *rec, size_t from, real lo, real hi,
                      size_t *at);

/**
 * @brief What a replay does with each mark it is given.
 *
 * Called once per mark that reaches the rows asked for, in the order
 * the marks were made -- which is the order they were painted in, and
 * so the order they must be painted in again. Returns 0 to go on, or
 * the error to raise, which the replay returns unchanged without
 * playing anything further.
 */
typedef int (*Xpost_Record_Player)(void *data, Xpost_Record_Kind kind,
                                   const real *colour,
                                   const real *ops, int nops);

/**
 * @brief Play back the marks that reach rows @p lo to @p hi inclusive.
 *
 * @return 0, what the player returned when it stopped, or VMerror
 *         where the record is short of a mark it was given and so
 *         describes a page it cannot reproduce
 *
 * A mark that reaches the range at all is played whole. A shape has to
 * be converted whole to be right about any part of it, so the range
 * chooses which marks are played and never trims one.
 */
int xpost_record_replay(const Xpost_Record *rec, real lo, real hi,
                        Xpost_Record_Player player, void *data);

#endif
