/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 * (BSD 3-clause; see COPYING)
 */

#ifndef XPOST_DEV_DRIVER_H
#define XPOST_DEV_DRIVER_H

#include <math.h> /* the device-space geometry below rounds by floor */

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
 * Marking geometry: a marking method paints one defined set of pixels
 * for given operands, and that set is stated once, below, rather than
 * by each implementation. A device coordinate names the pixel that
 * contains it (floor). xpost_dev_rect_normalize() with the span
 * clippers is the rectangle FillRect paints -- a negative extent
 * reflects through the origin, and the span is inclusive of the far
 * corner, so an integral w gives w+1 columns. Xpost_Dev_Line is the
 * line DrawLine paints -- the pixels whose centres the segment covers
 * along its major axis, so a run from a to b covers a..b-1 whichever
 * end it is drawn from. The compiled base-class fills reach both
 * through this header, and the PostScript
 * base class reaches them through .rectspan and .linepix, so there is
 * no second statement to drift from.
 *
 * A vector device has no pixels, so it converts: the operands describe
 * an inclusive pixel span and a vector rectangle is half-open, so what
 * it emits is x, y, w+1, h+1 -- the rectangle covering the same pixels
 * a raster device would paint.
 *
 * Destroy must be idempotent: it is called at setpagedevice, at job end,
 * and possibly by the program itself, so it clears each resource handle
 * in the private struct and stores the struct back, making a repeated
 * Destroy a no-op rather than a double free.
 *
 * Destroy releases the buffer but not the instance dictionary, so a
 * destroyed instance stays reachable and its slots stay callable. A
 * program reaches one by calling Destroy itself, and the interpreter
 * reaches one without being asked: setpagedevice retires the outgoing
 * device, and PLRM 6.1 makes the device an element of the graphics state
 * rather than a global fixture, so a saved graphics state still names the
 * retired one and a restore or grestore back past the change makes it
 * current again.
 *
 * Every slot therefore tests the handle it is about to follow instead of
 * assuming Create left one. The recorded width and height are no stand-in
 * for that test: they live in the private struct and outlive the buffer,
 * so an in-range pixel on a released instance passes the bounds check and
 * arrives at the read. A released raster reads as the ground, the same
 * answer a pixel outside the raster gets; it takes no marks; and it emits
 * nothing, its output having been finalised when it was released.
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
 * xpost_context.h, xpost_stack.h, xpost_error.h, xpost_dict.h,
 * xpost_string.h, xpost_name.h, xpost_operator.h and <string.h>).
 */

/* fold a numeric operand (integertype or realtype) to an int,
   truncating toward zero */
static inline int
xpost_dev_num_to_int(Xpost_Object obj)
{
    return (int)xpost_object_number(obj);
}

/* A colour component as a number in [0,1]. The component is clamped
   here, once, because the colour pipeline can hand a device one outside
   the range: setgray and its siblings substitute the nearest valid
   value (PLRM 8.2), but a Separation or DeviceN tint transform is the
   program's own procedure and its result is whatever it computes.
   Unclamped, the scale below wraps the stored channel -- 1.7 lands as
   0.69 of full scale, and on a packed pixel the overflow shifts across
   into the neighbouring component -- so the ink comes out a different
   colour rather than the nearest one. */
static inline double
xpost_dev_num_to_component(Xpost_Object obj)
{
    double d = xpost_object_number(obj);

    if (d < 0.0) return 0.0;
    if (d > 1.0) return 1.0;
    return d;
}

/* fold a unit-range colour component to the device's integer scale
   (255 for 8-bit channels, 65535 for 16-bit), truncating toward zero */
static inline int
xpost_dev_num_to_scaled(Xpost_Object obj, double scale)
{
    return (int)(xpost_dev_num_to_component(obj) * scale);
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
   instance carries no such string, or one too small to hold the struct
   (the caller reports undefined). A short string is reachable: the
   instance dictionary is an ordinary dictionary, and what it holds under
   this key is whatever was last stored there. */
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
    return xpost_memory_get(xpost_context_select_memory(ctx, *privatestr),
                            xpost_object_get_ent(*privatestr), 0,
                            size, priv);
}

