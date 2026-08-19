#!/bin/sh
# Guard: a literal spelling handed to the name-interning constructors is
# resolved where machinery installs, or answers for itself.
#
# Interning a spelling the name arena does not yet hold is a global
# allocation, and the arena is never asked twice for the same text --
# so the allocation lands wherever the spelling's FIRST intern happens
# to run. A call site written as xpost_name_cons(ctx, "spelling") makes
# that placement an accident of control flow: on a path only jobs
# re-enter, the first job to take the path pays the allocation in the
# middle of whatever it was doing, and a gate that reads memory across
# the pages of a job then moves by the spelling's storage on the first
# platform whose configuration takes that path -- and stays flat
# everywhere else. The pattern that closes the class is the table the
# font operators keep (_op_font_names, src/lib/xpost_op_font.c): the
# machinery's own spellings are resolved once as the operators install,
# and what reaches a per-call resolver is only text a program asked
# for, whose first ask pays for it.
#
# WHAT IS DERIVED. Every call in src/lib that hands a string literal to
# xpost_name_cons, xpost_name_cons_n or xpost_name_cons_global, and for
# each one whether a job can re-enter the function it sits in. The
# division is by mechanism, not by a list:
#
#   A job enters C through the operator table, so the job-context roots
#   are every function registered as an operator -- passed to
#   xpost_operator_cons under the (Xpost_Op_Func) cast -- every
#   function whose address is taken anywhere (a pointer this walk
#   cannot follow to its call), and xpost_run, the library's own entry
#   that runs a program. Everything those reach through direct calls is
#   job-reachable; a literal interned in a function they do not reach
#   runs only while the interpreter is being built, which is the
#   install-time class the font table is the pattern for, and is
#   allowed.
#
#   A function that itself installs operators -- one whose own body
#   registers through xpost_operator_cons under the cast, or through
#   xpost_dev_class_install, the funnel a device's method table goes
#   through -- is the operator-install path even when an operator is
#   what reaches it (a device class loads through its load operator). A
#   literal it interns is resolved in the act of installing, which is
#   when the font table resolves, so those sites are allowed too.
#
# Every remaining site is a literal a job's own running can be first to
# intern, and each one must answer for itself in
# tests/name_interning.register: one row per spelling per site,
# carrying a disposition and the reason the site stands. The register
# is held to the derived set in both directions, so a site added
# tomorrow is asked for a row the day it is added, and a row outlives
# its site only until this runs.
#
# What the walk cannot read it refuses rather than passes over: a call
# in a function it would have to classify whose argument list is not
# closed on its own line is a site the line-by-line reading cannot
# name, and fails loudly. (A spelling carrying an escaped quote would
# be misread the same way; none of the machinery's spellings does.)
#
#   $1  path to the source root
set -u
src=${1:?usage: check-name-interning.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$src/tests/name_interning.register" "the interning register"

guard_workdir

register="$src/tests/name_interning.register"

# ---- the sources, read as C ----
# Comments and string contents removed, one line each of
# "<path><tab><line><tab><code>", every line attributed to the function
# whose body it is in, named by the last column-zero line that opens
# one. Openers are tagged D (a definition or declaration, whose mention
# of a name is not a call) and body lines B.
guard_c_source "$src"/src/lib/*.c "$src"/src/lib/*.h \
| awk -F'\t' -v strip="$src/src/lib/" '
    {
        file = $1
        line = $2
        code = substr($0, length($1) + length($2) + 3)
        gsub(/\t/, " ", code)
        sub(strip, "", file)
        if (file != prev) { cur = "@NOFN@"; prev = file }
        if (code ~ /^[A-Za-z_].*\(/)
        {
            fn = code
            sub(/\(.*/, "", fn)
            sub(/[ \t]+$/, "", fn)
            sub(/^.*[ \t*]/, "", fn)
            if (fn != "")
            {
                cur = fn
                print file "\t" cur "\t" line "\tD\t" code
                next
            }
        }
        print file "\t" cur "\t" line "\tB\t" code
    }' > "$work/annot"

if ! grep -q . "$work/annot"; then
    echo "FAIL: nothing was read out of src/lib, so there is no population"
    echo "      and a pass over it proves nothing"
    exit 1
fi

# ---- where the constructors are called ----
# Located on the stripped code, where a mention in a comment or a
# string is not a call; the spelling is then read off the raw line,
# because guard_c_source removes string literals. An occurrence whose
# argument list is not closed on its line is marked rather than
# guessed at.
awk -F'\t' '
    $4 == "B" {
        code = $5
        open = 0
        hit = 0
        while (match(code, /xpost_name_cons(_n|_global)?[ ]*\(/)) {
            rest = substr(code, RSTART)
            code = substr(code, RSTART + RLENGTH)
            hit = 1
            if (rest !~ /\)/) open = 1
        }
        if (hit) print $1 "\t" $2 "\t" $3 "\t" (open ? "OPEN" : "CALL")
    }' "$work/annot" > "$work/calls"

