#!/bin/sh
# Guard the set of functions whose refusal a caller may not ignore.
#
# XPOST_MUST_CHECK is what makes "a function that can refuse returns that
# fact, and no caller may ignore it" a build error rather than a
# convention. A convention erodes: drop the mark from one declaration in
# passing and every call site that was answering the refusal goes on
# compiling, silently back where it started. The ratchet here is that the
# set may grow and may not shrink.
#
# The second list is the escape hatch. XPOST_REFUSAL_IMPOSSIBLE consumes
# an answer at a site where the refusal cannot arise; each use is a claim
# about that site, and the count is recorded so that adding one is a
# deliberate act rather than the easiest way past a build error.
#
# Usage: check-must-check.sh <source tree root>

set -eu

src=${1:?usage: check-must-check.sh <source tree root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
golden="$src/tests/must_check.golden"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

fail=0

# The decorated names, wherever they are declared: a header for the API,
# the defining file for a module-private helper. The return type may sit
# on its own line, so the declaration is gathered up to its first '('.
cat "$src"/src/lib/*.h "$src"/src/lib/*.c | awk '
    # a mention in a comment or a preprocessor line is not a declaration
    /XPOST_MUST_CHECK/ && !/^[ \t]*[#*]/ && !/\/\*.*XPOST_MUST_CHECK/ {
        decl = $0
        sub(/.*XPOST_MUST_CHECK/, "", decl)
        # what stands between the mark and the parameter list: a comment
        # closing there means the mark was being talked about, not applied
        head = decl
        sub(/\(.*/, "", head)
        if (head ~ /\*\//)
            next
        while (index(decl, "(") == 0 && (getline nxt) > 0)
            decl = decl " " nxt
        sub(/\(.*/, "", decl)
        n = split(decl, w, /[^A-Za-z0-9_]+/)
        if (n > 0 && w[n] != "")
            print w[n]
    }' | sort -u > "$tmp/current"

grep -vE '^[[:space:]]*(#|$)' "$golden" \
  | grep -v '^refusal-impossible ' | sort -u > "$tmp/recorded"

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

# The escape hatch, counted. The definition in xpost_private.h is one of
# the matches and is not a use.
uses=$(cat "$src"/src/lib/*.h "$src"/src/lib/*.c \
       | grep -c 'XPOST_REFUSAL_IMPOSSIBLE(' || true)
uses=$((uses - 1))
allowed=$(awk '/^refusal-impossible /{ print $2 }' "$golden")

if [ "$uses" -ne "$allowed" ]; then
    echo "check-must-check: $uses uses of XPOST_REFUSAL_IMPOSSIBLE, $allowed recorded." >&2
    echo "Each one claims that a refusal cannot arise at that site. Say why" >&2
    echo "there, and record the count here." >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "check-must-check: ok (refusals answered; $uses site(s) claim they cannot arise)"
