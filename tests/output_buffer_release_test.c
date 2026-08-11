/*
 * The page a run hands its embedder: who holds it, how long it lives,
 * and how it is given back. Asked of the whole family that can hand one
 * over, rather than of one device at a time.
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
 * Why a family and not a device. Every one of these questions is the
 * same question of every device that keeps its page in a buffer outside
 * the PostScript virtual machine, and they were asked device by device
 * -- so two of the four then compiled answered one of them wrongly, and
 * freed a handed-over page at Destroy, for as long as it took someone to
 * read the sources side by side. The roster below is the family, every
 * member is put to every question, and tests/check-buffer-family.sh
 * derives the membership from the sources so a device that later gains a
 * handoff cannot be left out of it.
 *
 * A device that cannot be asked is named as one. Three of the members
 * need a library to encode the file they also write, and a build without
 * it has no such device -- but a roster that skips what it cannot ask
 * reports the same success as one that asked everything, so each member
 * says which it was, the ones that could not are named with the reason,
 * and a member declared unbuildable that renders a page anyway fails as
 * loudly as one that was expected to and did not. A device the build
 * left the driver out of registers no maker, which the start-up device
 * is held to, so what says a member is absent is its first run refusing
 * rather than a context that was never made: the context is made either
 * way.
 *
 * The two arms, and what each settles:
 *
 *   The handoff. The page is transmitted, the context destroyed, and
 *   every byte of the page read afterwards against the pixel an unmarked
 *   page of that device holds. Then it is given back, the variable is
 *   held to naming nothing, and the same call is made on it again.
 *
 *   The lent buffer. A run started with XPOST_OUTPUT_BUFFERIN renders
 *   into memory the embedder already owns, and that memory is never the
 *   library's to give back: the device records it as no block of its
 *   own, so a release through it does nothing and a Destroy leaves it
 *   alone. One device renders into a lent buffer and the rest ignore
 *   one, which is a difference between them rather than a question only
 *   one of them has; both answers are held, so a device that starts
 *   honouring a lent buffer without recording it is caught here rather
 *   than by an embedder whose heap it corrupts.
 *
 * What this does not cover: how the bytes of a page are arranged, which
 * is tests/raster_buffer_format_test.c's, and what the written files
 * hold, which is tests/run-raster-formats-test.sh's. Here a page is read
 * for the ground it was erased to, which says the buffer is whole and
 * present without saying anything about a format.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpost.h"

#include "xpost_test.h"

/* Small, because what is read is the whole of it and there is nothing in
   a larger page that a smaller one does not say. */
#define PAGE_W 8
#define PAGE_H 6

/* Whether this build has the library each member needs. Written as a
   value rather than as a fence around the roster: a member left out of
   the table is a member nothing says was left out. */
#ifdef HAVE_LIBPNG
# define BUILT_PNG 1
#else
# define BUILT_PNG 0
#endif
#ifdef HAVE_LIBJPEG
# define BUILT_JPEG 1
#else
# define BUILT_JPEG 0
#endif

/* The most bytes a pixel takes in any member's buffer. */
#define MAX_PIXEL 4

/* One member of the family: the name a context is created with, the file
   its pages are written to for a member that writes one, whether this
   build has the library that member needs, the pixel of an unmarked page
   and how many bytes of it the buffer carries, and whether the member
   renders into a buffer the embedder lends it. */
typedef struct
{
    const char *device;
    const char *file;
    int built;
    int pixel;
    unsigned char ground[MAX_PIXEL];
    int lends;
} Member;

/* The roster. Membership is the sources' answer, not this table's:
   tests/check-buffer-family.sh reads the devices whose driver hands a
   raster over and holds this list to them, so a device that gains a
   handoff and is not added here fails there.

   The ground each carries is its own: the members that write a file
   start a page opaque white like the rest, and the one that writes a
   file with transparency in it starts a page clear, so that only what a
   job marks carries opacity. A single expected pixel for the family
   would have had to be the loosest of them. */
static const Member members[] =
{
    { "raster",   NULL, 1, 3, { 255, 255, 255, 0   }, 1 },
    { "bgr",      NULL, 1, 3, { 255, 255, 255, 0   }, 0 },
    { "png",      "output_buffer_release_test.png",
                        BUILT_PNG,  4, { 255, 255, 255, 255 }, 0 },
    { "pngalpha", "output_buffer_release_test_alpha.png",
                        BUILT_PNG,  4, { 255, 255, 255, 0   }, 0 },
    { "jpeg",     "output_buffer_release_test.jpg",
                        BUILT_JPEG, 3, { 255, 255, 255, 0   }, 0 }
};

#define MEMBERS ((int)(sizeof(members) / sizeof(*members)))

/* The bytes one member's whole page comes to. */
static size_t page_bytes(const Member *m)
{
    return (size_t)PAGE_W * PAGE_H * (size_t)m->pixel;
}

/* The byte a member's unmarked page holds at one offset. */
static unsigned char ground_byte(const Member *m, size_t offset)
{
    return m->ground[offset % (size_t)m->pixel];
}