/* Store the private struct back into its backing string. Returns 1 on
   success, 0 when the string will not hold it -- the same reachable
   case xpost_dev_private_get answers, seen from the other side, and the
   device state the caller was about to record is then lost. */
static inline XPOST_MUST_CHECK int
xpost_dev_private_put(Xpost_Context *ctx,
                      Xpost_Object privatestr,
                      const void *priv,
                      size_t size)
{
    return xpost_memory_put(xpost_context_select_memory(ctx, privatestr),
                            xpost_object_get_ent(privatestr), 0,
                            size, priv);
}

/*
 * Device-space geometry.
 *
 * A marking method's operands are positions in device space, which may
 * arrive as reals. The pixel a real coordinate names is the pixel
 * containing it -- floor, not truncation toward zero. The two agree
 * everywhere except across the origin, where truncation maps both -0.5
 * and 0.5 onto pixel 0 and never onto pixel -1, so a shape crossing the
 * origin gains a doubled column. Floor is also what the PostScript
 * classes' .to-int has always done, so the compiled and interpreted
 * methods land on the same pixels.
 */
static inline int
xpost_dev_pixel(double v)
{
    return (int)floor(v);
}

/* The rectangle FillRect paints, as an inclusive pixel span: a negative
   extent reflects the rectangle through its origin (w < 0 means the
   rectangle spans x-|w|..x), and the span runs from the pixel holding
   (x, y) to the pixel holding (x+w, y+h) -- so an integral w gives w+1
   columns. Unclipped: the clip source differs between devices (a fixed
   framebuffer, or a row array whose rows carry their own lengths), so
   the caller states it through the span clippers below. */
static inline void
xpost_dev_rect_normalize(double x, double y, double w, double h,
                         int *x0, int *y0, int *x1, int *y1)
{
    if (w < 0) { w = -w; x -= w; }
    if (h < 0) { h = -h; y -= h; }
    *x0 = xpost_dev_pixel(x);
    *y0 = xpost_dev_pixel(y);
    *x1 = xpost_dev_pixel(x + w);
    *y1 = xpost_dev_pixel(y + h);
}

/* Clip an inclusive span to [0, extent-1]. Returns 1 when something
   survives, 0 when the span lies wholly outside. */
static inline int
xpost_dev_span_clip(int *lo, int *hi, int extent)
{
    if (*lo < 0) *lo = 0;
    if (*hi > extent - 1) *hi = extent - 1;
    return *lo <= *hi;
}

/* Clip an inclusive rectangle to a device of the given size: the clip
   source for every device holding one rectangular framebuffer. */
static inline int
xpost_dev_rect_clip(int *x0, int *y0, int *x1, int *y1,
                    int width, int height)
{
    return xpost_dev_span_clip(x0, x1, width)
         & xpost_dev_span_clip(y0, y1, height);
}

/*
 * The pixels DrawLine paints, walked one at a time.
 *
 * DrawLine paints the pixels whose centres the segment covers along its
 * major axis: for each integer coordinate c on that axis between the
 * two endpoints' pixels, the segment is sampled where it crosses
 * c + 0.5, and the pixel it passes through there is painted when that
 * crossing lies within the segment.
 *
 * Two consequences worth stating, because implementations that walk
 * endpoints rather than centres have neither. The set does not depend
 * on which end the segment is drawn from -- reversing the operands
 * reverses the order and nothing else. And two collinear segments
 * meeting at a shared endpoint continue each other exactly, painting it
 * once, wherever within a pixel that endpoint falls; a horizontal run
 * from a to b with a < b integral therefore covers a..b-1, which is why
 * the scanline filler can hand a fill span to either DrawLine or
 * FillRect and get the same row.
 *
 * A segment too short to reach any centre still marks the pixel holding
 * its midpoint, so nothing a program draws vanishes.
 *
 * Endpoints are first quantised to the 1/256 device grid the fill
 * pipeline works on: an endpoint meant to sit on a pixel boundary
 * arrives carrying accumulated float noise, and the walk floors it.
 *
 * The walk does not clip. Which pixels exist is the device's business,
 * and every caller already rejects a pixel outside its raster; clipping
 * the segment first would give the same set anyway, since the sample
 * positions do not move.
 *
 *     Xpost_Dev_Line l;
 *     int px, py;
 *     xpost_dev_line_init(&l, x1, y1, x2, y2);
 *     while (xpost_dev_line_next(&l, &px, &py))
 *         plot(px, py);
 */
