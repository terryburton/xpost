#!/bin/sh
# Meson test wrapper: hold the boundary between the language and what a
# run settles.
#
# The interpreter's dictionaries hold the language: the same names with
# the same values however it was started. A few values are not like that
# -- where this run found its boot files, which directories a resource
# search covers, whether there is a user at the other end of standard
# input -- and each is decided afresh on every launch by the command
# line, the environment, the caller, or the state of the process. Those
# live in one dictionary of their own, .hostdict, so that "is this the
# same for every run of this build?" is answered by which dictionary
# holds a name rather than by knowing what the name means.
#
# The failure this exists to stop is quiet. A value that comes from the
# invocation and is kept among the language reads correctly for as long
# as the language and the run are built together; it goes wrong only once
# they are not, and then it goes wrong by answering with something a
# different launch decided, which is a plausible value and not an error.
# Nothing about it looks wrong at the point it is read.
#
# So the register in tests/host_settings.golden is held four ways, and it
# takes all four: any one alone passes for the wrong reason.
#
#   The C table.  xpost_interpreter.c carries host_settings[], the list
#   the interpreter clears and writes on every launch. A name in the
#   register and not in the table is a setting nothing settles -- its
#   reader gets whatever was under the name, which after an image is
#   whatever the machine that took the image had. A name in the table and
#   not in the register is one nothing here holds to anything.
#
#   The readers.  Every literal name written beside .hostdict or
#   .hostvalue in the boot files must be registered. A reader naming
#   something no run settles is asking for a value that will never be
#   there; a writer naming one is inventing a setting the host does not
#   know it has, and that is exactly the value an image would carry.
#
#   A live interpreter.  The register is compared against what a real
#   startup leaves in .hostdict -- neither more nor fewer. The static
#   halves above cannot see this: a name can be declared in the table,
#   registered here, read in the boot files, and still never written,
#   because the branch that writes it was not taken.
#
#   The values, against the invocation.  A member that is present says
#   nothing about where its value came from. So the interpreter is
#   started with a data directory and a resource path this guard chooses,
#   and the values it left behind must be those and in that order. That
#   is what catches a setting that has stopped tracking the run and gone
#   back to answering with a constant.
#
# And three properties of the dictionary itself, in the same live
# startup: a name that is not a setting is refused where it is asked for,
# rather than answered with a null the caller carries off somewhere else;
# the dictionary is still writable once the language has been sealed,
# which is what lets a setting be written afresh for each run rather than
# only at start-up; and no registered name is reachable from the
# language, which is what would let a value be read from two places with
# the wrong one winning.
#
#   $1  path to the xpost binary
#   $2  path to the source tree root
set -u
xpost=${1:?usage: check-host-settings.sh <xpost> <srcroot>}
src=${2:?usage: check-host-settings.sh <xpost> <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
golden="$src/tests/host_settings.golden"
guard_require_file "$golden" "the register of what a run settles"
guard_require_file "$src/src/lib/xpost_interpreter.c" "the interpreter core"

guard_workdir
trap 'rm -rf "$work"' EXIT
fail=0
cr=$(printf '\r')

# ---- the register ----
tr -d "$cr" < "$golden" > "$work/register"
awk '$1 !~ /^#/ && $1 != "entries" && $1 != "" { print $1 }' "$work/register" \
    | sort -u > "$work/registered"
declared=$(awk '$1 == "entries" { print $2; found = 1 }
                END { if (!found) print "" }' "$work/register")
counts=$(awk '$1 == "entries"' "$work/register" | grep -c .)
listed=$(awk '$1 !~ /^#/ && $1 != "entries" && $1 != ""' "$work/register" | grep -c .)

if [ "$counts" -ne 1 ]; then
    echo "FAILURES: $golden must carry exactly one 'entries <n>' line saying"
    echo "      how many settings it registers; it carries $counts"
    exit 1
fi
case "$declared" in
    ''|*[!0-9]*)
        echo "FAILURES: 'entries' takes a count: $declared"
        exit 1 ;;
esac
if [ "$declared" -ne "$listed" ]; then
    echo "FAILURES: $golden declares 'entries $declared' and lists $listed"
    exit 1
fi
if [ "$listed" -eq 0 ]; then
    echo "FAILURES: the register is empty; every check below would agree"
    echo "      with everything by having nothing to look for"
    exit 1
fi
if [ "$(grep -c . "$work/registered")" -ne "$listed" ]; then
    echo "FAILURES: $golden registers a setting twice; the count then holds"
    echo "      fewer settings than it says:"
    awk '$1 !~ /^#/ && $1 != "entries" && $1 != "" { print "      " $1 }' \
        "$work/register" | sort | uniq -d
    exit 1
fi

# ---- the C table ----
# The names are string literals, so they are read from the file itself
# rather than through guard_c_source, which takes literals out. The scan
# is bounded by the table's own braces, so a name in a comment or in a
# message elsewhere in the file is not one of these.
awk '
    /host_settings\[\][[:space:]]*=/ { intable = 1; next }
    intable && /^[[:space:]]*};/     { exit }
    intable {
        line = $0
        while (match(line, /"[^"]*"/)) {
            print substr(line, RSTART + 1, RLENGTH - 2)
            line = substr(line, RSTART + RLENGTH)
        }
    }
