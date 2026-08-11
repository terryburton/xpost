/*
 * The page a run hands its embedder: who holds it, how long it lives,
 * and how it is given back.
 *
 * A run started with XPOST_OUTPUT_BUFFEROUT stores its finished page
 * through the address the embedder gave xpost_create(), and from that
 * moment the page is the embedder's. Three things follow, none of which
 * any PostScript-level test can reach, because the only party that ever
 * sees this buffer is the program that linked the library:
 *
 *   The page outlives the context. An embedder that renders a job and
 *   ends it destroys the context and then reads what it rendered, so a
 *   device that freed the page on the way out would hand back memory
 *   that stops being a page the moment the embedder is done asking. The
 *   reading here is done after xpost_destroy() for that reason, and it
 *   is a reading of every byte rather than a look at the pointer: a
 *   pointer says nothing about whether what it names is still there.
 *
 *   The page is given back through xpost_output_buffer_release(), which
 *   is the only call in this process that gives it back. What a leak
 *   checker says about a run of this test is therefore a statement about
 *   that call, and about nothing else.
 *
 *   The call is answerable for the corners embedders reach: nowhere to
 *   read a pointer from, a variable holding none, and the same variable
 *   released again after it has been. Each of those is nothing to give
 *   back rather than something to do twice.
 *
 * Which devices. Every device that keeps a page in a buffer of its own
 * hands that buffer over the same way, including the two that also write
 * the page to a file -- their raster is not scratch space that the file
 * replaces, it is a page an embedder may ask for as well. Those two are
 * here because nothing else exercises what they do at the handoff, and
 * they are compiled only where the libraries that encode their files
 * are, so each is asked for only where it exists. The raster device is
 * asked for everywhere and is what this holds when neither is compiled.
 *
 * What this does not cover: how the bytes of a page are arranged, which
 * is tests/raster_buffer_format_test.c's, and what the written files
 * hold, which is tests/run-raster-formats-test.sh's. Here a page is read
 * for the white it was erased to, which every one of these devices
 * starts a page as, so the reading says the buffer is whole and present
 * without saying anything about a format.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>

#include "xpost.h"

#include "xpost_test.h"

/* Small, because what is read is the whole of it and there is nothing in
   a larger page that a smaller one does not say. */
#define PAGE_W 8
#define PAGE_H 6

/* What is read of each page: three bytes a pixel, which is the narrowest
   of the arrangements these devices hold a page in and so is within
   every one of their buffers. */
#define PAGE_BYTES ((size_t)PAGE_W * PAGE_H * 3)

/* The page every one of these devices starts as, before a program has
   marked it. */
#define ERASED 255

/* One device: the name a context is created with, and the file its pages
   are written to, for a device that writes one. */
typedef struct
{
    const char *device;
    const char *file;
} Device;

static const Device devices[] =
{
    /* the device that keeps a page in a buffer and writes no file */
    { "raster", NULL },
#ifdef HAVE_LIBPNG
    { "png", "output_buffer_release_test.png" },
#endif
#ifdef HAVE_LIBJPEG
    { "jpeg", "output_buffer_release_test.jpg" },
#endif
};

/* Read the whole page as the embedder holding it does. Reported once:
   the first byte that is not the erased page says what happened, and a
   page that is gone rather than wrong is what a checker running over
   this reports for itself. */
static void page_is_erased(const Device *d, const unsigned char *page)
{
    size_t i;

    for (i = 0; i < PAGE_BYTES; i++)
        if (page[i] != ERASED)
        {
            report_failure("%s: byte %u of the page read after the context"
                           " was destroyed is %d where the erased page is %d",
                           d->device, (unsigned)i, page[i], ERASED);
            return;
        }
}

static void run_device(const Device *d)
{
    Xpost_Context *ctx;
    unsigned char *page = NULL;
    char def[256];
    char *defs[1];

    ctx = xpost_create(d->device, XPOST_OUTPUT_BUFFEROUT, &page,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, PAGE_W, PAGE_H);
    if (!ctx)
    {
        report_failure("%s: no context", d->device);
        return;
    }
    xpost_job_snapshots_set(ctx, 0);

    /* a device that writes its page to a file is told which file, so the
       run does not depend on where a page with no name settled */
    if (d->file)
    {
        snprintf(def, sizeof def, "OutputFileName=(%s)", d->file);
        defs[0] = def;
        if (!xpost_add_definitions(ctx, 1, defs))
            report_failure("%s: the output file name was not taken",
                           d->device);
    }

    /* the page is transmitted and not painted: what is asked of it is
       that it is there afterwards, not what is on it */
    if (xpost_run(ctx, XPOST_INPUT_STRING, "showpage\n", 0)
        != XPOST_RUN_COMPLETE)
    {
        report_failure("%s: the run did not complete", d->device);
        xpost_destroy(ctx);
        xpost_output_buffer_release(&page);
        return;
    }

    if (!page)
    {
        report_failure("%s: the run handed back no page", d->device);
        xpost_destroy(ctx);
        return;
    }

    /* the context goes first, and the page is read after it */
    xpost_destroy(ctx);
    page_is_erased(d, page);

    xpost_output_buffer_release(&page);
    if (page)
        report_failure("%s: the released pointer still names memory",
                       d->device);

    /* the same call on the variable it cleared: nothing left to give
       back, and not given back twice */
    xpost_output_buffer_release(&page);

    if (d->file)
        remove(d->file);
}

int main(void)
{
    size_t i;
    unsigned char *nothing = NULL;

    if (!xpost_init())
    {
        report_failure("xpost_init");
        return verdict();
    }

    /* nowhere to read a pointer from, and a variable holding none: both
       are nothing to give back */
    xpost_output_buffer_release(NULL);
    xpost_output_buffer_release(&nothing);
    if (nothing)
        report_failure("a variable holding no page was written to");

    /* One interpreter instance lives at a time, so each device is
       created, run and destroyed before the next is asked for. */
    for (i = 0; i < sizeof(devices) / sizeof(*devices); i++)
        run_device(&devices[i]);

    xpost_quit();

    return verdict();
}
