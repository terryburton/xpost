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
    the colour. The order and arities follow xpost_dev_driver.h. */
typedef enum
{
    XPOST_RECORD_PUTPIX,   /**< x y */
    XPOST_RECORD_BLENDPIX, /**< cov x y */
    XPOST_RECORD_DRAWLINE, /**< x1 y1 x2 y2 */
    XPOST_RECORD_FILLRECT, /**< x y w h */
    XPOST_RECORD_FILLPOLY  /**< n, then n pairs of x y */
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
 * @brief How many marks a record holds.
 */
size_t xpost_record_count(const Xpost_Record *rec);

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
