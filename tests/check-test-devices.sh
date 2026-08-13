#!/bin/sh
#
# Guard what the tests do with the machine they are run on: a test names
# the device it runs the interpreter on, and a test that wants a window
# opens it on a display it conjured.
#
# Both rules are about the same thing, which is that a test's subject is
# this tree and not the machine underneath it.
#
#   A run with no device named takes the build's, and the build's is
#   whichever the libraries found allowed: a window system's device where
#   the X headers were present, a raster device where they were not. So
#   the same test is a different test on two machines, and on the one
#   where the developer is sitting it puts a window on their screen,
#   takes the pointer and the keyboard focus, and does it once per
#   invocation. The lanes that build this have no display, which is why
#   nothing there ever says so.
#
#   A run that does want the window device wants a display to open it on,
#   and the display the run was started with belongs to whoever is
#   sitting at it. One conjured for the run belongs to the run: it is the
#   same display on every machine, it is empty, and it goes away with the
#   test. So the window devices go through xvfb-run, and no test reads
#   the DISPLAY it was handed -- reading it is how a test comes to have
#   two behaviours, one of which nobody who runs the lanes ever sees.
#
# What the guard reads is the shell of the tests, so it holds a device
# named where the test is written. A device name that only exists at run
# time -- one read out of the interpreter's own list, say -- is past what
# this can see, and the second rule is what covers those: a test that
# never reads DISPLAY has no display of the developer's to open on, and
# what a conjured one shows is nobody's screen.
#
# The interpreter's options are read from src/bin/xpost_main.c rather
# than written here. An option added there joins this the day it is
# written, and an option that stops reading arguments and reports --
# there are three, and they never reach a device -- is recognised by the
# same reading rather than by a list that would go stale beside it.
#
# Usage: check-test-devices.sh <source root>

set -eu

