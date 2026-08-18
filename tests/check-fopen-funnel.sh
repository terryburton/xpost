#!/bin/sh
#
# Guard the single-opener invariant: every disk file the interpreter
# opens passes one enforcement point, so file-access policy has one place
# to be enforced from.
#
# That point is xpost_raw_fopen() in src/lib/xpost_file.c, which is what
# xpost_diskfile_fopen() and the sandbox's confined opener reach through.
# The rule was written down as "fopen appears only inside
# xpost_diskfile_fopen()", which named the wrong function and was checked
# against the wrong thing: the scan was file-scoped, so any call anywhere
# in xpost_file.c satisfied it, and the whole of src/bin was outside the
# directory it was pointed at -- where the sample client had been opening
# its output file directly all along.
#
# The rule is on the identifier, not on the call. A macro aliasing fopen,
# a function pointer taking its address, and a call written with a space
# before the parenthesis are all ways of opening a file that a scan for
# "fopen(" does not see, and each of them has to write the name down
# somewhere. So: outside the funnel, the name may not appear at all. The
# other spellings of the same library call -- fopen64, freopen, _wfopen,
# fdopen and the rest -- are refused wherever they appear, funnel
# included, since the funnel does not use them.
#
# Comments and string literals are not code: the sources talk about
# fopen in both, which is why the scan reads them out first.
#
# Usage: check-fopen-funnel.sh <source tree root>

set -eu

src=${1:?usage: check-fopen-funnel.sh <source tree root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_dir "$src/src/lib" "the library source directory"
guard_require_dir "$src/src/bin" "the program source directory"
guard_require_file "$src/src/lib/xpost_file.c" "the file layer"

guard_workdir
# Read a tree whose lines end where the scans below expect them to.
guard_mirror_tree "$src"
src=$mirror
funnel_file="$src/src/lib/xpost_file.c"
fail=0

# every C file the project builds, read as code
set -- "$src"/src/lib/*.c "$src"/src/lib/*.h "$src"/src/bin/*.c "$src"/src/bin/*.h
guard_c_source "$@" > "$work/code"
if [ ! -s "$work/code" ]; then
    echo "check-fopen-funnel: no C sources read under $src/src" >&2
    exit 1
fi

# ---- the funnel's own extent, in lines ----
#
# Found rather than assumed: a rename of the funnel would otherwise leave
# every call outside a range that no longer exists, which reads as a clean
# tree.
awk -F'\t' -v f="$funnel_file" '
    $1 != f { next }
    {
        line = substr($0, length($1) + length($2) + 3)
        if (!started && line ~ /(^|[^A-Za-z0-9_])xpost_raw_fopen[ \t]*\(/) {
            started = 1; first = $2
        }
        if (!started) next
        depth += gsub(/{/, "&", line) - gsub(/}/, "&", line)
        if (depth > 0) seen = 1
        if (seen && depth == 0) { print first, $2; exit }
    }' "$work/code" > "$work/extent"

read -r fstart fend < "$work/extent" || fstart=
if [ -z "${fstart:-}" ]; then
    echo "check-fopen-funnel: xpost_raw_fopen() was not found in" >&2
    echo "src/lib/xpost_file.c -- the funnel was renamed and this check would" >&2
    echo "have reported a tree with no opener in it as a tree with one." >&2
    exit 1
fi

# ---- the bare name, wherever it is written ----
awk -F'\t' -v f="$funnel_file" -v a="$fstart" -v b="$fend" '
    {
        line = substr($0, length($1) + length($2) + 3)
        n = gsub(/(^|[^A-Za-z0-9_])fopen([^A-Za-z0-9_]|$)/, "&", line)
        if (n == 0) next
        if ($1 == f && $2 >= a && $2 <= b) { inside += n; next }
        print $1 ":" $2 ":" n
    }' "$work/code" > "$work/outside"
inside=$(awk -F'\t' -v f="$funnel_file" -v a="$fstart" -v b="$fend" '
    {
        line = substr($0, length($1) + length($2) + 3)
        if ($1 == f && $2 >= a && $2 <= b)
            n += gsub(/(^|[^A-Za-z0-9_])fopen([^A-Za-z0-9_]|$)/, "&", line)
    }
    END { print n + 0 }' "$work/code")

if [ -s "$work/outside" ]; then
    echo "check-fopen-funnel: fopen named outside xpost_raw_fopen():" >&2
    sed "s|^$mirror/||; s|^|  |" "$work/outside" >&2
    echo "Route disk opens through xpost_diskfile_fopen()." >&2
    fail=1
fi

# and inside it, exactly once: a second call there is a second policy
# decision in the one place that is supposed to hold only one
if [ "$inside" -ne 1 ]; then
    echo "check-fopen-funnel: xpost_raw_fopen() names fopen $inside times," >&2
    echo "and the funnel is one call." >&2
    fail=1
fi

# ---- the other spellings, refused everywhere ----
#
# Each of these opens a path, which is the decision the funnel exists to
# make. fdopen and its kin are deliberately not here: they take a
# descriptor, so the path decision was already made -- by the sandbox's
# openat2 or by mkstemp -- and wrapping it in a stream adds nothing to
# decide.
others='fopen64 fopen_s freopen freopen64 freopen_s
        _fopen _wfopen _wfopen_s _wfreopen _wfreopen_s _fsopen _wfsopen'
pat=$(printf '%s' "$others" | tr -s ' \n' '|')
pat=${pat%|}
if awk -F'\t' -v pat="$pat" '
    {
        line = substr($0, length($1) + length($2) + 3)
        if (line ~ "(^|[^A-Za-z0-9_])(" pat ")([^A-Za-z0-9_]|$)")
            print $1 ":" $2
    }' "$work/code" > "$work/others"; then :; fi
if [ -s "$work/others" ]; then
    echo "check-fopen-funnel: another spelling of the same open:" >&2
    sed "s|^$mirror/||; s|^|  |" "$work/others" >&2
    echo "Route disk opens through xpost_diskfile_fopen()." >&2
    fail=1
fi

# ---- any other name that ends in fopen ----
#
# The list above is what is known today; a name nobody thought of still
# has to end in fopen to be one, so an identifier that does and does not
# belong to the funnel is refused as well.
# One line: the value reaches awk through -v, and an awk that takes the
# assignment as a string literal will not have a newline inside one.
known='xpost_diskfile_fopen xpost_diskfile_fopen_beneath xpost_raw_fopen xpost_confined_fopen'
awk -F'\t' -v known="$known" '
    BEGIN { split(known, k, /[ \n]+/); for (i in k) if (k[i] != "") ok[k[i]] = 1 }
    {
        line = substr($0, length($1) + length($2) + 3)
        while (match(line, /[A-Za-z_][A-Za-z0-9_]*fopen([^A-Za-z0-9_]|$)/)) {
            id = substr(line, RSTART, RLENGTH)
            line = substr(line, RSTART + RLENGTH)
            sub(/[^A-Za-z0-9_]$/, "", id)
            if (!(id in ok))
                print $1 ":" $2 ": " id
        }
    }' "$work/code" > "$work/unknown"
if [ -s "$work/unknown" ]; then
    echo "check-fopen-funnel: an opener this check does not know:" >&2
    sed "s|^$mirror/||; s|^|  |" "$work/unknown" >&2
    echo "Route disk opens through xpost_diskfile_fopen(), or declare the" >&2
    echo "name here if it belongs to the funnel." >&2
    fail=1
fi

[ "$fail" = 0 ] || exit 1
echo "check-fopen-funnel: ok (one opener, in xpost_raw_fopen at lines $fstart-$fend)"