typedef struct
{
    double a1, b1, da, db;   /* major axis a, minor axis b */
    int major_is_x;
    int c, cend, step;
    int painted;
    int degenerate;          /* no extent at all: one pixel, then done */
    int done;
} Xpost_Dev_Line;

static inline double
xpost_dev_line_quantize(double v)
{
    return floor(v * 256.0 + 0.5) / 256.0;
}

static inline void
xpost_dev_line_init(Xpost_Dev_Line *l,
                    double x1, double y1, double x2, double y2)
{
    double dx, dy;

    x1 = xpost_dev_line_quantize(x1);
    y1 = xpost_dev_line_quantize(y1);
    x2 = xpost_dev_line_quantize(x2);
    y2 = xpost_dev_line_quantize(y2);
    dx = x2 - x1;
    dy = y2 - y1;

    l->painted = 0;
    l->done = 0;
    l->degenerate = (dx == 0.0 && dy == 0.0);
    if (l->degenerate)
    {
        /* one pixel, the one holding the point */
        l->major_is_x = 1;
        l->a1 = x1; l->b1 = y1; l->da = 0.0; l->db = 0.0;
        l->c = xpost_dev_pixel(x1);
        l->cend = l->c;
        l->step = 1;
        return;
    }

    l->major_is_x = (fabs(dx) >= fabs(dy));
    if (l->major_is_x)
    {
        l->a1 = x1; l->da = dx;
        l->b1 = y1; l->db = dy;
    }
    else
    {
        l->a1 = y1; l->da = dy;
        l->b1 = x1; l->db = dx;
    }
    l->c = xpost_dev_pixel(l->a1);
    l->cend = xpost_dev_pixel(l->a1 + l->da);
    l->step = (l->da < 0.0) ? -1 : 1;
}

/* The next pixel, or 0 when the segment is walked out. */
static inline int
xpost_dev_line_next(Xpost_Dev_Line *l, int *px, int *py)
{
    if (l->done)
        return 0;

    if (l->degenerate)
    {
        l->done = 1;
        *px = xpost_dev_pixel(l->a1);
        *py = xpost_dev_pixel(l->b1);
        return 1;
    }

    while ((l->step > 0) ? (l->c <= l->cend) : (l->c >= l->cend))
    {
        int c = l->c;
        double t = (c + 0.5 - l->a1) / l->da;

        l->c += l->step;
        if (t >= 0.0 && t <= 1.0)
        {
            double b = l->b1 + l->db * t;

            l->painted = 1;
            if (l->major_is_x) { *px = c; *py = xpost_dev_pixel(b); }
            else               { *px = xpost_dev_pixel(b); *py = c; }
            return 1;
        }
    }

    l->done = 1;
    if (!l->painted)
    {
        /* too short to reach a centre: the midpoint's pixel */
        double a = l->a1 + l->da / 2.0;
        double b = l->b1 + l->db / 2.0;

        l->painted = 1;
        if (l->major_is_x) { *px = xpost_dev_pixel(a); *py = xpost_dev_pixel(b); }
        else               { *px = xpost_dev_pixel(b); *py = xpost_dev_pixel(a); }
        return 1;
    }
    return 0;
}

