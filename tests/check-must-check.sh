#!/bin/sh
# Guard the set of functions whose refusal a caller may not ignore.
#
# XPOST_MUST_CHECK is what makes "a function that can refuse returns that
# fact, and no caller may ignore it" a build error rather than a
# convention. A convention erodes: drop the mark from one declaration in
# passing and every call site that was answering the refusal goes on
# compiling, silently back where it started.
#
# What the register holds is the set exactly: a name recorded here whose
# mark has gone fails, and a newly marked function that is not recorded
# fails too. Retiring an entry takes two edits in this file -- the name
# and the count above it -- so a set that shrinks says so in the diff
# rather than passing as if nothing had happened.
#
# The mark must also be where the callers are. A mark that survives only
# on the definition in the .c file leaves every other translation unit
# compiling against an undecorated declaration, which is the erosion this
# guards against wearing the guard's own colours. So a registered name
# that a header declares must carry the mark in that header.
#
# The second list is the escape hatch. XPOST_REFUSAL_IMPOSSIBLE consumes
# an answer at a site where the refusal cannot arise; each use is a claim
# about that site, and the count is recorded so that adding one is a
# deliberate act rather than the easiest way past a build error. Uses are
# counted where they are written, not by the lines they are written on:
# two on one line used to count as one, which is a free unrecorded claim.
#
# Usage: check-must-check.sh <source tree root>

set -eu

src=${1:?usage: check-must-check.sh <source tree root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
golden="$src/tests/must_check.golden"
guard_require_file "$golden" "the register of functions that may refuse"
guard_require_dir "$src/src/lib" "the library source directory"

guard_workdir
tmp=$work
trap 'rm -rf "$tmp"' EXIT INT TERM
# the register is read line-wise and its counts word-wise, so read it
# with the line endings taken out
guard_mirror register "$golden"
golden="$mirror/$(basename "$golden")"

fail=0

# The scanner. The source is read as C -- comments and string literals
# out, by guard_c_source -- and preprocessor lines go too, since the
# mark's own #define is one and is not an application of it. What is left
# is split into declarations, and a declaration carrying the mark
# anywhere in it names its function immediately before its parameter
# list. Reading the name as "the word after the mark" only worked while
# every mark was written in front; a mark written after the parameter
# list, which is the other placement the compiler accepts, made the scan
# run on into the next declaration and record the wrong name.
scan() {
    guard_c_source "$@" \
    | sed 's/^[^:]*:[0-9]*://' \
    | grep -vE '^[[:space:]]*#' \
    | tr '\n' ' ' | tr ';{}' '\n\n\n' \
    | awk '
        /XPOST_MUST_CHECK/ {
            p = index($0, "(")
            if (p == 0) next
            head = substr($0, 1, p - 1)
            sub(/[ \t*]+$/, "", head)
            n = split(head, w, /[^A-Za-z0-9_]+/)
            if (n > 0 && w[n] != "" && w[n] != "XPOST_MUST_CHECK")
                print w[n]
        }'
}

scan "$src"/src/lib/*.h "$src"/src/lib/*.c | sort -u > "$tmp/current"
scan "$src"/src/lib/*.h | sort -u > "$tmp/in_headers"

grep -vE '^[[:space:]]*(#|$)' "$golden" | tr -d '\r' \
  | grep -vE '^(refusal-impossible|entries) ' | sort -u > "$tmp/recorded"

if [ ! -s "$tmp/recorded" ]; then
    echo "check-must-check: the register at $golden names no functions." >&2
    exit 1
fi
if [ ! -s "$tmp/current" ]; then
    echo "check-must-check: no XPOST_MUST_CHECK declarations found in" >&2
    echo "$src/src/lib -- the mark was renamed and this check disarmed." >&2
    exit 1
fi

comm -23 "$tmp/recorded" "$tmp/current" > "$tmp/missing"
comm -13 "$tmp/recorded" "$tmp/current" > "$tmp/added"

if [ -s "$tmp/missing" ]; then
    echo "check-must-check: these no longer refuse in a way a caller must answer:" >&2
    sed 's/^/  /' "$tmp/missing" >&2
    echo "The set may grow and may not shrink: restore XPOST_MUST_CHECK, or say" >&2
    echo "in the commit why the function can no longer refuse." >&2
    fail=1
fi

if [ -s "$tmp/added" ]; then
    echo "check-must-check: newly decorated, and not yet recorded:" >&2
    sed 's/^/  /' "$tmp/added" >&2
    echo "Add them to tests/must_check.golden in the same commit." >&2
    fail=1
fi

# The mark has to reach the callers. A registered name a header declares
# must carry it there; carrying it only on the definition protects the
# defining file and nothing else.
while read -r fn; do
    [ -n "$fn" ] || continue
    if grep -lq "[^A-Za-z0-9_]$fn[ 	]*(" "$src"/src/lib/*.h 2>/dev/null; then
        if ! grep -qx "$fn" "$tmp/in_headers"; then
            echo "check-must-check: $fn is declared in a header without the mark." >&2
            echo "Every translation unit but its own then compiles against a" >&2
            echo "declaration that says the refusal may be ignored." >&2
            fail=1
        fi
    fi
done < "$tmp/recorded"

# The recorded size, so that retiring a name is two edits and a visible one.
entries=$(awk '/^entries /{ print $2; found = 1 } END { if (!found) print "" }' "$golden")
have=$(grep -c . "$tmp/recorded")
case $entries in
    ''|*[!0-9]*)
        echo "check-must-check: the register has no 'entries <n>' line." >&2
        echo "Record the size of the set: a removal is then two edits, and the" >&2
        echo "count going down is what says so." >&2
        fail=1 ;;
    *)
        if [ "$entries" -ne "$have" ]; then
            echo "check-must-check: the register records $entries entries and holds $have." >&2
            fail=1
        fi ;;
esac

# The escape hatch, counted where it is written. The definition in
# xpost_private.h is one of the matches and is not a use.
uses=$(grep -o 'XPOST_REFUSAL_IMPOSSIBLE(' "$src"/src/lib/*.h "$src"/src/lib/*.c \
       | grep -c . || true)
uses=$((uses - 1))
allowed=$(awk '/^refusal-impossible /{ print $2; found = 1 } END { if (!found) print "" }' "$golden")

case $allowed in
    ''|*[!0-9]*)
        echo "check-must-check: the register has no 'refusal-impossible <n>' line." >&2
        echo "Without it the escape hatch is uncounted and this check said so" >&2
        echo "about a number nobody had written down." >&2
        fail=1 ;;
    *)
        if [ "$uses" -ne "$allowed" ]; then
            echo "check-must-check: $uses uses of XPOST_REFUSAL_IMPOSSIBLE, $allowed recorded." >&2
            echo "Each one claims that a refusal cannot arise at that site. Say why" >&2
            echo "there, and record the count here." >&2
            fail=1
        fi ;;
esac

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "check-must-check: ok (refusals answered; $uses site(s) claim they cannot arise)"
