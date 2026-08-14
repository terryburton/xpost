/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * Copyright (C) 2013, Vincent Torri
 * Copyright (C) 2013, Thorsten Behrens
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

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif

#include "xpost.h"
#include "xpost_log.h"
#ifdef _MSC_VER
# include "xpost_compat.h"
#endif

#include "xpost_main.h"


#define XPOST_MAIN_IF_OPT(so, lo, opt)  \
if ((!strcmp(argv[i], so)) || \
   (!strncmp(argv[i], lo, sizeof(lo) - 1))) \
{ \
    if (*(argv[i] + 2) == '\0') \
    { \
        if ((i + 1) < argc) \
        { \
            i++; \
            opt = argv[i]; \
        } \
        else \
        { \
            XPOST_LOG_ERR("missing option value"); \
            _xpost_main_usage(filename); \
            goto quit_xpost; \
        } \
    } \
    else \
    { \
        if (!*(argv[i] + sizeof(lo) - 1)) \
        { \
            XPOST_LOG_ERR("missing option value"); \
            _xpost_main_usage(filename); \
            goto quit_xpost; \
        } \
        else \
        { \
            opt = argv[i] + sizeof(lo) - 1; \
        } \
    } \
}

static const char *_xpost_main_devices[] =
{
    "pgm",
    "ppm",
    "pbm",
    "tiff",
    "null",
    "bbox",
#ifdef _WIN32
    "gdi",
    "gl",
#endif
#ifdef HAVE_XCB
    "xcb",
#endif
    "bgr",
    "raster",
    "record",
    "pdfwrite",
    "dscwrite",
    "svgwrite",
#ifdef HAVE_LIBPNG
    "png",
    "pngalpha",
#endif
#ifdef HAVE_LIBJPEG
    "jpeg",
#endif
    NULL
};

static void
_xpost_main_license(void)
{
    printf("BSD 3-clause\n");
}

static void
_xpost_main_version(const char *filename)
{
    int maj;
    int min;
    int mic;

    xpost_version_get(&maj, &min, &mic);
    printf("%s %d.%d.%d\n", filename, maj, min, mic);
}

/* permit the directory containing `path`, for writing when `forwrite` */
static void
_xpost_permit_file_dir(const char *path, int forwrite)
{
    char buf[4096];
    char *slash;

    if (!path || strlen(path) >= sizeof buf)
        return;
    strcpy(buf, path);
    slash = strrchr(buf, '/');
    if (slash)
    {
        *slash = '\0';
        if (buf[0] == '\0')
            strcpy(buf, "/");
    }
    else
    {
        strcpy(buf, ".");
    }
    if (forwrite)
        xpost_path_permit_write(buf);
    else
        xpost_path_permit_read(buf);
}