/*
 * A device's method suite, as data.
 *
 * Every C device installs the same shapes into its class dictionary,
 * and every one of them used to write out each installation by hand:
 * the operator name, the function, the result count, the operand count
 * and one type per operand, then a put whose refusal it had to remember
 * to check. Five of six answered success from a failed PutPix
 * registration, so a device loaded with no PutPix and failed at its
 * first paint.
 *
 * A method's arity is not a free choice: it follows from what the slot
 * is and from the device's declared colour space, since <colour> stands
 * for one operand per component. So the table states the slot, the
 * function and the kind, and the arity is derived. A device cannot
 * declare an arity that disagrees with its colour space, because it
 * does not declare one.
 *
 * xpost_dev_class_install() registers a table and then checks what it
 * produced: every mandatory slot filled, and -- for a device whose
 * raster is a buffer of its own rather than the base class's row array
 * -- every slot that would reach for that row array overridden. A
 * device that fails either does not load, rather than loading with a
 * method that answers undefined the first time the pipeline reaches it.
 */
typedef enum
{
    XPOST_DEV_M_CREATE,   /*        width height CLASS  ->  IMAGE   */
    XPOST_DEV_M_PUTPIX,   /*      <colour> x y IMAGE  ->  -         */
    XPOST_DEV_M_GETPIX,   /*               x y IMAGE  ->  <colour>  */
    XPOST_DEV_M_LINE,     /* <colour> x1 y1 x2 y2 IMAGE  ->  -      */
    XPOST_DEV_M_RECT,     /*    <colour> x y w h IMAGE  ->  -       */
    XPOST_DEV_M_BLEND,    /*  <colour> cov x y IMAGE  ->  -         */
    XPOST_DEV_M_POLY,     /*     <colour> polygon IMAGE  ->  -      */
    XPOST_DEV_M_PAGE      /*                     IMAGE  ->  -       */
} Xpost_Dev_Method_Kind;

typedef struct
{
    const char *slot;    /* the name the pipeline looks up */
    const char *opname;  /* the operator's own name, for the register */
    Xpost_Op_Func func;
    Xpost_Dev_Method_Kind kind;
} Xpost_Dev_Method;

#define XPOST_DEV_METHOD_COUNT(t) ((int)(sizeof(t) / sizeof(*(t))))

/* The slots whose base-class body reads the raster held as PostScript
   row arrays. A device that brings its own buffer must override every
   one of them: what it inherits instead reads a name its instance does
   not carry and answers undefined -- present, callable and broken,
   which no probe with `known` can tell from working. GetPix was
   inherited unoverridden by five devices for exactly that reason.
   tests/check-device-skeleton.sh holds this list to the classes. */
#define XPOST_DEV_RASTER_SLOTS { "Create", "PutPix", "GetPix", "Emit" }

/* Slots no device may be without, whatever it keeps its raster in. */
#define XPOST_DEV_MANDATORY_SLOTS { "Create", "Emit", "Destroy", ".copydict" }

/* Build a method's operator with the arity its kind and the device's
   colour space give it. Returns an invalidtype object if the shape is
   not one this contract knows. */
