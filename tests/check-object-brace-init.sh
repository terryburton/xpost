#!/bin/sh
# Guard how much of an object a brace initialiser clears.
#
# `Xpost_Object o = { 0 };` is how the library starts an object it is
# about to fill in, and it is written that way in every module. What it
# actually clears is decided in one place: the order of the members of
# the union. A union is not an aggregate, so the initialiser gives the
# member named first a value and says nothing about the storage past
# that member (C99 6.2.5, 6.7.8). Name a member narrower than the object
# first and the rest of every such object holds whatever the storage held
# before -- and an object is written to virtual memory whole, so what a
# reader takes back out of it is a value the language never put there.
# It is invisible where the compiler happens to clear the whole union
# anyway, which is most of them, most of the time.
#
# So the first member must be one that spans the object, and the two
# assertions in the header must still say that it does: the order alone
# is not the guarantee, it is the guarantee only alongside them.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-object-brace-init.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
hdr=$src/src/lib/xpost_object.h
guard_require_file "$hdr" "the object header"
fail=0

# The union's body: the lines between "typedef union" and the line that
# closes it as Xpost_Object. Read with the line endings taken out, so a
# checkout that carries them does not read the last token of every line
# as a different token.
body=$(tr -d '\r' < "$hdr" | sed -n '/^typedef union$/,/^} Xpost_Object;$/p')
if [ -z "$body" ]; then
    echo "check-object-brace-init: no union Xpost_Object in $hdr"
    exit 1
fi

# the first member declared in it
first=$(printf '%s\n' "$body" | sed -n '/^{$/,$p' | sed -n \
        '/^ *[A-Za-z_][A-Za-z_0-9]* [A-Za-z_][A-Za-z_0-9]*;$/{s/^ *//;p;q;}')
if [ "$first" != "Xpost_Object_Mark mark_;" ]; then
    echo "check-object-brace-init: the object union names '${first:-nothing}'"
    echo "      first, and a brace initialiser clears no further than the"
    echo "      member named first. It must name a member that spans an"
    echo "      object, which is Xpost_Object_Mark mark_;"
    fail=1
fi

# and what makes that member span an object: mark_ is as wide as the
# union, and an object is its three fields with no padding among them
if ! grep -q 'XPOST_OBJECT_MEMBER_FILLS_UNION(mark_)' "$hdr"; then
    echo "check-object-brace-init: nothing holds mark_ to being as wide as"
    echo "      an object, so naming it first no longer says a brace"
    echo "      initialiser reaches every field"
    fail=1
fi
if ! grep -q 'xpost_object_is_tag_pad_and_payload' "$hdr"; then
    echo "check-object-brace-init: nothing holds an object to being a tag,"
    echo "      a pad and a payload and no padding, so the fields mark_"
    echo "      declares are no longer every byte of one"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "check-object-brace-init: an object brace-initialised in this tree"
    echo "      is not cleared to its full width."
    exit 1
fi
echo "check-object-brace-init: ok (a brace initialiser covers every field of an object)"
exit 0