src=${1:?usage: check-test-devices.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

guard_workdir
trap 'rm -rf "$work"' EXIT
# read a tree whose lines end where the scans below expect them to
guard_mirror_tree "$src"
src=$mirror

testdir="$src/tests"
guard_require_dir "$testdir" "the test directory"
main="$src/src/bin/xpost_main.c"
guard_require_file "$main" "the interpreter's command line"

fail=0

# ---- what the interpreter's options are -----------------------------
#
# Three lists, all read from the one file that parses them:
#
#   opts      an option, matched whole
#   prefixes  an option carrying its value, matched by its start
#   stops     an option after which the program reports and returns,
#             so no device is ever made and none need be named
#
# The device option is picked out of the same reading, in both its
# spellings, so that a rename moves this with it.
awk '
    { line = $0 }
    # a fresh condition starts a fresh set of names
    line ~ /(^|[^a-zA-Z_])if[ \t]*\(/ { pending = "" }
    {
        rest = line
        while (match(rest, /strn?cmp\(argv\[i\], "[^"]*"/)) {
            piece = substr(rest, RSTART, RLENGTH)
            rest = substr(rest, RSTART + RLENGTH)
            if (!match(piece, /"[^"]*"/)) continue
            name = substr(piece, RSTART + 1, RLENGTH - 2)
            if (substr(name, 1, 1) != "-") continue
            if (piece ~ /strncmp/) print "PREFIX\t" name
            else print "OPT\t" name
            pending = pending " " name
        }
        rest = line
        while (match(rest, /XPOST_MAIN_IF_OPT\("[^"]*", "[^"]*", [a-z_]*\)/)) {
            piece = substr(rest, RSTART, RLENGTH)
            rest = substr(rest, RSTART + RLENGTH)
            nf = split(piece, part, /"/)
            if (nf < 5) continue
            short = part[2]; long = part[4]
            print "OPT\t" short
            print "PREFIX\t" long
            if (piece ~ /, device\)/) {
                print "DEVSHORT\t" short
                print "DEVLONG\t" long
            }
        }
    }
    line ~ /return EXIT_SUCCESS;/ && pending != "" {
        n = split(pending, w, /[ \t]+/)
        for (k = 1; k <= n; k++)
            if (w[k] != "") print "STOP\t" w[k]
        pending = ""
    }
' "$main" > "$work/vocab"

awk -F'\t' '$1 == "OPT"      { print $2 }' "$work/vocab" | sort -u > "$work/opts"
awk -F'\t' '$1 == "PREFIX"   { print $2 }' "$work/vocab" | sort -u > "$work/prefixes"
awk -F'\t' '$1 == "STOP"     { print $2 }' "$work/vocab" | sort -u > "$work/stops"
awk -F'\t' '$1 == "DEVSHORT" { print $2 }' "$work/vocab" | sort -u > "$work/devshort"
awk -F'\t' '$1 == "DEVLONG"  { print $2 }' "$work/vocab" | sort -u > "$work/devlong"

nopt=$(grep -c . "$work/opts" || true)
nprefix=$(grep -c . "$work/prefixes" || true)
nstop=$(grep -c . "$work/stops" || true)
ndevshort=$(grep -c . "$work/devshort" || true)
ndevlong=$(grep -c . "$work/devlong" || true)

# Every one of these is what the scan below recognises a command by. A
# reading that came back empty would leave it recognising nothing and
# reporting a clean tree, so each is held to having found something.
if [ "$nopt" -eq 0 ] || [ "$nprefix" -eq 0 ]; then
    echo "FAILURES: no command line options were read out of" >&2
    echo "      src/bin/xpost_main.c ($nopt whole, $nprefix carrying a value);" >&2
    echo "      nothing below would recognise an invocation. Fix the guard" >&2
    exit 1
fi
if [ "$nstop" -eq 0 ]; then
    echo "FAILURES: no option that reports and stops was read out of" >&2
    echo "      src/bin/xpost_main.c, so every probe that asks the" >&2
    echo "      interpreter what it is would be read as a run. Fix the guard" >&2
    exit 1
fi
if [ "$ndevshort" -eq 0 ] || [ "$ndevlong" -eq 0 ]; then
    echo "FAILURES: the device option was not read out of" >&2
    echo "      src/bin/xpost_main.c in both its spellings; the rule below" >&2
    echo "      would be held over the wrong word. Fix the guard" >&2
    exit 1
fi

# ---- the tests, as commands -----------------------------------------
#
# Every shell file under tests/, cut into commands. Line continuations
# are joined first, then the text is walked a character at a time so that
# a separator inside quotes stays inside them: the wrappers pass whole
# command lines to helpers as single arguments, and a command written
# that way is still a command.
#
# Quoting is taken a line at a time. A string that runs over a line end
# is left half-read either way, and the choice is between a guard that
# reads the rest of that one line as shell and a guard that reads the
# rest of the file as a string -- the first misreads a line, the second
# goes quiet over everything after it.
find "$testdir" -name '*.sh' -type f -print | sort > "$work/files"
nfiles=$(grep -c . "$work/files" || true)
if [ "$nfiles" -eq 0 ]; then
    echo "FAILURES: no shell file was found under $testdir; there is" >&2
    echo "      nothing here to hold to anything" >&2
    exit 1
fi

xargs awk '
    function flush(   t) {
        t = seg
        sub(/^[ \t]+/, "", t)
        sub(/[ \t]+$/, "", t)
        if (t != "") print FILENAME "\t" segline "\t" t
        seg = ""; segline = 0
    }
    FNR == 1 { seg = ""; segline = 0; held = ""; heldline = 0 }
    {
        line = $0
        sub(/\r$/, "", line)
        start = FNR
        if (held != "") { line = held " " line; start = heldline }
        if (line ~ /\\$/) {
            sub(/\\$/, "", line)
            held = line; heldline = start
            next
        }
        held = ""
        q = ""
        n = length(line)
        i = 1
        while (i <= n) {
            c = substr(line, i, 1)
            two = substr(line, i, 2)
            if (q != "") {
                if (q == "\"" && c == "\\") { seg = seg two; i += 2; continue }
                if (c == q) q = ""
                seg = seg c; i++
                continue
            }
            if (c == "\047" || c == "\"") { q = c; seg = seg c; i++
                if (segline == 0) segline = start
                continue }
            if (c == "#" && (seg == "" || seg ~ /[ \t]$/)) break
            if (two == "&&" || two == "||" || two == "$(") { flush(); i += 2; continue }
            if (c == "|" || c == ";" || c == "&" || c == "\140" || c == ")") {
                flush(); i++; continue
            }
            if (c != " " && c != "\t" && segline == 0) segline = start
            seg = seg c; i++
        }
        flush()
    }
    END { flush() }
' < "$work/files" > "$work/commands"

ncommands=$(grep -c . "$work/commands" || true)
if [ "$ncommands" -eq 0 ]; then
    echo "FAILURES: the $nfiles shell files under $testdir yielded no" >&2
    echo "      command at all; the reading below would hold nothing" >&2
    exit 1
fi

# ---- the two rules --------------------------------------------------
#
# A command is the interpreter being run when it carries at least one of
# the interpreter's options and any of these is true of it:
#
#   it names the interpreter -- the variable every wrapper takes it in,
#   or a path ending in its name;
#
#   the word it runs is one of the running script's own arguments, which
#   is how a wrapper is handed the interpreter and the shape a run
#   through a differently named variable takes. What such a word runs
#   cannot be read here, so it is held to the rule rather than passed
#   over;
#
#   it names a device and carries a second, different option of the
#   interpreter's. A command shaped like that is running something with
#   this interpreter's command line whatever it is called, which is what
#   reaches a run the two readings above cannot see: the differential
#   renderer hands the interpreter to a function and runs it from the
#   function's own parameter, several words into the command.
#
# The last of the three is how the display rule reaches a run this cannot
# name; it cannot be how the device rule does, since it is a reading that
# starts from a device having been named. A run that names no device, is
# named through a variable this cannot see, and is not the running
# script's own argument, is past what a reading of the shell can do. The
# rule after this one is what covers it: a suite that never reads the
# display it was handed has no screen of the developer's to reach.
#
# Reported per command, with a count of everything examined, so an
# emptied tree fails here rather than passing with nothing to say.
awk -F'\t' -v OFS='\t' '
    function isopt(t,   k) {
        if (t in OPT) return t
        for (k in PREFIX) if (index(t, k) == 1 && t != k) return k
        return ""
    }
    function unquote(t) {
        gsub(/["\047]/, "", t)
        return t
    }
    FILENAME == optf    { OPT[$0] = 1; next }
    FILENAME == prefixf { PREFIX[$0] = 1; next }
    FILENAME == stopf   { STOP[$0] = 1; next }
    FILENAME == dshortf { DSHORT[$0] = 1; next }
    FILENAME == dlongf  { DLONG[$0] = 1; next }
    {
        file = $1; lineno = $2
        cmd = substr($0, length($1) + length($2) + 3)
        n = split(cmd, tok, /[ \t]+/)
        named = 0; stops = 0; dev = ""; hasdev = 0; xvfb = 0; runvar = 0
        ndistinct = 0
        delete seen
        word = ""
        for (k = 1; k <= n; k++) {
            t = tok[k]
            if (word == "") {
                if (t ~ /^[A-Za-z_][A-Za-z0-9_]*=/) continue
                if (t ~ /^(if|then|else|elif|do|while|until|!|exec|eval|command|time)$/) continue
                word = t
                if (t ~ /^["\047]?\$\{?[0-9@]\}?["\047]?$/) runvar = 1
            }
            if (t ~ /\$\{?xpost\}?([^A-Za-z0-9_]|$)/) named = 1
            else if (t ~ /\$\{?XPOST\}?([^A-Za-z0-9_:}]|$)/) named = 1
            else if (t ~ /\/xpost["\047]?$/) named = 1
            if (t ~ /(^|\/)xvfb-run$/) xvfb = 1
            if (substr(t, 1, 1) != "-") continue
            if (t in STOP) stops = 1
            o = isopt(t)
            if (o != "" && !(o in seen)) {
                seen[o] = 1
                ndistinct++
            }
            for (d in DSHORT) if (t == d) {
                hasdev = 1
                if (k < n) dev = unquote(tok[k + 1])
            }
            for (d in DLONG) if (index(t, d) == 1) {
                hasdev = 1
                dev = unquote(substr(t, length(d) + 1))
            }
        }
        if (ndistinct < 1) next
        if (!(named || runvar || (hasdev && ndistinct >= 2))) next
        examined++
        if (stops) { reports++; next }
        if (!hasdev) {
            printf "check-test-devices: %s:%s runs the interpreter and names no\n", file, lineno
            print  "device, so it runs on whichever one this build was configured"
            print  "with -- a window on the developer\047s screen where the window"
            print  "system was found. Name the device the test means to use."
            print  "      " cmd
            bad = 1
            next
        }
        named_dev++
        if (dev !~ /^xcb($|:)/) next
        windowed++
        if (!xvfb) {
            printf "check-test-devices: %s:%s opens the window device on the\n", file, lineno
            print  "display the run was started with, which is somebody\047s screen."
            print  "Run it under xvfb-run, which conjures one of its own."
            print  "      " cmd
            bad = 1
        }
    }
    END {
        printf "examined %d interpreter commands: %d named a device, %d report and stop, %d open a window\n",
               examined, named_dev, reports, windowed
        if (examined == 0 || named_dev == 0) {
            print "FAILURES: no test was found running the interpreter at all;"
            print "      the rule above was held over nothing. Fix the guard"
            exit 1
        }
        if (windowed == 0) {
            print "FAILURES: no test was found opening the window device, so"
            print "      the display rule was held over nothing. Fix the guard"
            exit 1
        }
        exit bad ? 1 : 0
    }
' optf="$work/opts" prefixf="$work/prefixes" stopf="$work/stops" \
  dshortf="$work/devshort" dlongf="$work/devlong" \
  "$work/opts" "$work/prefixes" "$work/stops" "$work/devshort" "$work/devlong" \
  "$work/commands" || fail=1

# ---- and no test reads the display it was handed ---------------------
#
# The rule above is about a command that names the window device where it
# is written. A test that reads DISPLAY is choosing between two ways to
# run at the time it runs, and the way it chooses on a machine with a
# display is the one no lane ever takes.
displays=$(xargs grep -nE '[$][{]?DISPLAY' < "$work/files" 2>/dev/null || true)
if [ -n "$displays" ]; then
    echo "check-test-devices: these read the display the run was started" >&2
    echo "with, which is a test whose behaviour depends on who ran it:" >&2
    printf '%s\n' "$displays" | sed 's/^/      /' >&2
    echo "A test that wants a display conjures one with xvfb-run." >&2
    fail=1
fi

[ "$fail" = 0 ] || { echo "FAILURES: the tests above" >&2; exit 1; }
echo "SUCCESS"