# the spellings, from the raw lines the stripped reading located: every
# quoted string inside a constructor call whose arguments close on the
# line. A call handing no quoted text is handing a variable -- a face
# name, a glyph name -- whose first ask pays for it, outside this rule.
spell_scan() {  # $1 the located calls; then the source files
    awk -F'\t' '
        NR == FNR { want[$1 "\t" $3] = $2 "\t" $4; next }
        {
            fb = FILENAME
            sub(/^.*\//, "", fb)
            key = fb "\t" FNR
            if (!(key in want)) next
            split(want[key], w, "\t")
            code = $0
            while (match(code, /xpost_name_cons(_n|_global)?[ ]*\(/)) {
                rest = substr(code, RSTART + RLENGTH - 1)
                code = substr(code, RSTART + RLENGTH)
                if (rest !~ /\)/) continue
                args = rest
                sub(/\).*/, "", args)
                while (match(args, /"[^"]*"/)) {
                    lit = substr(args, RSTART + 1, RLENGTH - 2)
                    args = substr(args, RSTART + RLENGTH)
                    print fb "\t" w[1] "\t" lit
                }
            }
        }' "$@"
}
spell_scan "$work/calls" "$src"/src/lib/*.c "$src"/src/lib/*.h \
    | LC_ALL=C sort -u > "$work/sites"

# the scanners are put to a case of each kind before their answers are
# believed: a pattern that matches nothing reads as a tree in good order
{
    printf 'p.c\tfn\t1\tB\tx = xpost_name_cons(ctx, );\n'
    printf 'p.c\tfn\t2\tB\tx = xpost_name_cons(ctx, nm);\n'
    printf 'p.c\tfn\t3\tB\tx = xpost_name_cons(ctx,\n'
} > "$work/probe-annot"
awk -F'\t' '
    $4 == "B" {
        code = $5
        open = 0
        hit = 0
        while (match(code, /xpost_name_cons(_n|_global)?[ ]*\(/)) {
            rest = substr(code, RSTART)
            code = substr(code, RSTART + RLENGTH)
            hit = 1
            if (rest !~ /\)/) open = 1
        }
        if (hit) print $1 "\t" $2 "\t" $3 "\t" (open ? "OPEN" : "CALL")
    }' "$work/probe-annot" > "$work/probe-calls"
{
    printf 'x = xpost_name_cons(ctx, "one");\n'
    printf 'x = xpost_name_cons(ctx, nm);\n'
    printf 'x = xpost_name_cons(ctx,\n'
} > "$work/p.c"
nprobe=$(spell_scan "$work/probe-calls" "$work/p.c" | grep -c .)
nopen=$(grep -c 'OPEN$' "$work/probe-calls")
if [ "$nprobe" -ne 1 ] || [ "$nopen" -ne 1 ]; then
    echo "FAIL: the site scanner does not find one literal spelling and one"
    echo "      call split across lines in a probe holding exactly those;"
    echo "      it would pass every site in the tree"
    exit 1
fi

if ! grep -q . "$work/sites"; then
    echo "FAIL: no literal interning call was found anywhere in src/lib;"
    echo "      the constructors were renamed or the reading is broken, and"
    echo "      either way this guard is watching nothing"
    exit 1
fi

# ---- the functions the tree defines, and the calls between them ----
awk -F'\t' '$4 == "D" { print $2 }' "$work/annot" | LC_ALL=C sort -u > "$work/defined"

awk -F'\t' '
    NR == FNR { def[$1] = 1; next }
    $4 == "B" && $2 != "@NOFN@" {
        code = $5
        while (match(code, /[A-Za-z_][A-Za-z0-9_]*[ ]*\(/)) {
            nm = substr(code, RSTART, RLENGTH)
            code = substr(code, RSTART + RLENGTH)
            sub(/[ ]*\($/, "", nm)
            if ((nm in def) && nm != $2) print $2 "\t" nm
        }
    }' "$work/defined" "$work/annot" | LC_ALL=C sort -u > "$work/edges"

# ---- the job-context roots ----
# Functions the operator table dispatches to, taken from their
# registrations; functions whose address is taken, which a pointer can
# carry anywhere; and xpost_run, the entry that runs a program.
awk -F'\t' '
    $4 == "B" && $5 ~ /xpost_operator_cons/ {
        code = $5
        while (match(code, /\(Xpost_Op_Func\)[ ]*[A-Za-z_][A-Za-z0-9_]*/)) {
            nm = substr(code, RSTART, RLENGTH)
            code = substr(code, RSTART + RLENGTH)
            sub(/^\(Xpost_Op_Func\)[ ]*/, "", nm)
            print nm
        }
    }' "$work/annot" | LC_ALL=C sort -u > "$work/oproots"
if ! grep -q . "$work/oproots"; then
    echo "FAIL: no function is registered as an operator anywhere in"
    echo "      src/lib, so the job-context roots are empty and every site"
    echo "      would read as install-time. The registration idiom moved."
    exit 1
fi

