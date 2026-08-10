#!/bin/sh
#
# Guard: the file a page is written to is named and opened when the page
# is written, and nowhere else.
#
# An output name may carry a %d, which stands for the number of the page
# being written. So the name is not a property of the device: it is a
# property of the page, and it is settled once per page, by the page
# machinery, in .transmitpage (data/device.ps). Every device's Emit then
# writes to what that settled and stored on the device as /.outputfile.
#
# The hazard is a device that reads the output name when it is made
# rather than when a page is written. Such a device opens one file at
# creation and holds it for its whole lifetime, so a job's second page
# overwrites its first; a device that builds an image writer there writes
# into a stream that holds exactly one image, so a second page cannot be
# appended even in principle; and the name it opens is the template, %d
# and all. The devices written in PostScript reach the name the one way
# above, so a guard that held only them would hold half the fleet.
#
# What is held, therefore:
#
#   The substitution has one implementation. .pagefilename is fetched
#   from exactly one place in the interpreter's PostScript, inside
#   .transmitpage, and .transmitpage is defined once.
#
#   Every page goes through it. A device's Emit is reached from
#   .transmitpage and from nowhere else, so a device cannot be given a
#   page without being given the name to write it to.
#
#   No compiled device reads the output name for itself. The key
#   "OutputFileName" is the template, and a device that names it is
#   resolving what the page already resolved.
#
#   No compiled device keeps a stream between pages. A FILE * in a
#   device's private struct is a file that outlives the page that opened
#   it, which is what made the second page overwrite the first.
#
#   A compiled device opens and closes a page's file through the one
#   pair, xpost_device_page_open/xpost_device_page_close, and calls them
#   only from the function its method table registers for the Emit slot.
#
# The tests are outside this: a test that drives a device's methods
# one at a time is exercising the device and not transmitting a page, and
# supplies the settled name itself.
#
# Usage: check-page-output.sh <source tree root>

set -eu

src=${1:?usage: check-page-output.sh <source tree root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

guard_workdir
trap 'rm -rf "$work"' EXIT
# read a tree whose lines end where the scans below expect them to, and
# whose paths carry no colon: every record here is read as path, line and
# text split on colons
guard_mirror_tree "$src"
src=$mirror

libdir="$src/src/lib"
datadir="$src/data"
guard_require_dir "$libdir" "the library source directory"
guard_require_dir "$datadir" "the interpreter's PostScript"
guard_require_file "$datadir/device.ps" "the page machinery"
guard_require_file "$libdir/xpost_dev_generic.c" "the shared device helpers"

fail=0

# The extent of one named body, in lines: from the line that opens it to
# the line whose brace closes it. Found rather than assumed -- a rename
# would otherwise put every use outside a range that no longer exists,
# which reads as a clean tree.
#   $1 the code file (path:line:text), $2 the file to look in,
#   $3 an ERE matching the line that opens the body
extent() {
    awk -F: -v f="$2" -v pat="$3" '
        $1 != f { next }
        {
            line = $0
            sub(/^[^:]*:[0-9]+:/, "", line)
            if (!started && line ~ pat) { started = 1; first = $2 }
            if (!started) next
            depth += gsub(/\{/, "&", line) - gsub(/\}/, "&", line)
            if (depth > 0) seen = 1
            if (seen && depth == 0) { print first " " $2; exit }
        }' "$1"
}

# ---------------------------------------------------------------- the
# PostScript: one substitution, and one way to reach an Emit
# ---------------------------------------------------------------------
#
# Read as code: strings are emptied, because an output name holds a
# per-cent and a comment marker inside one would end the line early, and
# comments are dropped, because a brace or a name written in prose is
# neither.
awk '{
    sub(/\r$/, "")
    line = $0; out = ""; i = 1; n = length(line); d = 0
    while (i <= n) {
        c = substr(line, i, 1)
        if (d > 0) {
            if (c == "\\") { i += 2; continue }
            if (c == "(") d++
            else if (c == ")") { d--; if (d == 0) out = out "()" }
            i++; continue
        }
        if (c == "%") break
        if (c == "(") { d = 1; i++; continue }
        out = out c; i++
    }
    print FILENAME ":" FNR ":" out
}' "$datadir"/*.ps > "$work/ps"
if [ ! -s "$work/ps" ]; then
    echo "check-page-output: no PostScript read under $src/data" >&2
    exit 1
fi

read -r tstart tend <<EOF
$(extent "$work/ps" "$datadir/device.ps" '/\\.transmitpage[ \t]*\\{')
EOF
if [ -z "${tstart:-}" ]; then
    echo "check-page-output: .transmitpage was not found in data/device.ps." >&2
    echo "The one place a page's output name is settled has been renamed or" >&2
    echo "removed, and this check would report a tree with no page machinery" >&2
    echo "in it as a tree with one." >&2
    exit 1
