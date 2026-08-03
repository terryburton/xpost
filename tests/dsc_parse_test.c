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

    xpost_quit();

    if (failures)
    {
        printf("FAILURES: %d\n", failures);
        return 1;
    }
    printf("SUCCESS\n");
    return 0;
}
