#!/bin/sh
# Guard: a comparison whose answer the object width decides is reported
# by the compiler, in both widths, before it reaches another platform.
#
# The interpreter's integer type is an int in one build and wider in the
# other, so a bound written against the largest value an int holds is a
# real question in one width and a settled one in the other. A compiler
# is right to refuse the settled form, and the ones that do call it
# -Wtautological-type-limit-compare. Not every compiler has that name:
# -Wtype-limits, which the build already promotes to an error, does not
# reach a bound written as a cast of a limit constant, so a tree built
# only with a compiler lacking the first name reports nothing about a
# comparison the other rejects outright.
#
# What holds it here is one syntax-only pass per width with a compiler
# other than the one the tree was built with. Syntax-only is the whole
# of it: no objects, no link, nothing to install -- the cost is a
# fraction of a second, and it is the difference between hearing about
# such a comparison here and hearing about it from a platform lane.
#
# A machine with only one compiler cannot ask the question and says so
# by skipping, rather than passing quietly as though it had.
#
#   $1  source directory
#   $2  build directory (for the generated config header)
set -u
src=${1:?usage: check-width-comparisons.sh <srcroot> <buildroot>}
build=${2:?usage: check-width-comparisons.sh <srcroot> <buildroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
# The build directory is named for the header the build generated, which
# is where the width the tree was configured for is written down. A
# directory without one is not the build being checked: the unit still
# compiles, because the header it wants is found by another route or not
# needed at all, and the widths are then asked about a configuration
# nobody built.
guard_require_dir "$build" "the build directory"
guard_require_file "$build/config.h" "the generated configuration header"

guard_workdir

# The compiler the tree was built with is the one that has already had
# its say. What is wanted here is the other one.
built_with=cc
if [ -f "$build/compile_commands.json" ]; then
    if grep -q '"[^"]*clang[^"]*"' "$build/compile_commands.json" 2>/dev/null; then
        built_with=clang
    else
        built_with=gcc
    fi
fi

alt=
for c in clang gcc; do
    [ "$c" = "$built_with" ] && continue
    if command -v "$c" >/dev/null 2>&1; then
        alt=$c
        break
    fi
done

if [ -z "$alt" ]; then
    echo "only one compiler here, so a second opinion cannot be had"
    exit 77
fi

# One translation unit that reaches the width-dependent bounds: the
# device generic carries the buffer geometry and includes the driver
# contract, which is where a page extent is bounded against what a
# buffer can be indexed by.
unit=$src/src/lib/xpost_dev_generic.c
guard_require_file "$unit" "the unit the widths are asked about"
if ! grep -q 'xpost_dev_driver\.h' "$unit"; then
    echo "FAILURES: $unit no longer includes the driver contract, which is"
    echo "      where a page extent is bounded against what a buffer can be"
    echo "      indexed by; compiling it asks nothing about either width"
    exit 1
fi

flags="-fsyntax-only -I$src/src/lib -I$build -I$src
       -Werror=tautological-type-limit-compare
       -Werror=tautological-constant-out-of-range-compare
       -Werror=type-limits -Werror=sign-compare"

# ---- the compiler is shown to be answering ----
#
# A clean compile says nothing on its own: an empty file compiles clean
# under any flags, and so does a unit whose width-dependent bounds have
# been deleted or whose flags this compiler does not know. So put the
# compiler to a pair first -- one unit carrying the comparison this
# exists to catch and one carrying none -- and go on only if it
# separates them.
cat > "$work/control-quiet.c" <<'CQ'
int _xpost_width_control(unsigned int u);
int _xpost_width_control(unsigned int u)
{
    return (int)(u + 1u);
}
CQ
cat > "$work/control-loud.c" <<'CL'
#include <limits.h>
int _xpost_width_control(unsigned short v);
int _xpost_width_control(unsigned short v)
{
    return v <= (unsigned short)USHRT_MAX;
}
CL
if ! out=$("$alt" $flags "$work/control-quiet.c" 2>&1); then
    echo "FAILURES: $alt refuses a unit that carries no such comparison, so a"
    echo "      refusal below would say nothing about the tree:"
    printf '%s\n' "$out" | head -5 | sed 's/^/      /'
    exit 1
fi
if "$alt" $flags "$work/control-loud.c" >/dev/null 2>&1; then
    echo "FAILURES: $alt accepts a bound the object width settles, so it is"
    echo "      not being asked the question this check exists to put and"
    echo "      the tree would pass however such a comparison is written"
    exit 1
fi

fail=0
asked=0
for width in narrow wide; do
    case $width in
        narrow) def= ;;
        wide)   def=-DWANT_LARGE_OBJECT ;;
    esac
    out=$("$alt" $flags $def "$unit" 2>&1)
    st=$?
    asked=$((asked + 1))
    if [ "$st" -ne 0 ]; then
        # A unit that will not parse at all under the other compiler is
        # not this check's subject and is reported as what it is.
        if printf '%s\n' "$out" | grep -q "tautological"; then
            echo "FAILURES: $alt refuses a comparison in the $width width:"
            printf '%s\n' "$out" | grep -A2 "tautological" | sed 's/^/      /'
        else
            echo "FAILURES: $alt cannot read the unit in the $width width:"
            printf '%s\n' "$out" | head -5 | sed 's/^/      /'
        fi
        fail=1
    fi
done

if [ "$asked" -eq 0 ]; then
    echo "FAILURES: neither width was asked"
    exit 1
fi

[ "$fail" -eq 0 ] || exit 1
echo "width comparisons: $alt agrees in both widths"
echo "SUCCESS ($asked width(s) asked of $alt, which refuses such a comparison)"