' "$src/src/lib/xpost_interpreter.c" | tr -d "$cr" | sort -u > "$work/ctablenames"

if [ ! -s "$work/ctablenames" ]; then
    echo "FAILURES: no host_settings[] table found in src/lib/xpost_interpreter.c;"
    echo "      the register would be held to nothing on the C side"
    exit 1
fi
if ! diff -u "$work/registered" "$work/ctablenames" > "$work/cdiff"; then
    echo "FAILURES: the register and host_settings[] in"
    echo "      src/lib/xpost_interpreter.c name different settings"
    echo "      (- register, + table):"
    sed -n '4,$p' "$work/cdiff" | sed 's/^/      /'
    fail=1
fi

# ---- the readers among the boot files ----
# Read the sources as PostScript: a name inside a comment or a string is
# a mention of a setting and not a use of one.
awk '
    FNR == 1 { sdepth = 0; print "" }
    {
        line = $0
        gsub(/\r/, "", line)
        out = ""
        i = 1
        n = length(line)
        while (i <= n) {
            c = substr(line, i, 1)
            if (sdepth > 0) {
                if (c == "\\") { i += 2; continue }
                if (c == "(") { sdepth++; i++; continue }
                if (c == ")") { sdepth--; i++; continue }
                i++
                continue
            }
            if (c == "%") break
            if (c == "(") { sdepth = 1; i++; continue }
            out = out c
            i++
        }
        print FILENAME ":" FNR ":" out
    }' "$src"/data/*.ps > "$work/code"
if [ ! -s "$work/code" ]; then
    echo "FAILURES: no PostScript found under $src/data; every reader would"
    echo "      go unseen and this check would report agreement"
    exit 1
fi

# A statement is written on one line here, which is what lets the name
# and the dictionary be read off together. The two lines that define
# .hostdict and .hostvalue name only themselves and so contribute none.
grep -E '\.hostdict|\.hostvalue' "$work/code" > "$work/reads" || :
if [ ! -s "$work/reads" ]; then
    echo "FAILURES: nothing in $src/data reaches .hostdict at all; the"
    echo "      settings would be registered and never read"
    exit 1
fi
awk -F: '{
    where = $1 ":" $2
    line = $0
    sub(/^[^:]*:[0-9]*:/, "", line)
    n = split(line, tok, /[ \t{}\[\]]+/)
    for (i = 1; i <= n; i++)
        if (tok[i] ~ /^\/[^][(){}<>\/%]+$/ &&
            tok[i] != "/.hostdict" && tok[i] != "/.hostvalue")
            print substr(tok[i], 2) " " where
}' "$work/reads" > "$work/readnames"

while read -r name where; do
    [ -n "$name" ] || continue
    if ! grep -qxF "$name" "$work/registered"; then
        echo "UNREGISTERED setting: /$name reached at $where"
        echo "      a setting no run makes: add it to tests/host_settings.golden"
        echo "      and to host_settings[] in this commit, or stop naming it here"
        fail=1
    fi
done < "$work/readnames"

# ---- a live interpreter, and the values it settled ----
cat > "$work/probe.ps" <<'PSEOF'
% .hostdict is inside the private namespace, which cannot be named after
% lockdown. Recover the namespace the way tamper_dispatch_test.ps does:
% privatedict anchors bound procedures, and those carry the dictionary
% baked into them by their // references.
/found null def
/probe { 2 dict begin /d exch def /o exch cvlit def
  d 0 gt found null eq and {
    o type /dicttype eq { { o /.strcat known { /found o store } if } stopped pop }
    { o type /arraytype eq { o rcheck {
        0 1 o length 1 sub { o exch get d 1 sub probe } for } if } if } ifelse
  } if end } def