static inline Xpost_Object
xpost_dev_method_cons(Xpost_Context *ctx,
                      const Xpost_Dev_Method *m,
                      int ncomp)
{
    /* numeric operands before the device dictionary, and results */
    int n = 0, out = 0, poly = 0;

    switch (m->kind)
    {
        case XPOST_DEV_M_CREATE:
            return xpost_operator_cons(ctx, m->opname, m->func, 1, 3,
                                       integertype, integertype, dicttype);
        case XPOST_DEV_M_PUTPIX: n = ncomp + 2; break;
        case XPOST_DEV_M_GETPIX: n = 2; out = ncomp; break;
        case XPOST_DEV_M_LINE:   n = ncomp + 4; break;
        case XPOST_DEV_M_RECT:   n = ncomp + 4; break;
        case XPOST_DEV_M_BLEND:  n = ncomp + 3; break;
        case XPOST_DEV_M_POLY:   n = ncomp; poly = 1; break;
        case XPOST_DEV_M_PAGE:   n = 0; break;
    }

    if (poly)
    {
        switch (n)
        {
            case 1: return xpost_operator_cons(ctx, m->opname, m->func, out, 3,
                        numbertype, arraytype, dicttype);
            case 3: return xpost_operator_cons(ctx, m->opname, m->func, out, 5,
                        numbertype, numbertype, numbertype, arraytype, dicttype);
            case 4: return xpost_operator_cons(ctx, m->opname, m->func, out, 6,
                        numbertype, numbertype, numbertype, numbertype,
                        arraytype, dicttype);
        }
        return invalid;
    }

    switch (n)
    {
        case 0: return xpost_operator_cons(ctx, m->opname, m->func, out, 1,
                    dicttype);
        case 2: return xpost_operator_cons(ctx, m->opname, m->func, out, 3,
                    numbertype, numbertype, dicttype);
        case 3: return xpost_operator_cons(ctx, m->opname, m->func, out, 4,
                    numbertype, numbertype, numbertype, dicttype);
        case 4: return xpost_operator_cons(ctx, m->opname, m->func, out, 5,
                    numbertype, numbertype, numbertype, numbertype, dicttype);
        case 5: return xpost_operator_cons(ctx, m->opname, m->func, out, 6,
                    numbertype, numbertype, numbertype, numbertype,
                    numbertype, dicttype);
        case 6: return xpost_operator_cons(ctx, m->opname, m->func, out, 7,
                    numbertype, numbertype, numbertype, numbertype,
                    numbertype, numbertype, dicttype);
        case 7: return xpost_operator_cons(ctx, m->opname, m->func, out, 8,
                    numbertype, numbertype, numbertype, numbertype,
                    numbertype, numbertype, numbertype, dicttype);
        case 8: return xpost_operator_cons(ctx, m->opname, m->func, out, 9,
                    numbertype, numbertype, numbertype, numbertype,
                    numbertype, numbertype, numbertype, numbertype, dicttype);
    }
    return invalid;
}

/* Register a method table into a class dictionary and check the result.
   ncomp is the component count of the device's declared colour space.
   raster_is_compiled says the device keeps its pixels in a buffer of
   its own, so the base class's row-array methods cannot serve it.
   Returns 0, or the error that stopped it -- on the first failure, so a
   device that could not be completed does not load. */
static inline XPOST_MUST_CHECK int
xpost_dev_class_install(Xpost_Context *ctx,
                        Xpost_Object classdic,
                        int ncomp,
                        int raster_is_compiled,
                        const Xpost_Dev_Method *methods,
                        int nmethods)
{
    static const char *mandatory[] = XPOST_DEV_MANDATORY_SLOTS;
    static const char *raster[] = XPOST_DEV_RASTER_SLOTS;
    int i;

    for (i = 0; i < nmethods; i++)
    {
        Xpost_Object op = xpost_dev_method_cons(ctx, &methods[i], ncomp);
        int ret;

        if (xpost_object_get_type(op) == invalidtype)
        {
            XPOST_LOG_ERR("device method %s has no shape in the driver contract",
                          methods[i].slot);
            return unregistered;
        }
        ret = xpost_dict_put(ctx, classdic,
                             xpost_name_cons(ctx, methods[i].slot), op);
        if (ret)
            return ret;
    }

    for (i = 0; i < (int)(sizeof(mandatory) / sizeof(*mandatory)); i++)
    {
        Xpost_Object v = xpost_dict_get(ctx, classdic,
                                        xpost_name_cons(ctx, mandatory[i]));

        if (xpost_object_get_type(v) == invalidtype ||
            xpost_object_get_type(v) == nulltype)
        {
            XPOST_LOG_ERR("device class has no %s", mandatory[i]);
            return unregistered;
        }
    }

    if (raster_is_compiled)
        for (i = 0; i < (int)(sizeof(raster) / sizeof(*raster)); i++)
        {
            Xpost_Object v = xpost_dict_get(ctx, classdic,
                                            xpost_name_cons(ctx, raster[i]));

            if (xpost_object_get_type(v) != operatortype)
            {
                XPOST_LOG_ERR("device keeps its own raster but inherits %s,"
                              " which reads the base class's", raster[i]);
                return unregistered;
            }
        }

    return 0;
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