fi

# defined once, and in the page machinery
ndef=$(awk -F: '{ line = $0; sub(/^[^:]*:[0-9]+:/, "", line)
                  if (line ~ /\/\.transmitpage[ \t]*\{/) n++ }
                END { print n + 0 }' "$work/ps")
if [ "$ndef" -ne 1 ]; then
    echo "check-page-output: .transmitpage is written $ndef times." >&2
    echo "It is one procedure: a second is a second answer to what a page's" >&2
    echo "output is called." >&2
    fail=1
fi

# the substitution is fetched once, inside it
awk -F: -v f="$datadir/device.ps" -v a="$tstart" -v b="$tend" '
    {
        line = $0
        sub(/^[^:]*:[0-9]+:/, "", line)
        if (line !~ /\/\.pagefilename[ \t]+get/) next
        if ($1 == f && $2 >= a && $2 <= b) next
        print $1 ":" $2
    }' "$work/ps" > "$work/pf-outside"
npf=$(awk -F: -v f="$datadir/device.ps" -v a="$tstart" -v b="$tend" '
    {
        line = $0
        sub(/^[^:]*:[0-9]+:/, "", line)
        if ($1 == f && $2 >= a && $2 <= b && line ~ /\/\.pagefilename[ \t]+get/) n++
    }
    END { print n + 0 }' "$work/ps")
if [ -s "$work/pf-outside" ]; then
    echo "check-page-output: the page number is substituted outside .transmitpage:" >&2
    sed "s|^$src/||; s|^|  |" "$work/pf-outside" >&2
    echo "A device is handed the settled name in /.outputfile; it does not" >&2
    echo "settle one of its own." >&2
    fail=1
fi
if [ "$npf" -ne 1 ]; then
    echo "check-page-output: .transmitpage fetches .pagefilename $npf times," >&2
    echo "and settling a page's name is one call." >&2
    fail=1
fi

# and every Emit is reached from there
awk -F: -v f="$datadir/device.ps" -v a="$tstart" -v b="$tend" '
    {
        line = $0
        sub(/^[^:]*:[0-9]+:/, "", line)
        if (line !~ /\/Emit[ \t]+get/) next
        if ($1 == f && $2 >= a && $2 <= b) { n++; next }
        print $1 ":" $2
    }
    END { if (n != 1) print "COUNT " n + 0 }' "$work/ps" > "$work/emit"
if grep -q '^COUNT' "$work/emit"; then
    echo "check-page-output: .transmitpage reaches an Emit $(awk '$1=="COUNT"{print $2}' "$work/emit") times, and it runs one." >&2
    fail=1
fi
if grep -v '^COUNT' "$work/emit" | grep -q .; then
    echo "check-page-output: a device's Emit is run outside .transmitpage:" >&2
    grep -v '^COUNT' "$work/emit" | sed "s|^$src/||; s|^|  |" >&2
    echo "A page is transmitted through .transmitpage (data/device.ps), which" >&2
    echo "settles the name the page is written to first." >&2
    fail=1
fi

# ---------------------------------------------------------------- the
# compiled devices: no template, no kept stream, one opener
# ---------------------------------------------------------------------
set -- "$libdir"/xpost_dev_*.c
guard_c_source "$@" > "$work/code"
if [ ! -s "$work/code" ]; then
    echo "check-page-output: no device sources read under $src/src/lib" >&2
    exit 1
fi

# 1. the template is the page machinery's to read
awk 'FNR == 1 { file = FILENAME }
     { sub(/\r$/, "")
       if ($0 ~ /"OutputFileName"/) print file ":" FNR }' \
    "$libdir"/xpost_dev_*.c > "$work/template"
if [ -s "$work/template" ]; then
    echo "check-page-output: a compiled device reads the output name template:" >&2
    sed "s|^$src/||; s|^|  |" "$work/template" >&2
    echo "The template may carry a %d and the page number that replaces it is" >&2
    echo "the page's to know. Read /.outputfile, which the page machinery has" >&2
    echo "already settled, through xpost_device_page_open()." >&2
    fail=1
fi

# 2. a stream in the private struct outlives the page that opened it
for f in "$libdir"/xpost_dev_*.c; do
    hits=$(awk -v F="$f" '
        /^typedef struct/ { n = 0; delete buf; inb = 1; next }
        inb && /^\}[ \t]*PrivateData[ \t]*;/ {
            for (i = 1; i <= n; i++)
                if (buf[i] ~ /(^|[^A-Za-z0-9_])FILE([^A-Za-z0-9_]|$)/)
                    printf "%s:%d\n", F, lno[i]
            inb = 0; next
        }
        inb && /^\}/ { inb = 0; next }
        inb { buf[++n] = $0; lno[n] = FNR }
    ' "$f")
    if [ -n "$hits" ]; then
        echo "check-page-output: a device keeps a stream in its private struct:" >&2
        printf '%s\n' "$hits" | sed "s|^$src/||; s|^|  |" >&2
        echo "A page's file is opened and closed within the Emit that writes the" >&2
        echo "page; one held between pages is one the next page writes over." >&2
        fail=1
    fi
done

# 3. the one opener and the one closer
read -r ostart oend <<EOF
$(extent "$work/code" "$libdir/xpost_dev_generic.c" '(^|[^A-Za-z0-9_])xpost_device_page_open[ \t]*\\(')
EOF
read -r cstart cend <<EOF
$(extent "$work/code" "$libdir/xpost_dev_generic.c" '(^|[^A-Za-z0-9_])xpost_device_page_close[ \t]*\\(')
EOF
if [ -z "${ostart:-}" ] || [ -z "${cstart:-}" ]; then
    echo "check-page-output: xpost_device_page_open()/xpost_device_page_close()" >&2
    echo "were not both found in src/lib/xpost_dev_generic.c -- the pair a page's" >&2
    echo "file is opened and closed through has been renamed, and this check" >&2
    echo "would report a tree with no opener in it as a tree with one." >&2
    exit 1
fi

awk -F: -v g="$libdir/xpost_dev_generic.c" \
        -v oa="$ostart" -v ob="$oend" -v ca="$cstart" -v cb="$cend" '
    {
        line = $0
        sub(/^[^:]*:[0-9]+:/, "", line)
        if (line !~ /(^|[^A-Za-z0-9_])(xpost_diskfile_fopen|xpost_diskfile_fopen_beneath|fclose)[ \t]*\(/) next
        if ($1 == g && (($2 >= oa && $2 <= ob) || ($2 >= ca && $2 <= cb))) next
        print $1 ":" $2
    }' "$work/code" > "$work/opens"
if [ -s "$work/opens" ]; then
    echo "check-page-output: a device opens or closes a file of its own:" >&2
    sed "s|^$src/||; s|^|  |" "$work/opens" >&2
    echo "A page's file is opened by xpost_device_page_open() and closed by" >&2
    echo "xpost_device_page_close(); those two hold the decision." >&2
    fail=1
fi

# 4. and the pair is called from the Emit slot, in the file's own table
callers=0
for f in "$libdir"/xpost_dev_*.c; do
    [ "$f" = "$libdir/xpost_dev_generic.c" ] && continue
    uses=$(awk -F: -v F="$f" '
        $1 != F { next }
        {
            line = $0
            sub(/^[^:]*:[0-9]+:/, "", line)
            if (line ~ /(^|[^A-Za-z0-9_])xpost_device_page_(open|close)[ \t]*\(/)
                print $2
        }' "$work/code")
    [ -n "$uses" ] || continue
    callers=$((callers + 1))

    emitfn=$(awk '{ sub(/\r$/, "")
                    if ($0 ~ /"Emit"/ && $0 ~ /\(Xpost_Op_Func\)/) {
                        match($0, /\(Xpost_Op_Func\)[ \t]*[A-Za-z_][A-Za-z0-9_]*/)
                        s = substr($0, RSTART, RLENGTH)
                        sub(/\(Xpost_Op_Func\)[ \t]*/, "", s)
                        print s; exit
                    } }' "$f")
    if [ -z "$emitfn" ]; then
        echo "check-page-output: ${f#"$src"/} opens a page's file and its method" >&2
        echo "table names no Emit; there is nothing to hold the open to." >&2
        fail=1
        continue
    fi
    read -r estart eend <<EOF
$(extent "$work/code" "$f" "(^|[^A-Za-z0-9_])$emitfn[ \t]*\\\\(")
EOF
    if [ -z "${estart:-}" ]; then
        echo "check-page-output: $emitfn(), the Emit of ${f#"$src"/}, could not be" >&2
        echo "read; the opens below would be held to nothing." >&2
        fail=1
        continue
    fi
    for l in $uses; do
        if [ "$l" -lt "$estart" ] || [ "$l" -gt "$eend" ]; then
            echo "check-page-output: ${f#"$src"/}:$l opens a page's file outside" >&2
            echo "$emitfn(), the method that writes a page (lines $estart-$eend)." >&2
            echo "The name is settled per page, so the file is opened per page." >&2
            fail=1
        fi
    done
done

# A scan that found no caller at all found nothing, and a rename that
# made every rule above inert would leave exactly that.
if [ "$callers" -lt 2 ]; then
    echo "check-page-output: $callers compiled devices write a page through the" >&2
    echo "shared opener, and the fleet has more than one that writes a file." >&2
    echo "The scan is reading the wrong thing." >&2
    fail=1
fi

[ "$fail" = 0 ] || exit 1
echo "check-page-output: ok (one substitution at data/device.ps:$tstart-$tend, $callers compiled devices opening per page)"
