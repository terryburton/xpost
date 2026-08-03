/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 * (BSD 3-clause; see COPYING)
 */

#ifndef XPOST_DEV_DRIVER_H
#define XPOST_DEV_DRIVER_H

/*
 * The output-device driver contract.
 *
 * An output device is a PostScript dictionary: a class dictionary whose
 * Create method returns an instance dictionary, on which the graphics
 * pipeline looks methods up by name and executes them with the instance
 * as the topmost operand. The reference implementations are the
 * PostScript base classes in data/ppmimage.ps (DeviceRGB) and
 * data/pgmimage.ps (DeviceGray); data/nulldev.ps is the minimal
 * conforming device. A C device specializes a base class: it copies the
 * class dictionary (.copydict) and replaces method slots with operators.
 *
 * Method slots, with <colour> standing for one number per component of
 * the device's native colour space (see the colour rule below):
 *
 *     width height CLASS  Create    ->  IMAGE     (mandatory)
 *              <colour> x y IMAGE  PutPix    ->  -  (mandatory, raster)
 *                       x y IMAGE  GetPix    ->  <colour>  (optional)
 *      <colour> x1 y1 x2 y2 IMAGE  DrawLine  ->  -  (optional)
 *         <colour> x y w h IMAGE  DrawRect  ->  -  (optional, outline)
 *         <colour> x y w h IMAGE  FillRect  ->  -  (optional)
 *          <colour> polygon IMAGE  FillPoly  ->  -  (optional)
 *                           IMAGE  Emit      ->  -  (mandatory)
 *                           IMAGE  Flush     ->  -  (optional)
 *                           IMAGE  Destroy   ->  -  (mandatory)
 *                     dict1  .copydict  ->  dict2   (mandatory, class)
 *
 * Optional means the base class supplies a fallback built on PutPix, or
 * the pipeline probes the slot with `known` before calling it (Flush,
 * called by flushpage; FillRect and the probe paths in the device
 * contract test). A device that omits PutPix must bring its own
 * implementation of every marking method the pipeline can reach (the
 * vector devices do).
 *
 * Colour arity rule: the class's /nativecolorspace value determines the
 * component count of every <colour> operand: /DeviceRGB methods take
 * r g b, /DeviceGray methods take a single value. Components are unit
 * range (0..1) and every numeric operand may arrive as integertype or
 * realtype; a device folds them with the xpost_dev_num_* helpers below.
 *
 * FillRect extent semantics (the one definition, per the base class):
 * after folding the operands, a negative extent reflects the rectangle
 * through its origin (w < 0 means the rectangle spans x-|w|..x), and the
 * painted region is the inclusive pixel span from (x, y) to (x+w, y+h)
 * -- w+1 columns by h+1 rows -- clipped to the device bounds
 * [0, width-1] x [0, height-1]. A rectangle wholly outside the device
 * paints nothing. xpost_dev_rect_normalize() below is that definition;
 * FillRect implementations use it rather than restating it.
 *
 * Destroy must be idempotent: it is called at setpagedevice, at job end,
 * and possibly by the program itself, so it clears each resource handle
 * in the private struct and stores the struct back, making a repeated
 * Destroy a no-op rather than a double free.
 *
 * Instance state: C-level device state lives in a struct serialized into
 * a PostScript string stored under /Private in the instance dictionary.
 * The raw memory accessors record no save/restore backup, so the struct
 * is exempt from `restore` (raster memory is not part of VM, PLRM
 * 3.7.3). Devices load and store it with xpost_dev_private_get()/
 * xpost_dev_private_put() below.
 *
 * This header holds only static inline helpers; include it after the
 * standard device includes (xpost_object.h, xpost_memory.h,
 * xpost_context.h, xpost_stack.h, xpost_dict.h, xpost_string.h,
 * xpost_name.h and <string.h>).
 */

/* fold a numeric operand (integertype or realtype) to an int,
   truncating toward zero */
static inline int
xpost_dev_num_to_int(Xpost_Object obj)
{
    return (int)xpost_object_number(obj);
}

/* fold a unit-range colour component to the device's integer scale
   (255 for 8-bit channels, 65535 for 16-bit), truncating toward zero */
static inline int
xpost_dev_num_to_scaled(Xpost_Object obj, double scale)
{
    return (int)(xpost_object_number(obj) * scale);
}

/* fold a unit-range colour component to an 8-bit channel value */
static inline int
xpost_dev_num_to_byte(Xpost_Object obj)
{
    return xpost_dev_num_to_scaled(obj, 255.0);
}

/* Load the device's private C struct out of the string stored under
   key in the instance dictionary. Returns 1 on success and leaves the
   backing string in *privatestr for a later put; returns 0 when the
   instance carries no such string (the caller reports undefined). */
static inline int
xpost_dev_private_get(Xpost_Context *ctx,
                      Xpost_Object devdic,
                      Xpost_Object key,
                      Xpost_Object *privatestr,
                      void *priv,
                      size_t size)
{
    *privatestr = xpost_dict_get(ctx, devdic, key);
    if (xpost_object_get_type(*privatestr) != stringtype)
        return 0;
    xpost_memory_get(xpost_context_select_memory(ctx, *privatestr),
                     xpost_object_get_ent(*privatestr), 0,
                     size, priv);
    return 1;
}

/* store the private struct back into its backing string */
static inline void
xpost_dev_private_put(Xpost_Context *ctx,
                      Xpost_Object privatestr,
                      const void *priv,
                      size_t size)
{
    xpost_memory_put(xpost_context_select_memory(ctx, privatestr),
                     xpost_object_get_ent(privatestr), 0,
                     size, priv);
}

/* The rectangle FillRect paints, as inclusive device-clipped bounds:
   flips negative extents through the origin, forms the inclusive far
   corner (x+w, y+h) and clips to [0, width-1] x [0, height-1]. Returns
   1 with the span in *x0..*x1, *y0..*y1 (each end inclusive), or 0 when
   nothing survives the clip. */
static inline int
xpost_dev_rect_normalize(int x, int y, int w, int h,
                         int width, int height,
                         int *x0, int *y0, int *x1, int *y1)
{
    if (w < 0) { w = -w; x -= w; }
    if (h < 0) { h = -h; y -= h; }
    *x0 = x;
    *y0 = y;
    *x1 = x + w;
    *y1 = y + h;
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > width - 1) *x1 = width - 1;
    if (*y1 > height - 1) *y1 = height - 1;
    return (*x0 <= *x1) && (*y0 <= *y1);
}

/* Hand the rendered framebuffer to the embedding client: when the
   client registered an output-buffer hook (a pointer serialized into
   the /OutputBufferOut string in systemdict), store the buffer pointer
   through it. Returns 1 when the buffer was handed off -- ownership
   passes to the client and Destroy must leave the buffer alone -- and
   0 when no hook is registered. */
static inline int
xpost_dev_output_buffer_handoff(Xpost_Context *ctx,
                                unsigned char *data)
{
    Xpost_Object sd, outbufstr;

    sd = xpost_stack_bottomup_fetch(ctx->lo, ctx->ds, 0);
    outbufstr = xpost_dict_get(ctx, sd, xpost_name_cons(ctx, "OutputBufferOut"));
    if (xpost_object_get_type(outbufstr) == stringtype)
    {
        unsigned char **outbuf;

        memcpy(&outbuf, xpost_string_get_pointer(ctx, outbufstr), sizeof(outbuf));
        *outbuf = data;
        return 1;
    }
    return 0;
}

#endif
