#!/bin/sh
#
# Guard: nothing in the environment ends the process.
#
# The library is embedded. A host that loads it -- a long-running server
# among them -- keeps its own process, and the interpreter's answer to a
# state it cannot go on from is an error object raised into that host's
# run, not a signal delivered to it. An environment variable that turns
# an error into a dead process moves the decision out of the code
# entirely: whoever can set a variable in the process decides whether a
# served request fails or the server does.
#
# `stop` with no enclosing stopped context is the case that makes it
# concrete. PLRM 8.2 says stop there prints a message and executes quit,
# so it is a specified ending rather than a fault, and the interpreter
# cannot answer it with an error: errordict's handlers themselves finish
# with stop, so raising one recurses without bound. The path is normal
# and reachable -- the job error handler is the one piece of a run that
# is outside every stopped context, so a failure inside it arrives here
# -- and a process that aborts on it aborts on specified behaviour.
#
# Two rules, because neither covers the other:
#
#   abort is not named in the library at all. It is the whole of what a
#   library must not do to its host, it needs no window to be read in,
#   and the one place that genuinely cannot raise an error has quit
#   named for it by the specification.
#
#   no getenv gates an ending. That reaches exit and its spellings,
#   which abort's rule says nothing about, and it reaches src/bin as
#   well -- a program may end itself, but not because of a variable.
#   Read in a window, so an ending further from its getenv than the
#   window is outside the rule; the window is put to a fixture below
#   rather than trusted, since a pattern that finds nothing in a sound
#   tree and a pattern that finds nothing at all read alike.
#
# What is deliberately not here: reading the environment for what to do
# is ordinary, and the library does it for the data directory, the log
# level, the temporary directory and the collector's diagnostics. Those
# choose behaviour. The rule is about ending, not about reading.
#
#   $1  path to the xpost binary
#   $2  path to the source tree root
set -u
xpost=${1:?usage: check-env-traps.sh <xpost> <srcroot>}
src=${2:?usage: check-env-traps.sh <xpost> <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_dir "$src/src/lib" "the library source directory"
guard_require_dir "$src/src/bin" "the program source directory"

guard_workdir
trap 'rm -rf "$work"' EXIT
fail=0

# The names an ending is written with. abort is in the list twice over:
# once here, where it is an ending like the others, and once in the rule
# below that refuses it wherever it appears.
enders='abort|exit|_exit|_Exit|quick_exit|raise'

# How far after a getenv an ending is still read as gated by it. Four
# lines covers the shapes the tree writes: the ending on the same line,
# the ending on the next, and a braced body with a comment in it.
window=4

# ---- the runs: the specified path, under the variables and without ----
#
# The program reaches `stop` with nothing to catch it by failing inside
# the job's error handler. That handler is what the start procedure runs
# after its stopped context has already been left, so a failure in it
# passes no stopped context on its way out, and the errordict handler
# that ends with stop finds none.
#
# Reaching it is checked rather than assumed. The route runs through the
# start procedure in data/init.ps, and a rearrangement there that stops
# the program getting to the path would leave this reporting that an
# abort did not happen on a path nobody took.
cat > "$work/nostop.ps" <<'EOF'
% Fail inside the job error handler, which runs outside every stopped
% context: the errordict handler for the failure ends with `stop`, and
% there is no stopped context left for it to end.
/handleerror { 1 0 idiv } def
1 0 idiv
EOF

reached='no stopped context'

run_it() {                  # <label>; sets out_ and st_
    out_=$(env "$@" XPOST_DATA_DIR="$src/data" \
        "$xpost" -q --no-sandbox -d null "$work/nostop.ps" </dev/null 2>&1)
    st_=$?
}

run_it
plain_out=$out_
plain_st=$st_

if ! printf '%s\n' "$plain_out" | grep -q "$reached"; then
    echo "FAIL: the program did not reach stop with no stopped context;"
    echo "      this check would report a clean run over a path nobody took."
    echo "      The route is the job error handler in data/init.ps."
    fail=1
elif [ "$plain_st" -ge 128 ]; then
    echo "FAIL: the specified stop-without-stopped path ended the"
    echo "      interpreter on signal $((plain_st - 128)); PLRM 8.2 makes it a"
    echo "      message and a quit."
    fail=1
fi

# The same run with the variables set. They are named because they are
# what this started from; a name nobody has thought of is covered by the
# source rules below rather than by guessing at it here.
for v in XPOST_TRAP_NOSTOP XPOST_TRAP_OOB; do
    run_it "$v=1"
    if [ "$st_" -ge 128 ]; then
        echo "FAIL: $v=1 ended the interpreter on signal $((st_ - 128))"
        echo "      where the same run without it ended with status $plain_st."
        fail=1
    elif [ "$st_" != "$plain_st" ] || [ "$out_" != "$plain_out" ]; then
        echo "FAIL: $v=1 changed what the run did."
        fail=1
    fi
done

# ---- the source rules ----
#
# Both are one pass, so that the fixture below can be put to the same
# code the tree is.
scan() {                    # <root> <file>...; prints a finding per line
    guard_c_source "$@" > "$work/code" 2>/dev/null
    if [ ! -s "$work/code" ]; then
        echo "read-nothing: no C source was read"
        return
    fi
    awk -F: -v enders="$enders" -v win="$window" '
        {
            path[NR] = $1; lno[NR] = $2
            code = $0; sub(/^[^:]*:[0-9]+:/, "", code)
            src[NR] = code
        }
        END {
            for (i = 1; i <= NR; i++) {
                if (src[i] ~ /(^|[^A-Za-z0-9_])getenv[ \t]*\(/) {
                    for (j = i; j <= NR && j <= i + win; j++) {
                        if (path[j] != path[i]) break
                        if (src[j] ~ "(^|[^A-Za-z0-9_])(" enders ")[ \t]*\\(")
                            print "gated:" path[i] ":" lno[i] \
                                  ": an ending at line " lno[j]
                    }
                }
            }
        }' "$work/code"
}

scan "$src"/src/lib/*.c "$src"/src/lib/*.h \
     "$src"/src/bin/*.c "$src"/src/bin/*.h > "$work/found"

if grep -q '^read-nothing:' "$work/found"; then
    echo "FAIL: no C source was read under $src/src; this check would report"
    echo "      a tree with no source in it as a tree in good order."
    fail=1
fi

if grep -q '^gated:' "$work/found"; then
    echo "FAIL: an environment variable decides whether the process ends:"
    sed -n 's|^gated:'"$src"'/||p' "$work/found" | sed 's/^/      /'
    echo "      A variable chooses behaviour; it does not choose to abort."
    fail=1
fi

# abort, wherever it is written in the library
aborts_in() {               # <file>...; prints path:line per call
    guard_c_source "$@" | awk -F: '
        {
            code = $0; sub(/^[^:]*:[0-9]+:/, "", code)
            if (code ~ /(^|[^A-Za-z0-9_])abort[ \t]*\(/) print $1 ":" $2
        }'
}

guard_c_source "$src"/src/lib/*.c "$src"/src/lib/*.h > "$work/libcode"
if [ ! -s "$work/libcode" ]; then
    echo "FAIL: no library source was read under $src/src/lib"
    fail=1
fi
aborts_in "$src"/src/lib/*.c "$src"/src/lib/*.h > "$work/aborts"
if [ -s "$work/aborts" ]; then
    echo "FAIL: the library calls abort:"
    sed "s|^$src/||; s/^/      /" "$work/aborts"
    echo "      An embedded library raises an error; it does not end its host."
    fail=1
fi

# ---- the rules put to a tree that breaks them ----
#
# A scan that reports nothing over a sound tree and a scan that reports
# nothing whatever it is given give the same answer here, and this
# directory has gone green that way before. So the patterns are asked
# about source written to break each rule, in each shape the window has
# to reach, and a rule that does not find its own case is a failure of
# this check rather than of the tree.
# Each getenv below sits on a line whose number is named beside it, and
# each ending is inside the window from it. The gap in `braced` is what
# the window has to be four lines for.
mkdir -p "$work/fixture"
cat > "$work/fixture/broken.c" <<'EOF'
#include <stdlib.h>
/* "abort" in a comment, and "getenv abort" in a string, are not code */
static const char *msg = "getenv abort";
void same_line(void) { if (getenv("A")) abort(); }
void next_line(void)
{
    if (getenv("B"))
        exit(1);
}
void braced(const char *p)
{
    if (getenv("C"))
    {
        /* a comment between the two */
        raise(6);
    }
    (void)p;
}
EOF
scan "$work/fixture/broken.c" > "$work/fixfound"
missed=
for want in 4 7 12; do
    grep -q "^gated:.*:$want:" "$work/fixfound" || missed="$missed line $want"
done
if [ -n "$missed" ]; then
    echo "FAIL: the getenv-gating scan did not find its own fixture case(s):"
    echo "     $missed"
    echo "      The rule reads as satisfied by a tree it cannot see into."
    fail=1
fi
nfix=$(aborts_in "$work/fixture/broken.c" | wc -l | tr -d ' ')
if [ "$nfix" -ne 1 ]; then
    echo "FAIL: the abort scan found $nfix calls in a fixture written with"
    echo "      one; a comment and a string naming abort are not calls."
    fail=1
fi

[ "$fail" = 0 ] || exit 1
echo "SUCCESS (no environment variable ends the process; the specified"
echo "stop-without-stopped path quits with status $plain_st)"
exit 0