awk -F'\t' '
    NR == FNR { def[$1] = 1; next }
    $4 == "B" {
        code = $5
        while (match(code, /[A-Za-z_][A-Za-z0-9_]*/)) {
            nm = substr(code, RSTART, RLENGTH)
            after = substr(code, RSTART + RLENGTH)
            code = after
            if (!(nm in def)) continue
            sub(/^[ ]*/, "", after)
            if (substr(after, 1, 1) != "(") print nm
        }
    }' "$work/defined" "$work/annot" | LC_ALL=C sort -u > "$work/addrtaken"

{ cat "$work/oproots" "$work/addrtaken"; echo xpost_run; } \
    | LC_ALL=C sort -u > "$work/roots"

# ---- what a job can reach ----
awk -F'\t' '
    NR == FNR { reach[$1] = 1; next }
    { ncall++; caller[ncall] = $1; callee[ncall] = $2 }
    END {
        changed = 1
        while (changed) {
            changed = 0
            for (i = 1; i <= ncall; i++)
                if ((caller[i] in reach) && !(callee[i] in reach)) {
                    reach[callee[i]] = 1
                    changed = 1
                }
        }
        for (f in reach) print f
    }' "$work/roots" "$work/edges" | LC_ALL=C sort -u > "$work/jobreach"

# ---- the operator-install path ----
# A body that registers through the cast, or through the device method
# funnel, is installing: a literal it interns is resolved in the act.
awk -F'\t' '
    $4 == "B" && $2 != "@NOFN@" \
    && (($5 ~ /xpost_operator_cons/ && $5 ~ /\(Xpost_Op_Func\)/) \
        || $5 ~ /xpost_dev_class_install[ ]*\(/) { print $2 }
    ' "$work/annot" | LC_ALL=C sort -u > "$work/installers"

# ---- the sites that must answer ----
classify() {  # reads rows whose second field is a function; keeps job ones
    awk -F'\t' '
        FILENAME ~ /jobreach$/ { jr[$1] = 1; next }
        FILENAME ~ /installers$/ { inst[$1] = 1; next }
        ($2 in jr) && !($2 in inst)
        ' "$work/jobreach" "$work/installers" "$1"
}
classify "$work/sites" | LC_ALL=C sort -u > "$work/derived"

# an unreadable call matters exactly where its function would have to
# be classified; elsewhere the site is allowed however it is spelt
classify "$work/calls" | awk -F'\t' '$4 == "OPEN"' > "$work/openbad"
if grep -q . "$work/openbad"; then
    echo "FAIL: an interning call on a job-reachable path is not closed on"
    echo "      its own line, so this reading cannot say what was handed"
    echo "      to it:"
    awk -F'\t' '{ print "      " $1 ":" $3 " in " $2 }' "$work/openbad"
    echo "      Write the call on one line."
    exit 1
fi

ninstall=$(awk -F'\t' '
    FILENAME ~ /jobreach$/ { jr[$1] = 1; next }
    !($2 in jr)' "$work/jobreach" "$work/sites" | grep -c .)
if [ "$ninstall" -lt 1 ]; then
    echo "FAIL: not one literal site was classified as install-time; the"
    echo "      walk is calling the whole tree job-reachable, and a walk"
    echo "      that cannot tell the two apart holds the register to noise"
    exit 1
fi

# ---- the register ----
guard_mirror reg "$register"
awk -v OFS='\t' '
    /^[[:blank:]]*#/ { next }
    /^[[:blank:]]*$/ { next }
    { print $1, $2, $3, $4, NF }' "$mirror/name_interning.register" \
    > "$work/regrows"
bad=0
while IFS="$guard_tab" read -r disp rfile rfn rsp nf; do
    case $disp in
        settled|thorn|heading) ;;
        *)
            echo "FAIL: a register row opens with \"$disp\", which is not a"
            echo "      disposition (settled, thorn or heading): $rfile $rfn $rsp"
            bad=1
            ;;
    esac
    if [ "$nf" -lt 5 ]; then
        echo "FAIL: a register row carries no reason: $rfile $rfn $rsp"
        echo "      a row that does not say why the site stands excuses it"
        echo "      without answering for it"
        bad=1
    fi
done < "$work/regrows"
[ "$bad" -eq 0 ] || exit 1

cut -f2-4 "$work/regrows" | LC_ALL=C sort -u > "$work/regkeys"

guard_held=0
guard_format() { sed 's/^/      /'; }
guard_hold "$work/derived" "$work/regkeys" \
    "a literal spelling is interned on a path a job re-enters, and the
      register does not answer for it. Resolve it where the machinery
      installs -- the pattern is _op_font_names in src/lib/xpost_op_font.c,
      names resolved once as the operators install -- or add a row to
      tests/name_interning.register saying why the site stands
      (file, function, spelling):" \
    "a register row answers for a site the tree no longer holds, and a
      row nothing needs excuses whatever lands there next
      (file, function, spelling):"

[ "$guard_held" -eq 0 ] || exit 1

nsites=$(grep -c . "$work/sites")
njob=$(grep -c . "$work/derived")
echo "SUCCESS ($nsites literal spellings interned: $((nsites - njob)) resolved"
echo "         where machinery installs or reached only while it builds, and"
echo "         $njob on job-reachable paths, each answered for by a register row)"