.privatedict { exch pop dup type /arraytype eq { 6 probe }{ pop } ifelse } forall
found null eq { (bad recovered\n) print quit } if
found /.hostdict known not { (bad recovered\n) print quit } if
(ok recovered\n) print
/H found /.hostdict get def

% what it holds
H { pop dup type /nametype eq { (member ) print 60 string cvs print (\n) print }
    { pop } ifelse } forall

% the two values this run was started with, written out for the caller
% to compare against what it asked for
H /DATA_DIR get dup null eq { pop (datadir -\n) print }
    { (datadir ) print print (\n) print } ifelse
H /.resourcepath get dup null eq { pop }
    { { (respath ) print print (\n) print } forall } ifelse
H /.interactive get { (interactive yes\n) }{ (interactive no\n) } ifelse print

% a name that is not a setting is refused where it is asked for, rather
% than answered with a null the caller would carry off somewhere else
found /.hostvalue get /HV exch def
mark { /.nosuchsettinghere HV exec } stopped
    { $error /errorname get /undefined eq
        { (ok unsetraises\n) }{ (bad unsetraises\n) } ifelse }
    { (bad unsetraises\n) } ifelse print
cleartomark

% the dictionary is still writable once the language has been sealed:
% the namespace holding it is read-only by then, and it is the seal
% being shallow that lets a setting be written afresh for each run the
% context serves rather than only at start-up
H wcheck { (ok writable\n) }{ (bad writable\n) } ifelse print

% and none of them is reachable from the language, which is what would
% let a value be read from two places with the wrong one winning
/leaked 0 def
H { pop dup type /nametype eq {
      dup systemdict exch known { /leaked leaked 1 add store } if
      dup .privatedict exch known { /leaked leaked 1 add store } if
      dup 1183615869 internaldict exch known { /leaked leaked 1 add store } if
      dup where { pop /leaked leaked 1 add store } if
    } if pop } forall
leaked 0 eq { (ok unreachable\n) }{ (bad unreachable\n) } ifelse print
PSEOF

# Directories this guard chose, so that a value which has stopped
# tracking the run shows up as the wrong answer rather than as a
# plausible one.
inc1="$work/resources-one"
inc2="$work/resources-two"
mkdir -p "$inc1" "$inc2"
XPOST_DATA_DIR="$src/data" "$xpost" -q --no-sandbox -d null -o /dev/null \
    "-I$inc1" "-I$inc2" "$work/probe.ps" </dev/null 2>/dev/null \
    | tr -d "$cr" > "$work/out"

if ! grep -qx 'ok recovered' "$work/out"; then
    echo "FAILURES: the live startup did not yield a .hostdict to read;"
    echo "      every check below it would have nothing to look at"
    sed 's/^/      /' "$work/out"
    exit 1
fi

for probe in unsetraises writable unreachable; do
    if ! grep -qx "ok $probe" "$work/out"; then
        case "$probe" in
        unsetraises) what="a name that is not a setting is refused where it is asked for" ;;
        writable)    what=".hostdict is still writable once the language is sealed" ;;
        unreachable) what="no setting is reachable from the language" ;;
        esac
        echo "FAILURES: $what"
        fail=1
    fi
done

sed -n 's/^member //p' "$work/out" | sort -u > "$work/live"
if ! diff -u "$work/registered" "$work/live" > "$work/livediff"; then
    echo "FAILURES: the register and a live startup's .hostdict hold"
    echo "      different settings (- register, + live):"
    sed -n '4,$p' "$work/livediff" | sed 's/^/      /'
    fail=1
fi

got=$(sed -n 's/^datadir //p' "$work/out")
if [ "$got" != "$src/data" ]; then
    echo "FAILURES: the run was started with its data directory at"
    echo "      $src/data and settled DATA_DIR as ${got:-nothing}"
    fail=1
fi

printf '%s\n%s\n' "$inc1" "$inc2" > "$work/wantpath"
sed -n 's/^respath //p' "$work/out" > "$work/gotpath"
if ! diff -u "$work/wantpath" "$work/gotpath" > "$work/pathdiff"; then
    echo "FAILURES: the run was given two resource directories and settled a"
    echo "      different path (- asked for, + settled):"
    sed -n '4,$p' "$work/pathdiff" | sed 's/^/      /'
    fail=1
fi

if ! grep -qx 'interactive no' "$work/out"; then
    echo "FAILURES: the run read its program from a file with standard input"
    echo "      closed and settled that it has a user at the other end"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "host-settings: the boundary in tests/host_settings.golden no longer holds."
    exit 1
fi
echo "SUCCESS ($listed settings registered, held to the C table, the readers"
echo "      in the boot files, and a live startup)"
exit 0