static void
_xpost_main_usage(const char *filename)
{
    int i;

    printf("Usage: %s [options] [file.ps]\n\n", filename);
    printf("Postscript level 2 interpreter\n\n");
    printf("Options:\n");
    printf("  -o, --output=[FILE]                output file; the run ends with the program\n");
    printf("  -d, --device=[STRING]              device name\n");
    printf("  -Dname=token, --define name=token  add definition to userdict\n");
    printf("  -I[DIR], --include [DIR]           add a resource search directory\n");
    printf("  --no-graphics                      lock down and run without loading graphics\n");
    printf("  --no-sandbox                       allow the program unrestricted file access\n");
    printf("  -g, --geometry=WxH{+-}X{+-}Y       geometry specification\n");
    printf("  -s, --spill=auto|never|always      where a retained page's marks are held\n");
    printf("  -b, --band-bytes=BYTES             what one band of a page may cost\n");
    printf("  -q, --quiet                        suppress interpreter messages (default)\n");
    printf("  -v, --verbose                      do not go quiet into that good night\n");
    printf("  -t, --trace                        add additional tracing messages, implies -v\n");
    printf("  -L, --license                      show program license\n");
    printf("  -V, --version                      show program version\n");
    printf("  -h, --help                         show this message\n");
    printf("\n");
    printf("  Supported devices:\n");
    i = 0;
    while (_xpost_main_devices[i])
        printf("\t%s\n", _xpost_main_devices[i++]);
    printf("\n");
    printf("  A device whose page may arrive a band at a time holds a\n");
    printf("  band of it rather than the page: pgm, ppm, pbm, tiff, png\n");
    printf("  and jpeg. A page small enough to fit one band is held\n");
    printf("  whole, so this costs a small page nothing.\n");
    printf("\n");
    printf("  How large a band is is --band-bytes, in bytes of raster\n");
    printf("  held at once, and it decides both things above: a page the\n");
    printf("  budget covers arrives in one band, which is the page, so\n");
    printf("  it is painted directly and nothing is written down. The\n");
    printf("  default covers every ordinary sheet, so lowering it is\n");
    printf("  what bands an ordinary page. It bounds the marks too --\n");
    printf("  they go to a scratch file past a budget's worth of them --\n");
    printf("  so a banded page costs a band of raster and a budget of\n");
    printf("  marks whatever the drawing. currentsystemparams reports\n");
    printf("  the budget as MaxBandBytes and the band it bought as\n");
    printf("  CurBandHeight.\n");
    printf("\n");
    printf("  A device may be given a mode after a colon:\n");
    printf("\tDEVICE:whole    hold the whole page rather than a band of\n");
    printf("\t                it, which is what to compare against\n");
    printf("\tDEVICE:band     hold a band of it whatever the page size\n");
    printf("\traster:FORMAT   the pixel format a lent framebuffer is in\n");
    printf("\t                (rgb, argb, bgr, bgra)\n");
    printf("\n");
    printf("  record is the class a banded page is held by, and takes no\n");
    printf("  mode: selecting it is the same as ppm:band.\n");
    printf("\n");
    printf("  A page held a band at a time is held as the marks that made\n");
    printf("  it, and --spill says where those marks go:\n");
    printf("\tauto      in memory while they come to less than the\n");
    printf("\t          raster banding the page saves, and in a scratch\n");
    printf("\t          file past that. The default, and the only one\n");
    printf("\t          that bounds what a page costs without touching\n");
    printf("\t          a disk for a page that does not need it\n");
    printf("\tnever     in memory whatever they come to, touching no\n");
    printf("\t          scratch file at all. What a page costs then\n");
    printf("\t          follows its drawing with no limit\n");
    printf("\talways    in a scratch file from the first mark; refused\n");
    printf("\t          at start-up where no scratch file can be made\n");
}

static int
_xpost_atoi(char *str, int *v, char **endptr)
{
    long val;

    errno = 0;
    val = strtol(str, endptr, 10);;

    if (((errno == ERANGE) &&
         ((val == LONG_MAX) || (val == LONG_MIN))) ||
        ((errno != 0) && (val == 0)))
        return 0;

    if (*endptr == str)
        return 0;

    *v = (int)val;

    return 1;
}

static int
_xpost_geometry_parse(const char *geometry, int *width, int *height, int *xoffset, int *xsign, int *yoffset, int *ysign)
{
    char *str;
    char *endptr;
    int val;

    if (!geometry)
        return 0;

    /* width */
    str = (char *)geometry;
    if (!_xpost_atoi(str, &val, &endptr))
        return 0;

    *width = val;

    if (*endptr != 'x')
        return 0;

    /* height */
    str = endptr + 1;
    if (!_xpost_atoi(str, &val, &endptr))
        return 0;

    *height = val;

    if (*endptr == '+')
        *xsign = 1;
    else if (*endptr == '-')
        *xsign = -1;
    else
        return 0;

    /* xoffset */
    str = endptr + 1;
    if (!_xpost_atoi(str, &val, &endptr))
        return 0;

    *xoffset = val;

    if (*endptr == '+')
        *ysign = 1;
    else if (*endptr == '-')
        *ysign = -1;
    else
        return 0;

    /* yoffset */
    str = endptr + 1;
    if (!_xpost_atoi(str, &val, &endptr))
        return 0;

    *yoffset = val;

    if (*endptr != '\0')
        return 0;

    return 1;
}

/* Add one copy of str to a list that grows by one each time. The list and
   its count travel together, and a list that has taken nothing yet is the
   null pointer with a count of zero. Answers zero if the copy or the room
   for it could not be had, leaving the list exactly as it was. */
static int
_xpost_main_list_add(char ***list, int *count, const char *str)
{
    char **grown;
    char *copy;

    copy = strdup(str);
    if (!copy)
        return 0;
    grown = realloc(*list, (*count + 1) * sizeof *grown);
    if (!grown)
    {
        free(copy);
        return 0;
    }
    *list = grown;
    (*list)[(*count)++] = copy;
    return 1;
}

