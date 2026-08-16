#!/bin/sh
#
# The confinement an ordinary command line arranges.
#
# Every other PostScript test here runs with --no-sandbox, so the
# configuration every real run has -- the sandbox the command line raises
# by default -- was exercised by nothing that speaks PostScript. This
# runs the shipped default and holds it to what the option's own help
# text promises: the program reaches its working area and nothing else.
#
# The run happens from a scratch directory of its own, because the
# permitted set is built from where the run stands: the current
# directory, the temporary directory, and the directories of the input
# and output files. A run from the source tree would be permitted the
# source tree, and the file it is refused has to be somewhere none of
# those reach. So both the working directory and the refused file are
# made here, as siblings, and the program is copied in -- its own
# directory is read-permitted too, which would otherwise be the way in.
#
#   $1  path to the xpost binary
#   $2  path to the source tree root
set -u
xpost=${1:?usage: run-cli-sandbox-test.sh <xpost> <srcroot>}
src=${2:?usage: run-cli-sandbox-test.sh <xpost> <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
. "$(dirname "$0")/verdict.sh"
guard_require_srcroot "$src"
if [ ! -x "$xpost" ]; then
    echo "FAILURES: the interpreter is not an executable: $xpost"
    exit 1
fi
# the run below happens from a directory of its own, so a binary named
# relative to where this started would not be there to find
case $xpost in
    /*) ;;
    *) xpost=$(pwd)/$xpost ;;
esac

prog="$src/tests/cli_sandbox_test.ps"
[ -r "$prog" ] || { echo "FAILURES: not readable: $prog"; exit 1; }

work=$(mktemp -d) || { echo "FAILURES: no scratch directory"; exit 1; }
trap 'rm -rf "$work"' EXIT

mkdir "$work/run" || { echo "FAILURES: could not make the working directory"; exit 1; }
cp "$prog" "$work/run/prog.ps" || { echo "FAILURES: could not place the program"; exit 1; }
mkdir "$work/elsewhere" || { echo "FAILURES: could not make the other directory"; exit 1; }
# The temporary directory is one of the permitted trees, and this scratch
# is inside the system one -- so the file to be refused would sit in a
# tree the run may reach, and the refusals below would all be failures of
# the test rather than of the sandbox. Give the run a temporary directory
# of its own instead, which is also what says out loud that the default
# permits one.
mkdir "$work/tmp" || { echo "FAILURES: could not make the temporary directory"; exit 1; }
echo secret > "$work/elsewhere/f" ||
    { echo "FAILURES: could not write the file to be refused"; exit 1; }

# The refused file must exist and be readable to this user, or the
# refusal below would be indistinguishable from a missing name.
[ -r "$work/elsewhere/f" ] ||
    { echo "FAILURES: the file to be refused is not readable outside the run"; exit 1; }

# No --no-sandbox: this is the shipped configuration. The output goes to
# the working directory rather than /dev/null, whose directory would
# otherwise be write-permitted.
out=$(cd "$work/run" && TMPDIR="$work/tmp" XPOST_DATA_DIR="$src/data" "$xpost" \
      -q -d null -o out.txt "-DOUTSIDE=($work/elsewhere/f)" prog.ps </dev/null 2>&1)
st=$?
verdict_ok "$out" "the command-line sandbox test" ||
    { printf '%s\n' "$out" | sed 's/^/      /'; exit 1; }
if [ "$st" -ne 0 ]; then
    echo "FAILURES: the sandbox test exited with status $st after reporting"
    exit 1
fi

# What the program was told and what the filesystem did are two claims.
if [ ! -s "$work/elsewhere/f" ]; then
    echo "FAILURES: the file outside the permitted set was emptied"
    exit 1
fi

printf '%s\n' "$out" | grep -v '^SUCCESS' | sed 's/^/  /'
echo "SUCCESS (the default command line confines a program to its working area)"
