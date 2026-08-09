# Sourced by the check-*.sh guards: refuse a path that is not what the
# guard was promised.
#
# A guard that cannot find what it was pointed at must fail, not answer
# about something else. That is not a theoretical worry here.
# XPOST_DATA_DIR is only the first candidate the interpreter tries: if
# init.ps is not there it moves on without complaint, to the directory of
# the shared library and then to data, ../data and ../../data relative to
# wherever it was started. A guard handed the wrong source root therefore
# does not fail -- it finds the working tree by one of those routes and
# reports a perfectly true result about a tree the caller did not mean.
# That is worse than reading something stale, because every number in the
# report is real and nothing about it looks wrong.
#
# Every guard that derives a path from an argument passes it through here
# before use. tests/check-guard-paths.sh holds them to that.

guard_require_dir() {
    if [ ! -d "$1" ] || [ ! -r "$1" ]; then
        echo "FAILURES: $2 is not a readable directory: $1"
        exit 1
    fi
}

# -s alone answers yes for a file that cannot be opened, and a guard that
# cannot read its own register reports whatever an empty read gives it,
# which is usually agreement.
guard_require_file() {
    if [ ! -s "$1" ] || [ ! -r "$1" ]; then
        echo "FAILURES: $2 is missing, empty or unreadable: $1"
        exit 1
    fi
}

# The source root a guard is given must be the tree it is meant to read,
# not a subdirectory of it and not the build directory. Naming the
# subdirectory the caller most often passes by mistake makes the failure
# say what went wrong rather than only that something did.
guard_require_srcroot() {
    if [ ! -d "$1" ]; then
        echo "FAILURES: the source root is not a directory: $1"
        exit 1
    fi
    if [ ! -d "$1/data" ] || [ ! -d "$1/tests" ]; then
        echo "FAILURES: not a source root (no data/ and tests/ under it): $1"
        if [ -f "$1/init.ps" ]; then
            echo "      that looks like the data directory itself; pass its parent"
        fi
        exit 1
    fi
    # Two directories of the right names prove nothing: an empty pair
    # passes, and the interpreter then finds the real tree by its own
    # search and answers about that instead -- a true report about a tree
    # nobody asked for. Name a file that only the tree being checked has.
    if [ ! -r "$1/data/init.ps" ] || [ ! -r "$1/tests/guard-paths.sh" ]; then
        echo "FAILURES: not a source root (data/init.ps and tests/guard-paths.sh must be readable under it): $1"
        exit 1
    fi
}

# A guard whose scratch directory was never made writes its intermediate
# files to /, reads nothing back, and reports agreement between two empty
# sets. Checked here so every guard that sources this is covered without
# each having to remember.
# Sets `work` in the caller, rather than answering on stdout: an exit
# inside a command substitution ends only the subshell, so a guard that
# wrote `work=$(guard_workdir)` would print the refusal and carry on with
# an empty path -- which is the failure this exists to stop.
guard_workdir() {
    work=$(mktemp -d 2>/dev/null) || work=
    if [ -z "$work" ] || [ ! -d "$work" ] || [ ! -w "$work" ]; then
        echo "FAILURES: could not make a scratch directory (is TMPDIR writable?)"
        exit 1
    fi
}

# Mirror text files into the scratch directory with carriage returns
# taken out, and set `mirror` to where they landed. Requires guard_workdir.
#
# A carriage return is a line ending, not content. A checkout that brought
# CRLF in -- which is what git does on Windows by default, and what the
# .gitattributes file exists to stop -- leaves one at the end of every
# line, where it makes `$` match nothing: a sed range then never closes
# and runs to the end of the file, a grep for a whole line finds none, and
# the guard reports about a fraction of the tree without saying so. One
# guard went green that way with five sixths of its population missing.
# Guards read the mirror, so they hold the same rule on either checkout.
guard_mirror() {
    mirror="$work/mirror-$1"
    shift
    if ! mkdir -p "$mirror"; then
        echo "FAILURES: could not make a scratch directory under $work"
        exit 1
    fi
    for f in "$@"; do
        [ -f "$f" ] || continue
        tr -d '\r' < "$f" > "$mirror/$(basename "$f")"
    done
}

