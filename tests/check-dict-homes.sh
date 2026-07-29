#!/bin/sh
# Meson test wrapper: assert that no registered private-namespace member
# has moved home or vanished from the interpreter's PostScript sources,
# and that no graphics-state template slot has been dropped or renamed.
#
# tests/dict_homes.golden is the register: "DICT /name" lines must appear
# as an adjacent pair somewhere in data/*.ps (a definition or a frozen
# reference -- both disappear when a member is relocated), and
# "gstate /slot" lines must appear as slots of the .gstatetemplate
# literal in data/gstate.ps. Additions are free: a feature that adds a
# member or slot appends it to the register in the same commit. This
# keeps machinery born in its final home -- a later commit cannot
# relocate it without failing here.
#
#   $1  path to the source tree root
set -u
src=$1
golden="$src/tests/dict_homes.golden"
fail=0
cr=$(printf '\r')   # tolerate CRLF line endings (Windows checkouts)

# the template slots, one per line, extracted once
slots=$(sed -n '/\.gstatetemplate <</,/>> def/p' "$src/data/gstate.ps" \
        | grep -oE '^[[:space:]]*/[a-zA-Z][a-zA-Z0-9]*' | tr -d ' /\r')

while read -r home name; do
    home=${home%"$cr"}; name=${name%"$cr"}
    case "$home" in
        ''|'#'*) continue ;;
        gstate)
            slot=${name#/}
            if ! printf '%s\n' "$slots" | grep -qx "$slot"; then
                echo "MISSING gstate slot: /$slot (dropped or renamed in .gstatetemplate)"
                fail=1
            fi
            ;;
        *)
            # accept the pair under either the dotted or plain privatedict spelling
            if ! grep -qE "(\\$home|${home#.}) $name([^.=a-zA-Z0-9]|\$)" "$src"/data/*.ps; then
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
