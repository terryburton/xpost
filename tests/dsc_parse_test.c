/* Document Structuring Convention parsing.
 *
 * libxpost_dsc parses comments out of a PostScript document for the
 * viewer and the dsc client. It reads whole files chosen by the user,
 * so a malformed one must be reported, not acted on.
 *
 * The header comments that carry lists -- DocumentFonts and its
 * relatives -- continue across %%+ lines. The list is accumulated into
 * the header record, and %%BeginFont entries later index a parallel
 * array sized for that list. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpost.h"
#include "xpost_dsc.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* parse a document held in memory; 1 if the parse reported success */
static int parse(const char *text, Xpost_Dsc *dsc)
{
    Xpost_Dsc_File *file;
    Xpost_Dsc_Status st;

    file = xpost_dsc_file_new_from_address((const unsigned char *)text, strlen(text));
    if (!file)
        return 0;
    st = xpost_dsc_parse(file, dsc);
    xpost_dsc_file_del(file);
    return st == XPOST_DSC_STATUS_SUCCESS;
}

int main(void)
{
    /* A DocumentFonts list continued over %%+, with a %%BeginFont entry
       for every font named. The font array must have room for all of
       them: sized from the continuation alone it holds none, and the
       entries are written past its end. */
    static const char continued[] =
        "%!PS-Adobe-3.0\n"
        "%%DocumentFonts: Courier Helvetica\n"
        "%%+ Times-Roman Symbol\n"
        "%%Pages: 1\n"
        "%%EndComments\n"
        "%%BeginSetup\n"
        "%%BeginFont: Courier\n%%EndFont\n"
        "%%BeginFont: Helvetica\n%%EndFont\n"
        "%%BeginFont: Times-Roman\n%%EndFont\n"
        "%%BeginFont: Symbol\n%%EndFont\n"
        "%%EndSetup\n"
        "%%Page: 1 1\n"
        "showpage\n"
        "%%Trailer\n"
        "%%EOF\n";

    /* the same list on one line: the array is sized from that line */
    static const char single[] =
        "%!PS-Adobe-3.0\n"
        "%%DocumentFonts: Courier Helvetica Times-Roman Symbol\n"
        "%%Pages: 1\n"
        "%%EndComments\n"
        "%%BeginSetup\n"
        "%%BeginFont: Courier\n%%EndFont\n"
        "%%BeginFont: Helvetica\n%%EndFont\n"
        "%%BeginFont: Times-Roman\n%%EndFont\n"
        "%%BeginFont: Symbol\n%%EndFont\n"
        "%%EndSetup\n"
        "%%Page: 1 1\n"
        "showpage\n"
        "%%Trailer\n"
        "%%EOF\n";

    Xpost_Dsc dsc;

    if (!xpost_init())
    {
        printf("FAIL: xpost_init\n");
        return 1;
    }

    memset(&dsc, 0, sizeof(dsc));
    check(parse(single, &dsc), "a document naming its fonts on one line parses");
    check(dsc.header.document_fonts.nbr == 4,
          "all four fonts are recorded from one line");
    xpost_dsc_free(&dsc);

    memset(&dsc, 0, sizeof(dsc));
    check(parse(continued, &dsc), "a document continuing its font list parses");
    check(dsc.header.document_fonts.nbr == 4,
          "all four fonts are recorded across the continuation");
    /* the parallel array is indexed by the recorded count: reaching this
       point without the allocator faulting is the assertion */
    xpost_dsc_free(&dsc);

    /* Version 2.1 of the conventions exists alongside 1.0, 2.0 and 3.0
       and a document declaring it conforms. */
    memset(&dsc, 0, sizeof(dsc));
    check(parse("%!PS-Adobe-2.1\n"
                "%%Pages: 1\n"
                "%%EndComments\n"
                "showpage\n", &dsc),
          "a version 2.1 document is recognized");
    check(dsc.ps_vmaj == 2 && dsc.ps_vmin == 1,
          "the 2.1 version numbers are recorded");
    xpost_dsc_free(&dsc);

    /* The needed and supplied font lists are distinct comments and each
       must land in its own record. */
    memset(&dsc, 0, sizeof(dsc));
    check(parse("%!PS-Adobe-2.0\n"
                "%%DocumentNeededFonts: Courier\n"
                "%%DocumentSuppliedFonts: Zapf Dingbats\n"
                "%%EndComments\n"
                "showpage\n", &dsc),
          "a document listing needed and supplied fonts parses");
    check(dsc.header.document_needed_fonts.nbr == 1,
          "the needed font is recorded");
    check(dsc.header.document_supplied_fonts.nbr == 2,
          "the supplied fonts are recorded");
    xpost_dsc_free(&dsc);

    /* A comment on the file's final line spans up to the end of the
       file; its value must be captured whether or not a newline
       follows it. */
    memset(&dsc, 0, sizeof(dsc));
    check(parse("%!PS-Adobe-3.0\n"
                "%%Title: hello\n", &dsc),
          "a document ending at its title line parses");
    check(dsc.header.title && strcmp(dsc.header.title, "hello") == 0,
          "the title on a newline-terminated final line is captured");
    xpost_dsc_free(&dsc);

    memset(&dsc, 0, sizeof(dsc));
    check(parse("%!PS-Adobe-3.0\n"
                "%%Title: hello", &dsc),
          "a document ending inside its title line parses");
    check(dsc.header.title && strcmp(dsc.header.title, "hello") == 0,
          "the title on an unterminated final line is captured");
    xpost_dsc_free(&dsc);

    /* A section opened by the file's last line starts where the file
       ends: the recorded offset stays inside the file. */
    {
        static const char endcomments_last[] =
            "%!PS-Adobe-3.0\n"
            "%%EndComments\n";

        memset(&dsc, 0, sizeof(dsc));
        check(parse(endcomments_last, &dsc),
              "a document ending at EndComments parses");
        check(dsc.prolog.start == (ptrdiff_t)strlen(endcomments_last),
              "a prolog opened by the final line starts at the file's end");
        xpost_dsc_free(&dsc);
    }

    {
        static const char page_last[] =
            "%!PS-Adobe-3.0\n"
            "%%Pages: 1\n"
            "%%EndComments\n"
            "%%EndProlog\n"
            "%%Page: one 1\n";

        memset(&dsc, 0, sizeof(dsc));
        check(parse(page_last, &dsc),
              "a document ending at a Page comment parses");
        check(dsc.pages && dsc.header.pages == 1 &&
              dsc.pages[0].section.start == (ptrdiff_t)strlen(page_last),
              "a page opened by the final line starts at the file's end");
        xpost_dsc_free(&dsc);
    }

    /* A %%Page comment carries a label and an ordinal on its own line.
       When the ordinal is absent the comment is malformed and must be
       reported; the argument scan must not wander onto the following
       line and pick a number out of the page's contents. */
    memset(&dsc, 0, sizeof(dsc));
    check(!parse("%!PS-Adobe-3.0\n"
                 "%%Pages: 2\n"
                 "%%EndComments\n"
                 "%%EndProlog\n"
                 "%%Page: one\n"
                 "7\n"
                 "%%Page: two 2\n"
                 "%%Trailer\n", &dsc),
          "a Page comment without an ordinal is reported");
    check(!(dsc.pages && dsc.header.pages >= 1 && dsc.pages[0].ordinal == 7),
          "no ordinal is taken from the page's contents");
    xpost_dsc_free(&dsc);

    /* Under level 1 a page whose position is unknown carries ? as its
       ordinal; it is recorded as -1 and the pages after it keep
       parsing. */
    memset(&dsc, 0, sizeof(dsc));
    check(parse("%!PS-Adobe-1.0\n"
                "%%Pages: 2\n"
                "%%EndComments\n"
                "%%EndProlog\n"
                "%%Page: one ?\n"
                "showpage\n"
                "%%Page: two 2\n"
                "showpage\n"
                "%%Trailer\n", &dsc),
          "a level 1 document with a ? page ordinal parses");
    check(dsc.pages && dsc.header.pages == 2 && dsc.pages[0].ordinal == -1,
          "the ? ordinal is recorded as -1");
    check(dsc.pages && dsc.header.pages == 2 && dsc.pages[1].label &&
          strcmp(dsc.pages[1].label, "two") == 0 && dsc.pages[1].ordinal == 2,
          "the page after a ? ordinal is still recorded");
    xpost_dsc_free(&dsc);

    /* An ordinal that is not a positive integer is malformed and must
       be reported, not silently end the parse mid-file. */
    memset(&dsc, 0, sizeof(dsc));
    check(!parse("%!PS-Adobe-3.0\n"
                 "%%Pages: 2\n"
                 "%%EndComments\n"
                 "%%EndProlog\n"
                 "%%Page: one xyz\n"
                 "showpage\n"
                 "%%Page: two 2\n"
                 "%%Trailer\n", &dsc),
          "a Page comment with a non-numeric ordinal is reported");
    xpost_dsc_free(&dsc);

    memset(&dsc, 0, sizeof(dsc));
    check(!parse("%!PS-Adobe-3.0\n"
                 "%%Pages: 2\n"
                 "%%EndComments\n"
                 "%%EndProlog\n"
                 "%%Page: one 0\n"
                 "showpage\n"
                 "%%Page: two 2\n"
                 "%%Trailer\n", &dsc),
          "a Page comment with a non-positive ordinal is reported");
    xpost_dsc_free(&dsc);

    /* Only the first of a repeated header text comment counts; a
       repeat must not disturb the recorded value. */
    memset(&dsc, 0, sizeof(dsc));
    check(parse("%!PS-Adobe-3.0\n"
                "%%Title: first\n"
                "%%Title: second\n"
                "%%EndComments\n"
                "showpage\n", &dsc),
          "a document repeating its title comment parses");
    check(dsc.header.title && strcmp(dsc.header.title, "first") == 0,
          "the first title is the one recorded");
    xpost_dsc_free(&dsc);

    /* A declared page count beyond the integer range is not a count; it
       must not wrap into a small one. 4294967297 is 2^32 + 1. */
    memset(&dsc, 0, sizeof(dsc));
    parse("%!PS-Adobe-3.0\n"
          "%%Pages: 4294967297\n"
          "%%EndComments\n"
          "%%EndProlog\n"
          "%%Page: one 1\n"
          "showpage\n"
          "%%Trailer\n", &dsc);
    check(dsc.header.pages == 0 && !dsc.pages,
          "a page count beyond integer range is not recorded");
    xpost_dsc_free(&dsc);

    /* More %%Page comments than the header declared: the extras cannot
       be recorded, and the labels captured for them must be released.
       The excess page count is the observable; the label release is
       held by the leak checker. */
    memset(&dsc, 0, sizeof(dsc));
    check(parse("%!PS-Adobe-3.0\n"
                "%%Pages: 1\n"
                "%%EndComments\n"
                "%%EndProlog\n"
                "%%Page: one 1\n"
                "showpage\n"
                "%%Page: two 2\n"
                "showpage\n"
                "%%Trailer\n", &dsc),
          "a document with more pages than declared parses");
    check(dsc.pages && dsc.header.pages == 1 && dsc.pages[0].label &&
          strcmp(dsc.pages[0].label, "one") == 0,
          "the declared page is recorded");
    xpost_dsc_free(&dsc);

    /* Number conversion must judge only its own outcome: an errno left
       over from unrelated earlier work must not make a zero coordinate
       look like a conversion failure. */
    memset(&dsc, 0, sizeof(dsc));
    errno = ERANGE;
    check(parse("%!PS-Adobe-3.0\n"
                "%%BoundingBox: 0 0 612 792\n"
                "%%EndComments\n"
                "showpage\n", &dsc),
          "a bounding box parses regardless of entry errno");
    check(dsc.header.bounding_box.llx == 0 &&
          dsc.header.bounding_box.lly == 0 &&
          dsc.header.bounding_box.urx == 612 &&
          dsc.header.bounding_box.ury == 792,
          "a zero coordinate converts with errno set on entry");
    xpost_dsc_free(&dsc);

    xpost_quit();

    if (failures)
    {
        printf("FAILURES: %d\n", failures);
        return 1;
    }
    printf("SUCCESS\n");
    return 0;
}