static void
_xpost_main_interrupt(int sig)
{
    (void)sig;
#ifdef _WIN32
    /* the C runtime resets the disposition before the handler runs */
    signal(SIGINT, _xpost_main_interrupt);
#endif
    xpost_interrupt();
}

int main(int argc, char *argv[])
{
    Xpost_Context *ctx;
    const char *geometry = NULL;
    const char *output_file = NULL;
    const char *device = NULL;
    const char *spill = NULL;
    const char *band_bytes = NULL;
    const char *ps_file = NULL;
    const char *filename = argv[0];
    const char *define = NULL;
    char **defs = NULL;
    int num_defs = 0;
    char **incs = NULL;
    int num_incs = 0;
    int no_graphics = 0;
    int no_sandbox = 0;
    int output_msg = XPOST_OUTPUT_MESSAGE_QUIET;
    int have_device;
    int width = -1;
    int height = -1;
    int xoffset = 0;
    int yoffset = 0;
    int xsign = 1;
    int ysign = 1;
    int have_geometry = 0;
    int maj;
    int min;
    int mic;
    int i;

    xpost_version_get(&maj, &min, &mic);
    printf("Xpost %d.%d.%d\n", maj, min, mic);
    printf("Copyright (C) 2013, Michael Joshua Ryan. All rights reserved.\n");
    printf("This software is supplied under the BSD 3 clause and comes with NO WARRANTY:\n");
    printf("see the file COPYING for details.\n");

#ifdef HAVE_SIGACTION
    struct sigaction sa, oldsa;

    sa.sa_handler = SIG_IGN;
    sigaction(SIGTRAP, &sa, &oldsa);
#endif

#ifdef DEBUG_ENTS
    printf("EXTRA_BITS_SIZE = %u\n", (unsigned int)XPOST_OBJECT_TAG_EXTRA_BITS_SIZE);
    printf("COMP_MAX_ENT = %u\n", (unsigned int)XPOST_OBJECT_COMP_MAX_ENT);
#endif

#ifdef _WIN32
    device = "gdi";
#elif defined HAVE_XCB
    device = "xcb";
#else
    device = "pgm";
#endif

    if (!xpost_init())
    {
        fprintf(stderr, "Fail to initialize xpost\n");
        return -1;
    }

    /* control-C requests the PostScript interrupt error rather than
       killing the process; a blocked read resumes and the request
       lands at the next evaluation step */
#ifdef _WIN32
    signal(SIGINT, _xpost_main_interrupt);
#else
    {
        struct sigaction sa;

        memset(&sa, 0, sizeof sa);
        sa.sa_handler = _xpost_main_interrupt;
        sa.sa_flags = SA_RESTART;
        sigaction(SIGINT, &sa, NULL);
    }
#endif

    i = 0;
    while (++i < argc)
    {
        if (*argv[i] == '-')
        {
            /* The three options that report and stop leave through the
               same shutdown as every other exit from here. xpost_init
               above took what the process holds for as long as it runs --
               the font configuration's cache, and on some platforms the
               socket library and a handle on the system's random source --
               and xpost_quit is what gives each of them back; a path that
               returned without it would hold them to the end of the
               process and be answerable for them there. The label below
               is the failing exit and these three succeeded. */
            if ((!strcmp(argv[i], "-h")) ||
                (!strcmp(argv[i], "--help")))
            {
                _xpost_main_usage(filename);
                xpost_quit();
                return EXIT_SUCCESS;
            }
            else if ((!strcmp(argv[i], "-V")) ||
                     (!strcmp(argv[i], "--version")))
            {
                _xpost_main_version(filename);
                xpost_quit();
                return EXIT_SUCCESS;
            }
            else if ((!strcmp(argv[i], "-L")) ||
                     (!strcmp(argv[i], "--license")))
            {
                _xpost_main_license();
                xpost_quit();
                return EXIT_SUCCESS;
            }
            else if ((!strncmp(argv[i], "-D", 2)) ||
                     (!strcmp(argv[i], "--define")))
            {
                if (argv[i][1]=='D')
                {
                    define = argv[i] + 2;
                }
                else
                {
                    if ((i + 1) < argc)
                    {
                        ++i;
                        define = argv[i];
                    }
                    else
                    {
                        XPOST_LOG_ERR("missing option value");
                        _xpost_main_usage(filename);
                        goto quit_xpost;
                    }

                }
                if (!_xpost_main_list_add(&defs, &num_defs, define))
                {
                    XPOST_LOG_ERR("out of memory");
                    goto quit_xpost;
                }
            }
            else if ((!strncmp(argv[i], "-I", 2)) ||
                     (!strcmp(argv[i], "--include")))
            {
                const char *inc;
                if (argv[i][1] == 'I' && argv[i][2])
                {
                    inc = argv[i] + 2;
                }
                else if ((i + 1) < argc)
                {
                    inc = argv[++i];
                }
                else
                {
                    XPOST_LOG_ERR("missing option value");
                    _xpost_main_usage(filename);
                    goto quit_xpost;
                }
                if (!_xpost_main_list_add(&incs, &num_incs, inc))
                {
                    XPOST_LOG_ERR("out of memory");
                    goto quit_xpost;
                }
            }
            else if (!strcmp(argv[i], "--no-sandbox"))
            {
                no_sandbox = 1;
            }
            else if ((!strcmp(argv[i], "-q")) ||
                     (!strcmp(argv[i], "--quiet")))
            {
                output_msg = XPOST_OUTPUT_MESSAGE_QUIET;
            }
            else if (!strcmp(argv[i], "--no-graphics"))
            {
                no_graphics = 1;
            }
            else if ((!strcmp(argv[i], "-v")) ||
                     (!strcmp(argv[i], "--verbose")))
            {
                output_msg = XPOST_OUTPUT_MESSAGE_VERBOSE;
            }
            else if ((!strcmp(argv[i], "-t")) ||
                     (!strcmp(argv[i], "--trace")))
            {
                output_msg = XPOST_OUTPUT_MESSAGE_TRACING;
            }
            else XPOST_MAIN_IF_OPT("-o", "--output=", output_file)
            else XPOST_MAIN_IF_OPT("-d", "--device=", device)
            else XPOST_MAIN_IF_OPT("-g", "--geometry=", geometry)
            else XPOST_MAIN_IF_OPT("-s", "--spill=", spill)
            else XPOST_MAIN_IF_OPT("-b", "--band-bytes=", band_bytes)
            else
            {
                printf("unknown option\n");
                _xpost_main_usage(filename);
                goto quit_xpost;
            }
        }
        else
        {
            ps_file = argv[i];
        }
    }

    /* parse geometry if any */
    if (output_msg != XPOST_OUTPUT_MESSAGE_QUIET)
    {
        printf("geom 1 : %s\n", geometry);
    }
    /* the parse answers whether it understood the geometry, so a
       geometry that was given and not understood is the error; one that
       was not given at all leaves the default page size standing */
    if (geometry)
    {
        have_geometry = _xpost_geometry_parse(geometry,
                                              &width, &height,
                                              &xoffset, &xsign,
                                              &yoffset, &ysign);
        if (!have_geometry)
        {
            XPOST_LOG_ERR("bad formatted geometry");
            goto quit_xpost;
        }
    }
    if (output_msg != XPOST_OUTPUT_MESSAGE_QUIET)
    {
        printf("geom 2 : %dx%d%c%d%c%d\n",
               width, height,
               (xsign == 1) ? '+' : '-', xoffset,
               (ysign == 1) ? '+' : '-', yoffset);
    }

    {
        char *devstr = strdup(device);
        char *subdevice;
        if (!devstr)
        {
            XPOST_LOG_ERR("out of memory");
            goto quit_xpost;
        }
        if ((subdevice=strchr(devstr,':')))
            *subdevice++='\0';
        /* check devices */
        have_device = 0;
        i = 0;
        while (_xpost_main_devices[i])
        {
            if (strcmp(_xpost_main_devices[i], devstr) == 0)
            {
                have_device = 1;
                break;
            }
            i++;
        }
        free(devstr);
    }

    if (!have_device)
    {
        XPOST_LOG_ERR("wrong device.");
        _xpost_main_usage(filename);
        goto quit_xpost;
    }

    /* An image of virtual memory carries the language it was written
       with, and it is read as the context is created. A run that means
       to load no graphics wants another language, so it says before the
       context exists that it will build one. */
    if (no_graphics)
        xpost_vm_image_refuse();

    /* Where a retained page's marks are held, which the context reads as
       it is made. A word that is none of the three is refused naming
       what was given, the way an unrecognised device mode is: nothing
       further down reads a state it does not recognise, so one passed on
       would be taken for the default and the run would quietly do
       something else. */
    if (spill && !xpost_record_spill_set(spill))
    {
        XPOST_LOG_ERR("there is no way \"%s\" of holding a retained page's"
                      " marks; the ways there are: auto, never, always",
                      spill);
        goto quit_xpost;
    }

    /* What one band of a page may cost, which the context reads as it is
       made. Refused naming what was given and the range it takes, for
       the reason the state above is: a budget nothing recognises would
       be dropped and the run would band to a number the caller never
       chose. What the budget will not buy a single row of is refused
       further on, where a device says what a row of it costs. */
    if (band_bytes)
    {
        char *end;
        long budget;

        errno = 0;
        budget = strtol(band_bytes, &end, 10);
        if (errno || end == band_bytes || *end
            || !xpost_band_bytes_set(budget))
        {
            XPOST_LOG_ERR("\"%s\" is no budget for a band of a page; what one"
                          " may cost is a whole number of bytes from 1 to %ld",
                          band_bytes, XPOST_BAND_BYTES_MAX);
            goto quit_xpost;
        }
    }

    if (!(ctx = xpost_create(device,
                             XPOST_OUTPUT_FILENAME,
                             output_file,
                             XPOST_SHOWPAGE_DEFAULT,
                             output_msg,
                             have_geometry ? XPOST_USE_SIZE : XPOST_IGNORE_SIZE,
                             width, height)))
    {
        XPOST_LOG_ERR("Failed to initialize.");
        goto quit_xpost;
    }

    if (no_graphics)
        xpost_skip_graphics_set(ctx, 1);

    /* Naming the file the output goes to says what this invocation is:
       something waiting for that file, not somebody at a keyboard. So the
       run ends where the named program ends, which is where a job ends
       (PLRM 3.7.7), and the interactive executive is never offered after
       it -- an executive would read standard input and execute it, which
       is a second program nobody asked to run. A program that does want a
       session after itself asks for one the way the language provides,
       with the executive operator. */
    if (output_file)
        xpost_batch_set(ctx, 1);

    XPOST_LOG_INFO("defs=%p", (void*)defs);
    if (defs){
        /* the program is about to run against these definitions; one that
           could not be stored is a name the program will find undefined,
           and the report belongs here rather than wherever it is first
           looked up */
        if (!xpost_add_definitions(ctx, num_defs, defs))
            fprintf(stderr, "%s: cannot record the -D definitions\n", filename);
        for (i = 0; i < num_defs; ++i)
        {
            free(defs[i]);
        }
        free(defs);
        defs = NULL;
        num_defs = 0;
    }

    /* seed the resource search path from -I directories */
    if (num_incs > 0)
    {
        for (i = 0; i < num_incs; i++)
        {
            /* a directory that did not reach the search path is one no
               resource will ever be found under, and the run's only
               symptom is the lookup that comes up empty much later */
            if (!xpost_add_resource_dir(ctx, incs[i]))
                fprintf(stderr, "%s: cannot add resource directory %s\n",
                        filename, incs[i]);
            /* resource files are read from beneath this directory */
            xpost_path_permit_read(incs[i]);
            free(incs[i]);
        }
        free(incs);
        incs = NULL;
        num_incs = 0;
    }

    /* confine the program to its working area unless --no-sandbox: the
       current and temporary directories, the input file's directory
       (read) and the output file's directory (write). The interpreter
       permits its own data directory (init.ps, graphics.ps) during
       start-up; -I resource directories were read-permitted above. */
    if (!no_sandbox)
    {
        const char *tmp = getenv("TMPDIR");

        if (!tmp || !*tmp)
            tmp = "/tmp";
        xpost_path_permit_read(".");
        xpost_path_permit_write(".");
        xpost_path_permit_read(tmp);
        xpost_path_permit_write(tmp);
        _xpost_permit_file_dir(ps_file, 0);
        if (output_file)
            _xpost_permit_file_dir(output_file, 1);
        xpost_path_control_engage();
    }

    {
        Xpost_Run_Status status;

        status = xpost_run(ctx, XPOST_INPUT_FILENAME, ps_file, 0);
        xpost_destroy(ctx);

        xpost_quit();

        /* a job that ended in an uncaught error is a failed job,
           whatever was flushed or rendered along the way */
        return status == XPOST_RUN_COMPLETE || status == XPOST_RUN_YIELDED
             ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  quit_xpost:
    xpost_quit();

    return EXIT_FAILURE;
}