/* Read the whole page as the embedder holding it does. Reported once:
   the first byte that is not the unmarked page says what happened, and a
   page that is gone rather than wrong is what a checker running over
   this reports for itself. */
static void page_is_ground(const Member *m, const unsigned char *page)
{
    size_t i, n = page_bytes(m);

    for (i = 0; i < n; i++)
        if (page[i] != ground_byte(m, i))
        {
            report_failure("%s: byte %u of the page read after the context"
                           " was destroyed is %d where the unmarked page is"
                           " %d", m->device, (unsigned)i, page[i],
                           ground_byte(m, i));
            return;
        }
}

/* Start a context on one member. Answers NULL where the member could not
   be made at all, which is a failure for every member this build has the
   library for -- which is every member reaching here. */
static Xpost_Context *member_context(const Member *m, Xpost_Output_Type type,
                                     void *outputptr, const char *arm)
{
    Xpost_Context *ctx;
    char def[256];
    char *defs[1];

    ctx = xpost_create(m->device, type, outputptr,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, PAGE_W, PAGE_H);
    if (!ctx)
    {
        report_failure("%s: no context for the %s", m->device, arm);
        return NULL;
    }
    xpost_job_snapshots_set(ctx, 0);

    /* a member that writes its page to a file is told which file, so the
       run does not depend on where a page with no name settled */
    if (m->file)
    {
        snprintf(def, sizeof def, "OutputFileName=(%s)", m->file);
        defs[0] = def;
        if (!xpost_add_definitions(ctx, 1, defs))
            report_failure("%s: the output file name was not taken",
                           m->device);
    }
    return ctx;
}

/* The first arm: the page the run hands over is the embedder's, outlives
   the context, and is given back once. */
static void handed_over(const Member *m)
{
    Xpost_Context *ctx;
    unsigned char *page = NULL;

    ctx = member_context(m, XPOST_OUTPUT_BUFFEROUT, &page, "handoff");
    if (!ctx)
        return;

    /* the page is transmitted and not painted: what is asked of it is
       that it is there afterwards, not what is on it */
    if (xpost_run(ctx, XPOST_INPUT_STRING, "showpage\n", 0)
        != XPOST_RUN_COMPLETE)
    {
        report_failure("%s: the run did not complete", m->device);
        xpost_destroy(ctx);
        xpost_output_buffer_release(&page);
        return;
    }

    if (!page)
    {
        report_failure("%s: the run handed back no page", m->device);
        xpost_destroy(ctx);
        return;
    }

    /* the context goes first, and the page is read after it */
    xpost_destroy(ctx);
    page_is_ground(m, page);

    xpost_output_buffer_release(&page);
    if (page)
        report_failure("%s: the released pointer still names memory",
                       m->device);

    /* the same call on the variable it cleared: nothing left to give
       back, and not given back twice */
    xpost_output_buffer_release(&page);

    if (m->file)
        remove(m->file);
}

/* The byte a lent buffer is filled with before the run, chosen so that
   neither an unmarked page nor a cleared pointer can pass for it. */
#define LENT_FILL 0x5a

/* Room for any member's header in front of the page it lends. The
   headers are the drivers' own and are not declared outside them, so the
   space is generous rather than exact; what the reading below locates is
   the page itself. */
#define LENT_HEADROOM 4096

/* Where in a lent buffer the whole unmarked page was rendered, or -1
   where none of it was. */
static long page_in(const Member *m, const unsigned char *lent, size_t n)
{
    size_t need = page_bytes(m);
    size_t start, i;

    if (n < need)
        return -1;
    for (start = 0; start <= n - need; start++)
    {
        for (i = 0; i < need; i++)
            if (lent[start + i] != ground_byte(m, i))
                break;
        if (i == need)
            return (long)start;
    }
    return -1;
}

/* The second arm: a buffer the embedder lends the run is the embedder's
   throughout. A member that renders into one records it as no block of
   its own, so a release through the page it rendered gives nothing back
   and the embedder frees its own buffer afterwards; a member that
   ignores a lent buffer leaves it as it found it. Which of the two a
   member is, is stated in the roster and held here, so a member that
   changes sides fails rather than passing quietly under the other's
   rule. */
