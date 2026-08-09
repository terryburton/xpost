#!/bin/sh
# Guard the library's exported symbol set against accidental growth.
#
# A symbol becomes exported by losing its `static`, which a declaration
# interposed between the keyword and the function does silently: the
# compiler reports only "useless storage class specifier in empty
# declaration", and the linkage change is invisible to every test.
#
# tests/exported_symbols.golden lists what the library exports. A new
# entry is a deliberate act and is added to the register in the same
# commit; a symbol that disappears breaks a consumer.
#
# Names beginning with an underscore are reserved at file scope (C99
# 7.1.3), so an exported one has to be declared twice over: once in the
# register, as any export must be, and once as a "reserved-exception"
# line saying that a reserved name is being used anyway. That second list
# may shrink and may not grow. Five such exports predate the rule and are
# recorded there; a sixth fails.
#
# Everything the linker will hand out counts, including weak definitions:
# taking only the strong ones let a weak symbol be exported without
# appearing here at all.
#
# Two shapes of library are read. An ELF object keeps its exports in a
# dynamic symbol table, which nm reads; a PE image keeps them in an
# export table of its own, which nm does not, and reading one with nm
# yields nothing at all. Nothing is not an answer, so the export table is
# read directly when that happens.
#
# A name with a full stop in it is not an identifier. The compiler makes
# them when it privatises or clones a function -- foo.lto_priv.0,
# foo.isra.0, foo.part.0 -- and an export table filled by taking every
# symbol the linker can name lists them beside the interface. They are
# not part of it and are read out.
#
# Some entry points exist only in builds configured for them: a device
# compiled for one windowing system is absent from every other build.
# Those are named on "conditional" lines, which neither require the
# symbol nor refuse it. What holds the device set itself is
# tests/check-device-roster.sh, which reads the sources.
#
# A build instrumented for coverage links the profiling runtime into the
# library, and that runtime's own exports -- the __gcov_ counters and the
# path mangler beside them -- come out of nm along with the interface.
# They are the compiler's, not this project's: nothing here can rename
# them, and six of them begin with two underscores, so the reserved-name
# rule refuses a coverage build outright and the register comparison
# refuses it a second time. An instrumented library is therefore read
# with those names taken out. The condition is the runtime's own
# presence -- a library with no __gcov_ symbol in it is not instrumented
# and nothing is taken out of it, so an ordinary build is read exactly as
# strictly as before. Any other name the runtime contributes is left in
# and fails here, which names the cause rather than hiding it.
#
# The comparison is a set comparison, so it is done in one collation --
# the C one, which is also the one a POSIX default environment sorts in.
# Sorting the register in the author's locale and comparing it without
# saying so left this check reporting "not in sorted order" and failing
# for everyone whose environment did not match.
#
# Usage: check-exported-symbols.sh <path to libxpost.so> <golden file>

set -u
lib=${1:?usage: check-exported-symbols.sh <library> <golden>}
golden=${2:?usage: check-exported-symbols.sh <library> <golden>}

LC_ALL=C
export LC_ALL

if ! command -v nm >/dev/null 2>&1; then
    echo "SKIP: nm is not available"
    exit 77
fi
# A library that is not there is a wrong path, not a platform without
# shared libraries: answering that with a skip made every misdirection of
# this check permanently silent.
if [ ! -f "$lib" ]; then
    echo "FAILURES: no library to read at $lib"
    exit 1
fi
if [ ! -s "$golden" ] || [ ! -r "$golden" ]; then
    echo "FAILURES: no usable register at $golden"
    exit 1
fi

work=$(mktemp -d 2>/dev/null) || work=
if [ -z "$work" ] || [ ! -d "$work" ] || [ ! -w "$work" ]; then
    echo "FAILURES: could not make a scratch directory (is TMPDIR writable?)"
    exit 1
fi
trap 'rm -rf "$work"' EXIT

