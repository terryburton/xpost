#!/bin/sh
# Meson test wrapper: assert that no registered private-namespace member
# has moved home or vanished from the interpreter's PostScript sources,
# and that no graphics-state template slot has been dropped, renamed or
# added without being declared.
#
# tests/dict_homes.golden is the register: "DICT /name" lines must appear
# as an adjacent pair somewhere in data/*.ps (a definition or a frozen
# reference -- both disappear when a member is relocated), and
# "gstate /slot" lines must name the slots of the .gstatetemplate literal
# in data/gstate.ps, all of them. A member may gain company freely; a
# feature that adds one appends it here in the same commit. This keeps
# machinery born in its final home -- a later commit cannot relocate it
# without failing here.
#
# A registered name is found by looking for the pair in the sources, so
# where the name ends matters. The terminator was once "any character
# that is not a letter, digit, dot or equals", which let `_` and `-` end
# a name: the register could then carry /DATA, which nothing defines,
# and be satisfied by /DATA_DIR, which something else does. Four entries
# were fiction on that account. A name ends where PostScript ends one --
# at whitespace or a delimiter -- so that is what is required here.
#
# Where the pair is found matters as much. The sources were read as text,
# so a comment counted: the register carried .xpostsys /h, which nothing
# has ever defined, and was answered by the line of data/init.ps that
# writes `.xpostsys /h { ... } put` while explaining what the helper-call
# idiom looks like. An entry satisfied that way holds nothing to
# anything, and any name a comment happens to spell can be registered
# without a member behind it. So the sources are read as PostScript: a
# `%` inside a string is not a comment and is neutralised first, and what
# follows any other `%` is not part of the program.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-dict-homes.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
golden="$src/tests/dict_homes.golden"
guard_require_file "$golden" "the register of private-namespace homes"
guard_require_file "$src/data/gstate.ps" "the graphics state module"

guard_workdir
trap 'rm -rf "$work"' EXIT
fail=0
cr=$(printf '\r')   # tolerate CRLF line endings (Windows checkouts)

# ---- the sources, as PostScript rather than as text ----
# A `%` inside a string is not a comment, so those are neutralised before
# comments are stripped; otherwise a line mentioning (%stdout) loses its
# tail and a real definition on it goes unseen.
for f in "$src"/data/*.ps; do
    tr -d "$cr" < "$f" | sed 's|(%[^)]*)|(STR)|g; s|%.*||'
done > "$work/code"
if [ ! -s "$work/code" ]; then
    echo "FAILURES: no PostScript found under $src/data; every member would"
    echo "      be reported missing from a tree this check cannot read"
    exit 1
fi

# ---- the graphics state template, and every slot in it ----
#
# The scan below reads one slot per line, so that is required of the
# literal rather than assumed of it: two slots written on one line would
# be a slot the scan cannot see, and therefore one this check does not
# hold to anything.
sed -n '/\.gstatetemplate[[:space:]]*<</,/^[[:space:]]*>>[[:space:]]*def/p' \
    "$src/data/gstate.ps" | tr -d "$cr" > "$work/template"
guard_require_file "$work/template" "the .gstatetemplate literal in data/gstate.ps"
if ! grep -qE '^[[:space:]]*>>[[:space:]]*def' "$work/template"; then
    echo "FAILURES: the .gstatetemplate literal in data/gstate.ps is not closed"
    echo "      by a '>> def' line; the slot scan read to the end of the file"
    exit 1
fi

sed '1d; $d' "$work/template" | sed 's/%.*//' \
    | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' | grep -v '^$' > "$work/body"

# One key and one value per line: the key first, and after it exactly one
# top-level item -- a token, a balanced composite, or a `<n> dict`. A line
# carrying a second key would declare a slot the scan reads straight past.
awk '
    {
        if ($1 !~ /^\/[A-Za-z][A-Za-z0-9]*$/) { print "BAD " $0; next }
        items = 0; depth = 0
        for (i = 2; i <= NF; i++) {
            if (depth == 0) items++
            t = $i
            depth += gsub(/[{[<]/, "&", t) - gsub(/[]}>]/, "&", t)
        }
        if (depth != 0) { print "BAD " $0; next }
        if (items == 1 || (items == 2 && $NF == "dict")) {
            print "SLOT " substr($1, 2)
        } else {
            print "BAD " $0
        }
    }' "$work/body" > "$work/scan"

if grep -q '^BAD ' "$work/scan"; then
    echo "FAILURES: every line of .gstatetemplate must declare one slot, named"
    echo "      first, so that the slots can be read off it:"
    sed -n 's/^BAD /      /p' "$work/scan"
    exit 1
fi
sed -n 's/^SLOT //p' "$work/scan" | sort -u > "$work/slots"
if [ ! -s "$work/slots" ]; then
    echo "FAILURES: no slots found in the .gstatetemplate literal"
    exit 1
fi

awk '$1 == "gstate" { print substr($2, 2) }' "$golden" | tr -d "$cr" \
    | sort -u > "$work/registered"

# a slot nobody declared is a slot nothing holds: it may be renamed at
# will and no test says otherwise
if [ -s "$work/slots" ]; then
    while read -r slot; do
        [ -n "$slot" ] || continue
        if ! grep -qx "$slot" "$work/registered"; then
            echo "UNDECLARED gstate slot: /$slot"
            echo "      add 'gstate /$slot' to tests/dict_homes.golden in this commit"
            fail=1
        fi
    done < "$work/slots"
fi

# ---- the register, line by line ----
lineno=0
while read -r home name extra; do
    lineno=$((lineno + 1))
    home=${home%"$cr"}; name=${name%"$cr"}; extra=${extra%"$cr"}
    case "$home" in
        ''|'#'*) continue ;;
    esac
    # a line that is not a "DICT /name" pair is not a registration, and a
    # missing name would make the search below match everything
    if [ -z "$name" ] || [ -n "$extra" ]; then
        echo "MALFORMED register line $lineno: $home $name $extra"
        fail=1
        continue
    fi
    case "$name" in
        /?*) ;;
        *)  echo "MALFORMED register line $lineno: the name must be literal: $home $name"
            fail=1
            continue ;;
    esac
    case "$home" in
        gstate)
            slot=${name#/}
            if ! grep -qx "$slot" "$work/slots"; then
                echo "MISSING gstate slot: /$slot (dropped or renamed in .gstatetemplate)"
                fail=1
            fi
            ;;
        *)
            # The name ends where PostScript ends one: at whitespace or a
            # delimiter. Regex metacharacters in the name -- the leading
            # dot most of them carry -- are matched as themselves.
            pat=$(printf '%s' "$name" | sed 's/[].[^$*\\+?(){}|/]/\\&/g')
            if ! grep -qE "(\\$home|${home#.}) $pat([][(){}<>/%[:space:]]|\$)" \
                 "$work/code"; then
                echo "MISSING member: $home $name (relocated or removed from data/*.ps)"
                fail=1
            fi
            ;;
    esac
done < "$golden"

if [ "$fail" -ne 0 ]; then
    echo "dict-homes: the register in tests/dict_homes.golden no longer holds."
    exit 1
fi
echo "SUCCESS"
exit 0