# The same, for a guard that reads across the tree rather than one
# directory: mirrors the source root and sets `mirror` to the copy, which
# the guard then uses as its source root. Requires guard_workdir.
#
# A corpus is left out. Its programs are fetched and belong to their own
# sources, no guard scans them, and its scratch directory is written
# while the corpus tests run -- a walk that copies it races them and
# dies on a file that went away between being listed and being read.
# Which paths those are is stated once, in tests/corpus/.gitignore; that
# file is distributed, so a tarball states it too.
guard_mirror_tree() {
    mirror="$work/tree"
    if ! mkdir -p "$mirror"; then
        echo "FAILURES: could not make a scratch directory under $work"
        exit 1
    fi
    gm_prune=
    gm_pats=$(tr -d '\r' < "$1/tests/corpus/.gitignore" 2>/dev/null \
        | sed 's/#.*//' | tr -s ' \t' '\n' | grep .)
    set -f
    for gm_p in $gm_pats; do
        case $gm_p in
        */) gm_prune="$gm_prune -path 'tests/corpus/${gm_p%/}' -prune -o" ;;
        *)  gm_prune="$gm_prune -path 'tests/corpus/$gm_p' -prune -o" ;;
        esac
    done
    set +f
    eval "( cd \"\$1\" && find data src tests $gm_prune -type f -print )" \
        2>"$work/gm-err" > "$work/gm-list"
    # The directories, and the files a single pass will not reach: an
    # empty one has no line to be read and so is never opened.
    while read -r gm_rel; do
        gm_d=${gm_rel%/*}
        [ "$gm_d" = "$gm_rel" ] || [ -d "$mirror/$gm_d" ] || mkdir -p "$mirror/$gm_d"
        [ -s "$1/$gm_rel" ] || : > "$mirror/$gm_rel"
    done < "$work/gm-list"
    # Then the contents, in one pass over the whole list rather than a
    # process for each file. A tree of a few hundred files costs a few
    # hundred processes that way, which on a platform where starting one
    # is expensive took this longer than the guard that called it.
    ( cd "$1" && xargs awk -v dir="$mirror" '
        FNR == 1 { if (gm_out != "") close(gm_out); gm_out = dir "/" FILENAME }
        { gsub(/\r/, ""); print > gm_out }
      ' < "$work/gm-list" )
    for f in Makefile.am meson.build; do
        [ -f "$1/$f" ] && tr -d '\r' < "$1/$f" > "$mirror/$f"
    done
    if [ ! -r "$mirror/data/init.ps" ] || [ ! -r "$mirror/tests/guard-paths.sh" ]; then
        echo "FAILURES: could not mirror the source tree under $1"
        echo "      files listed: $(wc -l < "$work/gm-list" 2>/dev/null)"
        if [ -s "$work/gm-err" ]; then
            echo "      the walk wrote:"
            sed 's/^/        /' "$work/gm-err"
        fi
        for gm_w in data/init.ps tests/guard-paths.sh; do
            [ -r "$mirror/$gm_w" ] || echo "      absent from the mirror: $gm_w"
        done
        exit 1
    fi
    # Counts in against counts out. The pass above is what puts the tree
    # in the mirror, and one that stopped partway leaves a subset there:
    # every guard then scans less than the tree while reporting on the
    # tree, and their scan loops pass over a file that is not there
    # rather than saying so. Two files being readable is no evidence
    # about the several hundred beside them, which is the shape the
    # guards themselves are written against.
    gm_in=$(grep -c . "$work/gm-list")
    gm_out=$( ( cd "$mirror" && find data src tests -type f -print ) \
              2>/dev/null | grep -c . )
    if [ "$gm_in" -eq 0 ] || [ "$gm_out" -ne "$gm_in" ]; then
        echo "FAILURES: $gm_out of $gm_in files reached the mirror of $1;"
        echo "      a guard reading it would scan part of the tree and"
        echo "      report on the whole of it"
        exit 1
    fi
}

# Read C sources as C rather than as text: every named file is emitted as
# "<path>:<line>:<code>" with comments and string literals removed, so a
# guard scanning for a construct is not answered by a mention of it in a
# comment or by a word inside a message. Preprocessor lines are kept --
# a macro that aliases the thing being guarded is a way past the rule,
# not a comment on it -- and a guard that does not want them drops them.
#
# Every guard that reads C goes through here, so that what counts as code
# is stated once. Take the files by name; a build in the tree leaves
# object files beside the sources whose debug information answers to the
# same patterns.
guard_c_source() {
    awk '
        FNR == 1 { inblock = 0; instr = 0 }
        { sub(/\r$/, "") }
        {
            line = $0
            sub(/\r$/, "", line)
            out = ""
            i = 1
            n = length(line)
            while (i <= n) {
                c = substr(line, i, 1)
                d = substr(line, i, 2)
                if (inblock) {
                    if (d == "*/") { inblock = 0; i += 2 } else i++
                    continue
                }
                if (instr) {
                    if (c == "\\") { i += 2; continue }
                    if (c == q) instr = 0
                    i++
                    continue
                }
                if (d == "/*") { inblock = 1; i += 2; continue }
                if (d == "//") break
                if (c == "\"" || c == "'\''") { instr = 1; q = c; i++; continue }
                out = out c
                i++
            }
            print FILENAME ":" FNR ":" out
        }' "$@"
}
