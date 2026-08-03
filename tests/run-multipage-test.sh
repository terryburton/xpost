#!/bin/sh
# Meson test wrapper: render a MULTI-PAGE job through every file device and
# require the right multi-page shape.
#
# The job wraps each page in save...showpage...restore -- the separation-plate
# idiom -- which rewinds local virtual memory between pages. State a device
# keeps per page (a page counter, an open output file) must not be rewound with
# it, or later pages collide with the first.
#
# Two shapes, by device:
#
#  * Paginated container formats (PDF, DSC) default to ONE file holding every
#    page; a %d in the output name selects a file per page instead. So a plain
#    multi-showpage job to a fixed name yields a single N-page document, and the
#    same job to a %d name yields N one-page files.
#
#  * The raster devices and SVG cannot hold more than one page in a file, so a
#    %d gives a file per page and a fixed name keeps the last page (every page
#    rewrites the one file). This is also what a single-page consumer -- which
#    reads the file right after showpage -- depends on.
#
# The %d name is the only page-specific step in either shape; the open still
# goes through the ordinary `file` operator, so a device under the file-access
# sandbox is bound by the same rules a single-page write is.
#
# png and jpeg hold one image per file and open at device creation, so they do
# not page; they are covered by the `devices` test.
#
#   $1  path to the built xpost binary
set -u
xpost=$1

# Reach the interpreter's data directory outside any sandbox root: disable the
# file-access sandbox when this build has one (detected from the usage text),
# so the test is valid at every point in the series.
if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
prog="$work/pages.ps"
# three pages, each a stroke at its own position, so the pages differ in content
printf '%s\n' \
    'save 10 10 moveto 40 40 lineto stroke showpage restore' \
    'save 40 40 moveto 70 70 lineto stroke showpage restore' \
    'save 70 10 moveto 95 35 lineto stroke showpage restore' > "$prog"

# device:extension:paginated(1=one file holds all pages, 0=one page per file)
devices='pgm:pgm:0 ppm:ppm:0 pbm:pbm:0 tiff:tiff:0 svgwrite:svg:0 pdfwrite:pdf:1 dscwrite:ps:1'

fail=0

render() {   # $1=device $2=output-path ; returns 1 on skip/error
    err=$("$xpost" -q $ns -d "$1" -o "$2" "$prog" </dev/null 2>&1)
    status=$?
    if [ "$status" -ne 0 ]; then
        echo "FAILURES: the interpreter exited with status $status"
        exit 1
    fi
    case "$err" in
        *"wrong device"*) return 1 ;;
    esac
    if printf '%s' "$err" | grep -q '%%\[ Error'; then
        echo "FAIL $1: $(printf '%s' "$err" | grep '%%\[ Error' | head -1)"
        fail=1
        return 1
    fi
    return 0
}

for de in $devices; do
    dev=${de%%:*}
    rest=${de#*:}
    ext=${rest%%:*}
    pag=${rest#*:}
    rm -f "$work"/page_* "$work"/fixed.* 2>/dev/null

    # every device: a %d gives one file per page, all three distinct
    if ! render "$dev" "$work/page_%d.$ext"; then
        [ "$fail" -eq 0 ] && echo "SKIP $dev (not built in)"
        continue
    fi
    p1="$work/page_1.$ext"; p2="$work/page_2.$ext"; p3="$work/page_3.$ext"
    n=$(ls "$work"/page_*."$ext" 2>/dev/null | wc -l)
    if [ "$n" -ne 3 ]; then
        echo "FAIL $dev: %d gave $n file(s), want 3"; fail=1; continue
    fi
    for p in "$p1" "$p2" "$p3"; do
        [ -s "$p" ] || { echo "FAIL $dev: $(basename "$p") missing or empty"; fail=1; }
    done
    [ "$fail" -ne 0 ] && continue
    if cmp -s "$p1" "$p2" || cmp -s "$p2" "$p3" || cmp -s "$p1" "$p3"; then
        echo "FAIL $dev: %d page files are not all distinct (counter rewound?)"; fail=1; continue
    fi

    # fixed name (no %d)
    if ! render "$dev" "$work/fixed.$ext"; then
        echo "FAIL $dev: fixed-name render failed"; fail=1; continue
    fi
    if [ "$pag" = 1 ]; then
        # one file holding all three pages
        case "$dev" in
        pdfwrite)
            c=$(grep -aoE '/Count [0-9]+' "$work/fixed.$ext" | awk '{print $2}')
            [ "$c" = 3 ] || { echo "FAIL $dev: page tree /Count $c, want 3"; fail=1; continue; }
            np=$(grep -ac '/Type /Page[^s]' "$work/fixed.$ext")
            [ "$np" = 3 ] || { echo "FAIL $dev: $np page objects, want 3"; fail=1; continue; }
            ;;
        dscwrite)
            np=$(grep -ac '^%%Page:' "$work/fixed.$ext")
            [ "$np" = 3 ] || { echo "FAIL $dev: $np %%Page sections, want 3"; fail=1; continue; }
            grep -aq '^%%Pages: 3' "$work/fixed.$ext" || { echo "FAIL $dev: no %%Pages: 3 trailer"; fail=1; continue; }
            ;;
        esac
        # an independent consumer must agree the document has three pages
        if command -v gs >/dev/null 2>&1; then
            pages=$(gs -q -dNODISPLAY -dPDFINFO -dBATCH -dNOPAUSE "$work/fixed.$ext" </dev/null 2>&1 \
                    | grep -aoiE 'has [0-9]+ page' | grep -aoE '[0-9]+')
            # gs -dPDFINFO reads PDF; for DSC, run it through and count showpages
            case "$dev" in
            pdfwrite) [ "$pages" = 3 ] || { echo "FAIL $dev: gs reads $pages pages, want 3"; fail=1; continue; } ;;
            esac
        fi
        echo "OK   $dev (one file, three pages; %d gives three files)"
    else
        # last page stands in the one file
        cmp -s "$work/fixed.$ext" "$p3" || { echo "FAIL $dev: fixed-name output is not the last page"; fail=1; continue; }
        echo "OK   $dev (one page per file via %d; fixed name = last page)"
    fi
done

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: multi-page output regressed"
    exit 1
fi
echo "SUCCESS"
exit 0