# every defined dynamic symbol, whatever section or linkage it has
nm -D --defined-only "$lib" 2>/dev/null | tr -d '\r' \
    | awk 'NF == 3 && $2 !~ /^[a-z]$/ { print $3 }' > "$work/raw"

# a PE image, whose exports are not in a symbol table nm reads
if [ ! -s "$work/raw" ] && command -v objdump >/dev/null 2>&1; then
    objdump -p "$lib" 2>/dev/null | tr -d '\r' \
        | awk '/\[Ordinal\/Name Pointer\] Table/ { table = 1; next }
               table && /\+base\[/ { print $NF }' > "$work/raw"
fi

# a Mach-O image: the nm there has neither of those options, lists a
# defined symbol under -U, and writes a C name with the underscore the
# ABI puts in front of it
if [ ! -s "$work/raw" ]; then
    nm -gU "$lib" 2>/dev/null | tr -d '\r' \
        | awk 'NF == 3 && $2 !~ /^[a-z]$/ { sub(/^_/, "", $3); print $3 }' \
        > "$work/raw"
fi

grep -v '\.' "$work/raw" | sort -u > "$work/have"

# the profiling runtime's exports, present only when the library carries
# the runtime at all
if grep -q '^__gcov_' "$work/have"; then
    grep -vE '^(__gcov_|mangle_path$)' "$work/have" > "$work/have.t"
    mv "$work/have.t" "$work/have"
fi

if [ ! -s "$work/have" ]; then
    case $lib in
        *.a|*.lib)
            echo "SKIP: $lib is a static library; it has no dynamic symbol table"
            exit 77 ;;
    esac
    echo "FAILURES: nm read no defined dynamic symbols from $lib"
    exit 1
fi

sed 's/\r$//' "$golden" | grep -vE '^[[:space:]]*(#|$)' \
    | grep -vE '^(reserved-exception|conditional) ' | sort -u > "$work/register"
sed 's/\r$//' "$golden" | sed -n 's/^reserved-exception //p' \
    | sort -u > "$work/allowed-reserved"
sed 's/\r$//' "$golden" | sed -n 's/^conditional //p' \
    | sort -u > "$work/conditional"

# a conditional name is neither required nor refused, so it takes no part
# in either direction of the comparison
if [ -s "$work/conditional" ]; then
    comm -23 "$work/have" "$work/conditional" > "$work/have.t"
    mv "$work/have.t" "$work/have"
fi

if [ ! -s "$work/register" ]; then
    echo "FAILURES: the register at $golden names no symbols"
    exit 1
fi

fail=0

added=$(comm -13 "$work/register" "$work/have")
removed=$(comm -23 "$work/register" "$work/have")

if [ -n "$added" ]; then
    echo "FAIL: newly exported symbols not in the register:"
    printf '%s\n' "$added" | sed 's/^/      /'
    fail=1
fi
if [ -n "$removed" ]; then
    echo "FAIL: symbols in the register no longer exported:"
    printf '%s\n' "$removed" | sed 's/^/      /'
    fail=1
fi

# the reserved-identifier rule, on what is actually exported
grep '^_' "$work/have" > "$work/reserved" || true
undeclared=$(comm -23 "$work/reserved" "$work/allowed-reserved")
if [ -n "$undeclared" ]; then
    echo "FAIL: exported names reserved to the implementation (C99 7.1.3):"
    printf '%s\n' "$undeclared" | sed 's/^/      /'
    echo "      give the symbol a name of its own, or -- if it truly cannot"
    echo "      have one -- add a reserved-exception line saying why."
    fail=1
fi
# the list may shrink and may not grow, so an exception for a name that is
# no longer exported is retired rather than left standing
stale=$(comm -13 "$work/reserved" "$work/allowed-reserved")
if [ -n "$stale" ]; then
    echo "FAIL: reserved-exception recorded for a name that is not exported:"
    printf '%s\n' "$stale" | sed 's/^/      /'
    echo "      remove the line; the exception list only shrinks."
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: the exported symbol set changed"
    exit 1
fi
echo "SUCCESS"
exit 0