static void lent_buffer(const Member *m)
{
    Xpost_Context *ctx;
    unsigned char *lent;
    unsigned char *page;
    size_t n = LENT_HEADROOM + page_bytes(m);
    long at;
    size_t i;

    lent = malloc(n);
    if (!lent)
    {
        report_failure("%s: no memory to lend", m->device);
        return;
    }
    memset(lent, LENT_FILL, n);

    ctx = member_context(m, XPOST_OUTPUT_BUFFERIN, lent, "lent buffer");
    if (!ctx)
    {
        free(lent);
        return;
    }

    if (xpost_run(ctx, XPOST_INPUT_STRING, "showpage\n", 0)
        != XPOST_RUN_COMPLETE)
    {
        report_failure("%s: the run over a lent buffer did not complete",
                       m->device);
        xpost_destroy(ctx);
        free(lent);
        return;
    }

    /* the buffer is the embedder's throughout, so it is read -- and
       freed -- after the context that may have rendered into it has gone */
    xpost_destroy(ctx);

    if (!m->lends)
    {
        /* a member that does not render into a lent buffer leaves every
           byte of it as the embedder set it */
        for (i = 0; i < n; i++)
            if (lent[i] != LENT_FILL)
            {
                report_failure("%s: byte %u of a buffer this device does not"
                               " render into reads %d where the embedder left"
                               " %d", m->device, (unsigned)i, lent[i],
                               LENT_FILL);
                break;
            }
        free(lent);
        if (m->file)
            remove(m->file);
        return;
    }

    at = page_in(m, lent, n);
    if (at < 0)
    {
        report_failure("%s: no page was rendered into the buffer it was lent",
                       m->device);
        free(lent);
        return;
    }
    /* nothing past the page: a run that rendered beyond the page it was
       given a buffer for wrote over memory the embedder kept for itself */
    for (i = (size_t)at + page_bytes(m); i < n; i++)
        if (lent[i] != LENT_FILL)
        {
            report_failure("%s: byte %u, past the page rendered into the lent"
                           " buffer, reads %d where the embedder left %d",
                           m->device, (unsigned)i, lent[i], LENT_FILL);
            break;
        }
    /* and the block named in front of it is none: the memory is the
       embedder's, so there is nothing here for a release to give back */
    if ((size_t)at < sizeof(void *))
    {
        report_failure("%s: the page was rendered with no room in front of it"
                       " for the block a release reads", m->device);
        free(lent);
        return;
    }
    for (i = 1; i <= sizeof(void *); i++)
        if (lent[(size_t)at - i] != 0)
        {
            report_failure("%s: the block named in front of a page rendered"
                           " into a lent buffer is not none, so a release"
                           " through it would give back memory the embedder"
                           " owns", m->device);
            /* and it is not released here: the release gives back what
               the block names, and this has just shown it names the
               embedder's own buffer */
            free(lent);
            if (m->file)
                remove(m->file);
            return;
        }

    /* the embedder's own sequence: release through the page, which gives
       nothing back, and then free what it lent */
    page = lent + at;
    xpost_output_buffer_release(&page);
    if (page)
        report_failure("%s: releasing through a lent page left the pointer"
                       " naming memory", m->device);
    free(lent);

    if (m->file)
        remove(m->file);
}

/* A member this build has no library for is put to the question anyway,
   and what is held is that it cannot answer: the run refuses and hands
   back no page. A member declared absent that renders one is a member
   whose reason has stopped being true, and every question it was excused
   from is one it can now be held to. */
static void cannot_be_asked(const Member *m)
{
    Xpost_Context *ctx;
    unsigned char *page = NULL;

    ctx = xpost_create(m->device, XPOST_OUTPUT_BUFFEROUT, &page,
                       XPOST_SHOWPAGE_NOPAUSE, XPOST_OUTPUT_MESSAGE_QUIET,
                       XPOST_USE_SIZE, PAGE_W, PAGE_H);
    if (ctx)
    {
        xpost_job_snapshots_set(ctx, 0);
        if (xpost_run(ctx, XPOST_INPUT_STRING, "showpage\n", 0)
            == XPOST_RUN_COMPLETE && page)
            report_failure("%s: this build is declared to have no library to"
                           " encode its file, and it rendered a page anyway",
                           m->device);
        xpost_destroy(ctx);
        xpost_output_buffer_release(&page);
    }
    printf("UNASKED: %s (the library that encodes its file is not compiled"
           " in)\n", m->device);
}

int main(void)
{
    int i;
    int asked = 0;
    int floor = 0;
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

    /* One interpreter instance lives at a time, so each member is
       created, run and destroyed before the next is asked for. */
    for (i = 0; i < MEMBERS; i++)
    {
        const Member *m = &members[i];

        if (!m->built)
        {
            cannot_be_asked(m);
            continue;
        }

        /* Said before the member is asked rather than after it answered.
           A device that breaks this contract breaks memory, and a
           checker watching for that ends the process where it happens --
           so what names the device is the last line printed, and a line
           printed on the way out would name the one before it. */
        printf("ASKING: %s\n", m->device);
        fflush(stdout);
        handed_over(m);
        lent_buffer(m);
        asked++;
    }

    xpost_quit();

    /* A roster that answered for nothing reports as quietly as one that
       answered for everything, so what was asked is said. The floor is
       the members needing no library, which every build has. */
    printf("output-buffer-release: held on %d of %d family member(s)\n",
           asked, MEMBERS);
    for (i = 0; i < MEMBERS; i++)
        if (members[i].built && !members[i].file)
            floor++;
    if (asked < floor)
        report_failure("%d family member(s) answered where %d need no library"
                       " at all", asked, floor);

    return verdict();
}
