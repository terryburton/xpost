#!/bin/sh
# Meson test wrapper: assert that every guard which derives a path from an
# argument refuses a path that is not what it was promised.
#
# The guards take a source root, or a directory or file beneath one, and
# read the tree there. Handed the wrong one, they do not fail:
# XPOST_DATA_DIR is only the first candidate the interpreter tries, and it
# falls through to the shared library's directory and to relative paths,
# so the guard finds the working tree anyway and reports a true result
# about a tree nobody asked about. Every number in that report is real,
# which is what makes it dangerous.
#
# tests/guard-paths.sh carries the refusal. This holds each guard to
# calling it, and then to the refusal actually firing.
#
# Three things this got wrong, each of which made it agree with a guard
# that was not working:
#
#   A refusal was taken to be any non-zero exit. A guard with a syntax
#   error, one whose tool is missing, one that skips, and one that exits 1
#   whatever it is handed all read as refusals -- the more broken a guard
#   was, the more certainly it passed. A refusal is exit 1 with something
#   said, and it only counts alongside the other half of the pair: the
#   same guard, handed what it really wants, must succeed. That is what
#   separates "refuses a wrong path" from "refuses everything".
#
#   Which guards to check was decided by grepping each one for '$src/data'
#   and the like, so a guard opted out by naming its variable something
#   else or writing "$1/data". The list comes from meson.build now, which
#   is where the guards are actually run from and cannot be quietly left
#   out of, and a guard in tests/ that appears in no test is itself a
#   failure. The argument shapes come from there too -- two of the three
#   shapes tried before pointed at $src/build/src/bin/xpost, a path that
#   exists in no build, so those probes ran the guard with a
#   nonexistent binary and its refusal proved nothing about a path.
#
#   The decoy was a directory holding init.ps, which is the one shape
#   guard_require_srcroot names in its own message. A directory with
#   empty data/ and tests/ under it is the harder one, and the one the
#   interpreter's fallback search makes dangerous.
#
#   Every decoy was a path that is wrong, so what was held was only that
#   a guard reads the path it was given. A guard handed the right path to
#   a tree with nothing in it was held to nothing at all: it scanned an
#   empty population, found no fault in it -- there being none to find --
#   and answered SUCCESS. That answer is the dangerous one, because it is
#   the same answer a working guard gives, and a reader takes it for
#   coverage. So a guard must say how many things it examined and refuse
#   a count of none: the emptied tree below is a source root of exactly
#   the real shape, every directory and every file name in place and
#   every one of them empty, and a guard that passes on it is a guard
#   that never checked it had anything to look at.
#
#   $1  path to the source tree root
#   $2  path to the built interpreter
#   $3  path to the built library
set -u
src=${1:?usage: check-guard-paths.sh <srcroot> <xpost> <library>}
xpost=${2:?usage: check-guard-paths.sh <srcroot> <xpost> <library>}
lib=${3:?usage: check-guard-paths.sh <srcroot> <xpost> <library>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$src/meson.build" "the build description"

guard_workdir
trap 'rm -rf "$work"' EXIT

# ---- the decoys ----
# a directory that is emphatically not a source root, and looks like the
# mistake actually made: the data directory itself
decoy_data="$work/decoy-data"
mkdir -p "$decoy_data"
: > "$decoy_data/init.ps"
# and the one that gets past a check for the names alone: the right two
# directories, with nothing in them. The interpreter then finds the real
# tree by its own search and the guard reports about that instead.
decoy_shape="$work/decoy-shape"
mkdir -p "$decoy_shape/data" "$decoy_shape/tests"
# a path that is not there at all
decoy_gone="$work/decoy-gone"
# an empty directory, for an argument naming a directory under the root
decoy_empty="$work/decoy-empty"
mkdir -p "$decoy_empty"
# and an empty file, for an argument naming a register or a test
decoy_emptyfile="$work/decoy-empty-file"
: > "$decoy_emptyfile"
# and the source tree with everything taken out of it: the real shape,
# every file empty. A corpus is left out for the reason guard_mirror_tree
# leaves it out -- it is written while its own tests run, and a walk that
# lists it races them -- but its .gitignore stays, that being the file
# the mirroring reads to know what to leave out.
decoy_hollow="$work/decoy-hollow"
mkdir -p "$decoy_hollow"
( cd "$src" && find data src tests -path 'tests/corpus/*' -prune -o -type f -print ) \
    > "$work/hollow-list" 2>/dev/null
echo tests/corpus/.gitignore >> "$work/hollow-list"
nhollow=$(grep -c . "$work/hollow-list" || true)
if [ "${nhollow:-0}" -lt 100 ]; then
    echo "FAILURES: only $nhollow files were listed under $src, so the emptied"
    echo "      tree would be empty for the wrong reason and every guard"
    echo "      would refuse it without being held to anything"
    exit 1
fi
( cd "$decoy_hollow" \
  && sed 's|/[^/]*$||' "$work/hollow-list" | sort -u | xargs mkdir -p \
  && xargs touch < "$work/hollow-list" )
: > "$decoy_hollow/meson.build"
if [ ! -r "$decoy_hollow/data/init.ps" ] || [ ! -r "$decoy_hollow/tests/guard-paths.sh" ]; then
    echo "FAILURES: the emptied tree was not made; a guard would refuse it as"
    echo "      a path rather than as a tree with nothing in it"
    exit 1
fi

# ---- what meson runs, and with what ----
#
# Each guard's argument list is read from its registration: a bare
# meson.current_source_dir() is the source root, one with path components
# after it is a directory or file beneath it, files(...) is a file in the
# tree, and the two built things are named. An argument this cannot read
# is a failure rather than something to pass over.
#
# The name is taken as everything up to the closing quote it is written
# inside, rather than as a run of the characters names have been made of
# so far. A reader spelling out those characters is narrower than the
# set it reads, and the two part company at whichever name is first
# written with one it does not list -- which does not read as a reader
# that came up short. It reads as this check saying a guard is
# registered nowhere, of a guard the same run has just executed, and the
# name it says that of is the one piece of evidence pointing away from
# the cause. Quoting is what the file guarantees about a name, so it is
# what the name is read to.
tr -d '\r' < "$src/meson.build" | awk '
    /find_program\(.tests\/check-[^'"'"']*\.sh.\)/ {
        match($0, /tests\/check[^'"'"']*\.sh/)
        cur = substr($0, RSTART, RLENGTH)
        sub(/^tests\//, "", cur)
        collecting = 0
        next
    }
    cur != "" && /args:/ { collecting = 1; buf = "" }
    collecting {
        buf = buf " " $0
        if (index($0, "]") > 0) {
            sub(/.*args:[[:space:]]*\[/, "", buf)
            sub(/\].*/, "", buf)
            print cur "\t" buf
            cur = ""; collecting = 0
        }
    }
' > "$work/registrations"

if [ ! -s "$work/registrations" ]; then
    echo "FAILURES: no guard registrations found in meson.build; this check"
    echo "      can no longer read the file it takes its list from"
    exit 1
fi

# every guard in the tree must be one meson runs
for g in "$src"/tests/check-*.sh; do
    base=$(basename "$g")
    if ! cut -f1 "$work/registrations" | grep -qx "$base"; then
        echo "FAIL: $base is in tests/ and in no meson test; nothing runs it"
        echo "      and nothing here can hold it to anything"
        exit 1
    fi
done

fail=0
checked=0
probes=0

# Run a guard with the argument list given, and say what happened.
# Prints "status" and leaves the output in $work/out.
runguard() {
    g=$1
    shift
    sh "$g" "$@" > "$work/out" 2>&1
    echo $?
}

while IFS="$(printf '\t')" read -r base args; do
    g="$src/tests/$base"
    [ "$base" = "check-guard-paths.sh" ] && continue
    if [ ! -r "$g" ]; then
        echo "FAIL: meson runs tests/$base, which is not there"
        fail=1
        continue
    fi

    # the argument list, one per line, as a kind and a value
    printf '%s\n' "$args" | tr ',' '\n' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' \
      | grep -v '^$' > "$work/args"
    n=0
    : > "$work/kinds"
    : > "$work/values"
    bad=
    while read -r a; do
        n=$((n + 1))
        case $a in
            "meson.current_source_dir()")
                echo SRCROOT >> "$work/kinds"; echo "$src" >> "$work/values" ;;
            "meson.current_source_dir()"*)
                rel=$(printf '%s' "$a" | grep -oE "'[^']*'" | tr -d "'" | tr '\n' '/')
                rel=${rel%/}
                echo SUBPATH >> "$work/kinds"; echo "$src/$rel" >> "$work/values" ;;
            "files("*)
                rel=$(printf '%s' "$a" | grep -oE "'[^']*'" | tr -d "'")
                echo FILE >> "$work/kinds"; echo "$src/$rel" >> "$work/values" ;;
            "meson.current_build_dir()")
                # the build root, which a guard needs when it reads a
                # generated header rather than a source one. Derived from
                # the built binary this check was handed, so it names the
                # tree being checked rather than whichever one is current.
                echo BUILDROOT >> "$work/kinds"
                echo "$(dirname "$(dirname "$(dirname "$xpost")")")" >> "$work/values" ;;
            "xpost_exe")
                echo BUILT >> "$work/kinds"; echo "$xpost" >> "$work/values" ;;
            "libxpost_lib.full_path()")
                echo BUILT >> "$work/kinds"; echo "$lib" >> "$work/values" ;;
            *)  bad=$a ;;
        esac
    done < "$work/args"

    if [ -n "$bad" ]; then
        echo "FAIL: cannot read the arguments meson gives tests/$base: $bad"
        echo "      this check would have passed over it in silence"
        fail=1
        continue
    fi
    if [ "$n" -eq 0 ]; then
        echo "FAIL: tests/$base is run with no arguments; there is no path"
        echo "      for it to be misdirected with, and nothing to check"
        fail=1
        continue
    fi

    checked=$((checked + 1))

    # ---- the guard must succeed on what it really wants ----
    #
    # Without this, a guard that exits 1 whatever it is handed passes
    # every probe below, and the more broken it is the better it looks.
    set --
    while read -r v; do set -- "$@" "$v"; done < "$work/values"
    st=$(runguard "$g" "$@")
    # 77 is the skip status, which a guard uses to say the question it
    # asks cannot be put on this machine -- a guard needing a second
    # compiler on a machine with one, say. That is not a guard failing
    # on the tree it is given, and the probes below are skipped with it:
    # a guard that cannot ask its question cannot be asked to notice a
    # tree that has been broken either.
    if [ "$st" = 77 ]; then
        echo "$base: skipped, so what it holds was not put to the test"
        continue
    fi
    if [ "$st" != 0 ]; then
        echo "FAIL: tests/$base does not succeed on the tree it is given"
        echo "      (exit $st); its refusals below would prove nothing"
        sed 's/^/      /' "$work/out"
        fail=1
        continue
    fi

    # ---- and refuse each decoy, in each path position ----
    i=0
    while read -r kind; do
        i=$((i + 1))
        case $kind in
            SRCROOT)   list="$decoy_data $decoy_shape $decoy_gone $decoy_hollow" ;;
            SUBPATH)   list="$decoy_empty $decoy_gone" ;;
            FILE)      list="$decoy_emptyfile $decoy_gone" ;;
            # A built thing that is not there, or is there and holds
            # nothing, is the same hazard in the other half of the
            # argument list: a guard that runs an interpreter it could
            # not find, or reads a library with no symbols in it, reports
            # about nothing.
            BUILT)     list="$decoy_emptyfile $decoy_gone" ;;
            BUILDROOT) list="$decoy_empty $decoy_gone" ;;
            *)         continue ;;
        esac
        for d in $list; do
            set --
            j=0
            while read -r v; do
                j=$((j + 1))
                if [ "$j" -eq "$i" ]; then set -- "$@" "$d"; else set -- "$@" "$v"; fi
            done < "$work/values"
            st=$(runguard "$g" "$@")
            probes=$((probes + 1))
            case $st in
                1)  if [ ! -s "$work/out" ]; then
                        echo "FAIL: tests/$base refused $d in position $i without saying why"
                        fail=1
                    fi ;;
                0)  echo "FAIL: tests/$base accepted $d in position $i"
                    fail=1 ;;
                77) echo "FAIL: tests/$base skipped rather than refused $d in position $i;"
                    echo "      a skip is a permanent silent pass"
                    fail=1 ;;
                *)  echo "FAIL: tests/$base exited $st on $d in position $i, which is"
                    echo "      not a refusal -- a missing tool, or the script itself broken"
                    sed 's/^/      /' "$work/out"
                    fail=1 ;;
            esac
        done
    done < "$work/kinds"

    # a guard taking a source root must call the refusal rather than
    # reinvent it, so that what a source root is stays said in one place
    if grep -qx SRCROOT "$work/kinds" \
       && ! grep -q 'guard_require_srcroot' "$g"; then
        echo "FAIL: tests/$base takes a source root without validating it"
        echo "      through guard_require_srcroot"
        fail=1
    fi
done < "$work/registrations"

if [ "$checked" -lt 15 ]; then
    echo "FAILURES: only $checked guards were checked; the registrations could"
    echo "      not be read and this check is unusable"
    exit 1
fi

[ "$fail" = 0 ] || exit 1
echo "SUCCESS ($checked guards succeed on the tree and refuse $probes wrong paths)"
exit 0
