#!/bin/sh
# Guard the properties a test must have to be able to fail.
#
# Every rule here encodes a defect found in this suite, each of which let
# tests report success over broken code:
#
#   1. A failure counter incremented with `def` inside a procedure that
#      opens a scratch dictionary writes the scratch dictionary; `end`
#      discards it. Such a test prints its FAIL lines and then reports
#      SUCCESS. `store` writes the binding where the name is defined.
#   2. A wrapper that captures the interpreter's output without its exit
#      status passes a run that printed SUCCESS and then crashed. The
#      rule used to apply only to wrappers that mentioned $xpost, so a
#      wrapper that ran nothing at all was outside it: one whose whole
#      body was `exit 0` passed.
#   3. A wrapper that accepts a golden file, register or list without
#      requiring it to be non-empty passes when given nothing to check.
#   4. A test file with nothing in it, or nothing but comments, runs
#      clean. So does one whose content was commented out.
#   5. A guard that is not executable runs only because meson falls back
#      to the shebang, and not at all through any other route.
#   6. A wrapper that looks for SUCCESS anywhere in a run's output passes
#      a run that printed a failure and then printed SUCCESS, and one
#      that reads only what a run left behind passes a run that wrote
#      every one of those and then died on the way out. Both rules live
#      in tests/verdict.sh so there is one of each, and every wrapper the
#      directory holds reaches one of them.
#   7. A C test reports through its exit status, so the same hole one
#      layer down is a path that prints a failure and returns zero: a
#      failure printed without being recorded, or a status answered
#      without reading the record. Each of the eighteen C tests carried
#      its own counter, its own assertion and its own verdict, so each
#      was a place the pair could come apart. The rule lives in
#      tests/xpost_test.h so there is one of it.
#   8. A status read off a pipeline is the last stage's. A wrapper that
#      pipes the interpreter into a text filter and then reads $? is
#      answered by the filter, which reports on its own reading rather
#      than on what it was reading, so a run that died passes.
#   9. A bracket expression in a sed script is a set of characters, and
#      a backslash in one is a character: where \t in a bracket is read
#      as a backslash and a letter rather than as a tab, a scan that
#      trims blanks that way takes a letter off the words it trims, and
#      the guard counts a population the host it ran on gave it.
#
# This check reads a directory, so it says which directory it will
# accept. It used to accept any: pointed at a directory that does not
# exist, at an empty one, at the source root, at data/ or at a build
# tree, it found no test to complain about and reported SUCCESS -- the
# same answer it gives for a suite in good order.
#
# Usage: check-test-quality.sh <tests directory>

set -eu
dir=${1:?usage: check-test-quality.sh <tests directory>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_dir "$dir" "the tests directory"
guard_require_file "$dir/guard-paths.sh" "the guard path helper"
guard_require_file "$dir/verdict.sh" "the verdict helper"
guard_require_file "$dir/xpost_test.h" "the C-test verdict helper"

count_of() {
    n=0
    for f in "$@"; do
        [ -e "$f" ] && n=$((n + 1))
    done
    echo "$n"
}
nps=$(count_of "$dir"/*.ps)
nrun=$(count_of "$dir"/run-*.sh)
ncheck=$(count_of "$dir"/check-*.sh)
nc=$(count_of "$dir"/*.c)
if [ "$nps" -lt 50 ] || [ "$nrun" -lt 15 ] || [ "$ncheck" -lt 15 ] \
   || [ "$nc" -lt 15 ]; then
    echo "FAILURES: $dir holds $nps .ps tests, $nc C tests, $nrun wrappers and"
    echo "      $ncheck guards; that is not the tests directory, or the suite"
    echo "      has collapsed"
    exit 1
fi

guard_workdir
trap 'rm -rf "$work"' EXIT

fail=0

# 1. scoped counter increments
for f in "$dir"/*.ps; do
    [ -e "$f" ] || continue
    grep -q 'dict begin' "$f" || continue
    if grep -qE '/(failcount|nbad|nunbound|failures)[a-z]* +[a-z]+ +1 add def' "$f"; then
        echo "FAIL: $(basename "$f") increments a failure counter with def;"
        echo "      a scratch dictionary would swallow it -- use store"
        fail=1
    fi
done

# 2. wrappers must run what they were handed, and consult its exit status
for f in "$dir"/run-*.sh; do
    [ -e "$f" ] || continue
    if ! grep -qE '\$\{?[1-9@]|\$\{?[A-Za-z_]+:\?' "$f"; then
        echo "FAIL: $(basename "$f") never uses what it was handed -- it cannot"
        echo "      be running the thing under test"
        fail=1
    fi
    if ! grep -qE 'status=\$\?|st=\$\?|\$\? -ne 0|\|\| (exit|fail)|rc=\$\?|ret=\$\?|verdict_run[^|]*\$\?' "$f"; then
        echo "FAIL: $(basename "$f") runs the interpreter without checking its"
        echo "      exit status -- a crash after SUCCESS would pass"
        fail=1
    fi
done

# 3. guards fed a golden/register file must require content
for f in "$dir"/check-*.sh "$dir"/run-golden-render.sh; do
    [ -e "$f" ] || continue
    # only scripts that read such a file, not ones that mention one
    grep -qE '(<|\bread .*<) *"\$(golden|manifest)"|grep [^|]*"\$(golden|manifest)"' "$f" || continue
    if ! grep -qE '! -s|guard_require_file' "$f"; then
        echo "FAIL: $(basename "$f") accepts its golden input without requiring"
        echo "      it to be non-empty -- an empty file would pass vacuously"
        fail=1
    fi
done

# 4. a test file has to have something in it
for f in "$dir"/*.ps; do
    [ -e "$f" ] || continue
    if ! sed 's/%.*//' "$f" | grep -q '[^[:space:]]'; then
        echo "FAIL: $(basename "$f") is empty or is nothing but comments;"
        echo "      it runs clean because it does not run"
        fail=1
    fi
done

# 5. a guard has to be runnable as itself
for f in "$dir"/check-*.sh "$dir"/run-*.sh; do
    [ -e "$f" ] || continue
    if [ ! -x "$f" ]; then
        echo "FAIL: $(basename "$f") is not executable; it runs only where"
        echo "      something falls back to its shebang for it"
        fail=1
    fi
done

# 6. every wrapper judges its run by the one rule, and 8. reads the
#    status of the process it ran
#
# A wrapper that decides for itself what makes a run a pass leaves out
# one of the two halves, and which half depends on how the run answers. A
# run that prints its own verdict has already failed if it printed a
# failure before it, whatever it went on to conclude; a run that reports
# through its exit status has already failed if it complained its way to
# a clean exit, and equally if it wrote every artifact the wrapper reads
# and then died on the way out. tests/verdict.sh carries both halves of
# both, as verdict_ok and verdict_run; a wrapper reaches one of them.
#
# The population is derived rather than listed: every shell file the
# directory holds that is not one of the check-*.sh guards, which report
# verdicts of their own rather than judging a run's. A wrapper written
# next year is inside it without anyone remembering to put it there. What
# is outside is named below with the reason it is outside, the list is
# held to its length, and an exemption naming something the directory
# does not hold is a failure too -- so the list cannot outlive what it
# excuses, and cannot grow without the number moving with it.
verdict_exempt='verdict.sh guard-paths.sh device-fleet.sh run-profile.sh'
verdict_exempt_n=4
#   verdict.sh       the rule itself
#   guard-paths.sh   the path helper the guards source
#   device-fleet.sh  the device roster the wrappers source
#   run-profile.sh   drives meson over a selection of the suite rather
#                    than running the interpreter; the runs it starts
#                    report to meson, which is what it reads back

# The two rules as one pass over a wrapper, so the self-check below can
# put cases to them rather than trusting that patterns which find nothing
# in a sound directory would have found something in an unsound one.
wrapper_faults() {          # <file>; prints a line per fault, else nothing
    tr -d '\r' < "$1" | sed 's/#.*//' > "$work/w-body"
    # a pipeline written over several lines is one line to look at
    awk '{ while (sub(/\\$/, "")) { if ((getline nxt) <= 0) break; $0 = $0 nxt }
           print }' "$work/w-body" > "$work/w-joined"

    if ! grep -q 'verdict_ok\|verdict_run' "$work/w-joined"; then
        echo "judges its run without verdict_ok or verdict_run"
    elif ! grep -q 'verdict\.sh' "$work/w-joined"; then
        echo "reaches for a verdict function without sourcing verdict.sh"
    fi

    # A status read off a pipeline is the last stage's. `xpost | tail`
    # followed by a status read answers for tail, which reports on its
    # own reading and not on what it was reading, so a run that died
    # passes. What is looked for is the last stage being a text filter:
    # a stage that is a subshell or a group around the thing under test
    # is the status of the thing under test.
    awk '
        /\$\?/ && prev ~ /\|/ {
            t = prev
            gsub(/\|\|/, "\001", t)
            n = split(t, part, "|")
            if (n > 1 && part[n] ~ /^[[:space:]]*(grep|head|tail|sed|awk|tr|cut|sort|uniq|wc|od|xxd|cmp|diff|cat|tee)([[:space:]]|$)/)
                print "line " FNR ": reads a status off a pipeline whose last" \
                      " stage is a text filter"
        }
        { if ($0 ~ /[^[:space:]]/) prev = $0 }' "$work/w-joined"
}

for f in "$dir"/*.sh; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    case $base in check-*.sh) continue ;; esac
    case " $verdict_exempt " in *" $base "*) continue ;; esac
    faults=$(wrapper_faults "$f")
    [ -n "$faults" ] || continue
    echo "FAIL: $base does not hold its run to the rule in tests/verdict.sh --"
    echo "      judge a run that prints a verdict with verdict_ok, and one"
    echo "      that reports through its exit status with verdict_run:"
    printf '%s\n' "$faults" | sed 's/^/      /'
    fail=1
done

n=0
for base in $verdict_exempt; do
    n=$((n + 1))
    if [ ! -e "$dir/$base" ]; then
        echo "FAIL: $base is exempt from the verdict rule and is not there;"
        echo "      an exemption that excuses nothing has outlived its reason"
        fail=1
    fi
done
if [ "$n" -ne "$verdict_exempt_n" ]; then
    echo "FAIL: $n wrappers are exempt from the verdict rule where"
    echo "      $verdict_exempt_n are declared; a list that grows without its"
    echo "      length moving with it is a list nobody reviewed"
    fail=1
fi

# 7. a C test's printed failure has to reach its exit status
#
# A C test has no wrapper reading its output: the status is the whole
# report, so a failure printed on a path that goes on to return zero is
# a failure nobody is ever told about. Two things have to hold for it not
# to be -- the failure has to be recorded where it is printed, and the
# status has to be read off the record -- and a test that arranges either
# for itself is a test that can be written not to. tests/xpost_test.h
# holds both, so neither is the test's to arrange.
#
# What is checked here is the two ways around it rather than the include:
# a test that prints a failure of its own is not recording one, and a
# main that answers with anything but the verdict is not reading the
# record. A test holding nothing to anything is the third way, and is
# rule 4 for the C half.
#
# The file is read as C, so a failure word in a comment or in a string
# the test compares against is not a finding -- only one being printed.
# What the rule catches is that spelling: a test that assembled the word
# somewhere else and printed the result would be outside it, and would be
# a new thing to review rather than a variation on this.
#
# Every word boundary below is a bracket rather than an anchor, and the
# line is padded on both ends so the bracket has something to match at
# either edge. A caret means "the start of the line" only at the start of
# an expression, and a dollar the end of one only at the end; inside an
# alternation both are undefined, and a matcher that reads them as
# neither an anchor nor a literal answers about no line at all -- a rule
# that never fires, over a suite that then looks to be in good order.
ctest_faults() {            # <file>; prints a line per fault, else nothing
    guard_c_source "$1" | cut -f3- | sed 's/^/ /; s/$/ /' \
        > "$work/ct-code"
    tr -d '\r' < "$1" > "$work/ct-raw"

    # a failure printed by hand: a print call in the code, on a line
    # whose text carries the word a failure is read by
    awk 'NR == FNR { code[FNR] = $0; next }
         code[FNR] ~ /[^A-Za-z_](printf|fprintf|puts|fputs)[[:space:]]*\(/ &&
         $0 ~ /"[^"]*(FAIL|MISMATCH)/ {
             print "line " FNR ": prints a failure nothing records"
         }' "$work/ct-code" "$work/ct-raw"

    # a status that did not come from the record
    awk '
        !inmain && /^[[:space:]]*[A-Za-z_][^;]*[^A-Za-z_]main[[:space:]]*\(/ {
            inmain = 1; depth = 0; opened = 0
        }
        inmain {
            if ($0 ~ /[^A-Za-z_]return[^A-Za-z_]/ &&
                $0 !~ /return[[:space:]]*verdict\(\)/)
                print "line " FNR ": main answers with something other than verdict()"
            if ($0 ~ /[^A-Za-z_]exit[[:space:]]*\(/)
                print "line " FNR ": main leaves through exit(), around the verdict"
            t = $0; o = gsub(/\{/, "", t)
            t = $0; c = gsub(/\}/, "", t)
            depth += o - c
            if (o) opened = 1
            if (opened && depth <= 0) inmain = 0
        }' "$work/ct-code"

    # a test that holds nothing to anything
    if ! grep -qE '[^A-Za-z_](check|report_failure)[[:space:]]*\(' \
            "$work/ct-code"; then
        echo "the file asserts nothing; it runs clean because it does not run"
    fi
}

for f in "$dir"/*.c; do
    [ -e "$f" ] || continue
    faults=$(ctest_faults "$f")
    [ -n "$faults" ] || continue
    echo "FAIL: $(basename "$f") can print a failure its exit status does not"
    echo "      carry -- report through check/report_failure and answer with"
    echo "      verdict(), from tests/xpost_test.h:"
    printf '%s\n' "$faults" | sed 's/^/      /'
    fail=1
done

# 9. a scan reads the same population wherever it runs
#
# A bracket expression in a sed script is a set of characters, and a
# backslash inside one is a character rather than an escape: one sed
# reads \t there as a tab, another as the backslash and the letter t.
# A scan written that way takes a t off the end of every word it trims
# on the second, and reports about the population that leaves without
# saying it read a different one -- a guard whose count depends on which
# host ran it. A blank is written [[:blank:]], and a tab outside a
# bracket is read as one by either.
#
# Read on the lines that invoke sed, which is where every sed script in
# this suite is written; a bracket earlier on the line belongs to
# whatever feeds the invocation, and a line that is all comment is prose.
sedclass() {
    awk '
        /^[[:blank:]]*#/ { next }
        match($0, /(^|[^A-Za-z0-9_])sed[[:blank:]]/) {
            if (substr($0, RSTART) ~ /\[\^?[^]]*\\t/)
                print "line " FNR ": " $0
        }' "$1"
}
nsed=0
for f in "$dir"/*.sh; do
    [ -e "$f" ] || continue
    nsed=$((nsed + 1))
    bad=$(sedclass "$f")
    [ -n "$bad" ] || continue
    echo "FAIL: $(basename "$f") writes a blank as an escape inside a bracket,"
    echo "      which one sed reads as a tab and another as a backslash and a"
    echo "      letter -- write [[:blank:]]:"
    printf '%s\n' "$bad" | sed 's/^/      /'
    fail=1
done
if [ "$nsed" -eq 0 ]; then
    echo "FAIL: no shell file was read for the dialect its scans are written"
    echo "      in; the rule above found nothing because it looked at nothing"
    fail=1
fi
# and the rule has to be able to find one, or it is quiet for the same
# reason a suite in good order is. The lines it is given are assembled
# from the escape rather than written out, so that this file does not
# hold the fault it is here to refuse.
esc='\t'
{
    printf "x | sed 's/[ %s]*\$//'\\n" "$esc"
    printf "sed -n 's/^[^%s]*%s//p'\\n" "$esc" "$esc"
} > "$work/dialect"
if [ "$(sedclass "$work/dialect" | grep -c .)" -ne 2 ]; then
    echo "FAIL: the dialect rule does not find a blank written as an escape"
    echo "      inside a bracket; it would pass every scan in the suite"
    fail=1
fi
{
    printf "x | sed 's/[[:blank:]]*\$//'\\n"
    printf "awk '{ sub(/[ %s]+\$/, \\\"\\\") }'\\n" "$esc"
    printf "# one sed reads [ %s] as a tab\\n" "$esc"
} > "$work/dialect"
if [ -n "$(sedclass "$work/dialect")" ]; then
    echo "FAIL: the dialect rule refuses the portable spelling, an awk that"
    echo "      does read the escape, or prose about the rule"
    fail=1
fi

# and the rule itself has to hold, under the pattern matcher this host has
#
# A rule kept in one place fails in one place, and a rule made of
# patterns fails by matching nothing rather than by matching wrongly --
# which reads as a suite in good order. Nothing in the text of a pattern
# says whether the matcher here agrees with it. So put the cases to it.
verdict_expect() {          # <want: ok|no> <what> <output>
    if verdict_ok "$3" "the self-check" >/dev/null 2>&1; then
        got=ok
    else
        got=no
    fi
    if [ "$got" != "$1" ]; then
        echo "FAIL: verdict.sh answered $got where $1 is owed, for $2;"
        echo "      the rule every wrapper reads its verdict by does not hold"
        fail=1
    fi
}
guard_mirror verdict "$dir/verdict.sh"
. "$mirror/verdict.sh"
verdict_expect ok "a run that reported success" "SUCCESS"
verdict_expect ok "a verdict on the end of the page banner" \
    "----showpage----SUCCESS"
verdict_expect no "a run that printed nothing" ""
verdict_expect no "a run that did not report" "ok: something"
verdict_expect no "a failure before the verdict" \
    "$(printf 'FAILURE: something\nSUCCESS')"
verdict_expect no "a failure after the verdict" \
    "$(printf 'SUCCESS\nFAIL: something')"
verdict_expect no "a failure on the end of the page banner" \
    "$(printf -- '----showpage----FAIL: something\nSUCCESS')"
verdict_expect no "a mismatch before the verdict" \
    "$(printf 'MISMATCH results: 1 2\nSUCCESS')"
verdict_expect no "two verdicts from one run" "$(printf 'SUCCESS\nSUCCESS')"
verdict_expect no "a word the verdict is only the tail of" "NOTSUCCESS"
verdict_run_expect() {      # <want: ok|no> <what> <status> <output>
    if verdict_run "$3" "$4" "the self-check" >/dev/null 2>&1; then
        got=ok
    else
        got=no
    fi
    if [ "$got" != "$1" ]; then
        echo "FAIL: verdict.sh answered $got where $1 is owed, for $2;"
        echo "      the rule a wrapper reads its run's status by does not hold"
        fail=1
    fi
}
verdict_run_expect ok "a run that left cleanly and said nothing" 0 ""
verdict_run_expect ok "a run that left cleanly saying what it did" 0 \
    "OK   pgm (12 bytes)"
verdict_run_expect no "a run that wrote its artifacts and then died" 139 ""
verdict_run_expect no "a run that complained its way to a clean exit" 0 \
    "$(printf 'FAIL: an assertion nothing recorded\n')"
verdict_run_expect no "a mismatch from a run that left cleanly" 0 \
    "MISMATCH results: 1 2"
verdict_run_expect ok "a word a failure is only the tail of" 0 "PREFAIL"

# and the two rules over a wrapper, for the same reason: what they find
# in a directory of sound wrappers is nothing, which is also what a rule
# that has gone inert finds. So they are given wrappers that are not
# sound, and have to say so about each; and sound ones they must pass.
#
# Each case names the fault it is owed rather than merely that there was
# one, so that a case is not answered by whichever rule happens to speak.
wrapper_case() {            # <name> <body>
    printf '%s\n' "$2" > "$work/wcase-$1.sh"
    echo "$work/wcase-$1.sh"
}
wrapper_expect() {          # <owed: none|<text of the fault>> <what> <file>
    w_got=$(wrapper_faults "$3")
    if [ "$1" = none ]; then
        [ -z "$w_got" ] && return 0
        echo "FAIL: the wrapper rule complains about $2, which is sound:"
    else
        printf '%s\n' "$w_got" | grep -qF "$1" && return 0
        echo "FAIL: the wrapper rule does not answer \"$1\" for $2:"
    fi
    printf '%s\n' "${w_got:-(it said nothing)}" | sed 's/^/      /'
    echo "      the rule every wrapper judges its run by does not hold"
    fail=1
}
wrapper_expect none "a wrapper that judges a printed verdict" \
    "$(wrapper_case ok '. "$(dirname "$0")/verdict.sh"
out=$("$xpost" -q -d null "$script" 2>&1)
status=$?
verdict_ok "$out" "the suite"')"
wrapper_expect none "a wrapper that judges a run by its status" \
    "$(wrapper_case run '. "$(dirname "$0")/verdict.sh"
out=$("$xpost" -q -d pgm -o "$page" "$script" 2>&1)
verdict_run "$?" "$out" "the render" || exit 1
[ -s "$page" ] || { echo "FAIL: no page"; exit 1; }')"
wrapper_expect "without verdict_ok or verdict_run" \
    "a wrapper that matches the verdict itself" \
    "$(wrapper_case own '. "$(dirname "$0")/device-fleet.sh"
out=$("$xpost" -q -d null "$script" 2>&1)
status=$?
printf "%s\n" "$out" | grep -q SUCCESS || exit 1')"
wrapper_expect "without sourcing verdict.sh" \
    "a wrapper that reaches for the rule without loading it" \
    "$(wrapper_case unsourced 'out=$("$xpost" -q -d null "$script" 2>&1)
status=$?
verdict_ok "$out" "the suite"')"
wrapper_expect "whose last stage is a text filter" \
    "a status read off the filter the run was piped into" \
    "$(wrapper_case piped '. "$(dirname "$0")/verdict.sh"
"$xpost" -q -d null "$script" 2>&1 | tail -20
status=$?
verdict_run "$status" "" "the suite"')"
# the boundaries, each of which reads as the pipeline rule going quiet
wrapper_expect none "a status read off a subshell around the run" \
    "$(wrapper_case subshell '. "$(dirname "$0")/verdict.sh"
printf "%b" "$input" | ( cd "$work" && "$xpost" -q -d null "$script" )
status=$?
verdict_run "$status" "" "the suite"')"
wrapper_expect none "a status read after a run that was piped into nothing" \
    "$(wrapper_case plain '. "$(dirname "$0")/verdict.sh"
"$xpost" -q -d null "$script" >/dev/null 2>&1
status=$?
verdict_run "$status" "" "the suite"')"
wrapper_expect none "a status read after a line carrying an or, not a pipe" \
    "$(wrapper_case orbar '. "$(dirname "$0")/verdict.sh"
"$xpost" -q -d null "$script" >"$log" 2>&1 || cat "$log"
status=$?
verdict_run "$status" "" "the suite"')"
wrapper_expect "whose last stage is a text filter" \
    "a pipeline written over lines, the last of them carrying no pipe" \
    "$(wrapper_case continued '. "$(dirname "$0")/verdict.sh"
"$xpost" -q -d null "$script" 2>&1 \
    | tail -20 \
    > "$log"
status=$?
verdict_run "$status" "" "the suite"')"

# and so does the C-test rule, for the same reason
#
# Its findings are patterns over a C file, and a pattern that has gone
# inert finds nothing in eighteen sound files -- which is exactly what
# eighteen sound files look like. So it is given files that are not
# sound, and has to say so about each; and files that carry a failure
# word without printing one, which it has to pass.
#
# Each case names the fault it is owed rather than merely that there was
# one. A case judged on whether anything at all was said passes when the
# rule it was written for has gone quiet and some other rule happens to
# answer instead -- which is how the first draft of the case below for a
# status in the first column read as sound while the padding that makes
# it work had been taken out.
ctest_case() {              # <name> <body>
    printf '%s\n' "$2" > "$work/case-$1.c"
    echo "$work/case-$1.c"
}
ctest_expect() {            # <owed: none|<text of the fault>> <what> <file>
    ct_got=$(ctest_faults "$3")
    if [ "$1" = none ]; then
        [ -z "$ct_got" ] && return 0
        echo "FAIL: the C-test rule complains about $2, which is sound:"
    else
        printf '%s\n' "$ct_got" | grep -qF "$1" && return 0
        echo "FAIL: the C-test rule does not answer \"$1\" for $2:"
    fi
    printf '%s\n' "${ct_got:-(it said nothing)}" | sed 's/^/      /'
    echo "      the rule every C test reaches its exit status by does not hold"
    fail=1
}
ctest_expect none "a test that reports and answers through the helper" \
    "$(ctest_case sound '#include "xpost_test.h"
int main(void)
{
    check(1 == 1, "one is one");
    if (0)
        return verdict();
    return verdict();
}')"
ctest_expect none "a failure word in a comment" \
    "$(ctest_case comment '#include "xpost_test.h"
/* printf("FAIL: not a finding") */
int main(void)
{
    check(1 == 1, "one is one");
    return verdict();
}')"
ctest_expect none "a failure word in a string the test compares against" \
    "$(ctest_case compare '#include "xpost_test.h"
int main(void)
{
    check(strcmp(s, "FAILURE") == 0, "the reply is the one expected");
    return verdict();
}')"
ctest_expect "prints a failure nothing records" "a failure printed by hand" \
    "$(ctest_case handprint '#include "xpost_test.h"
int main(void)
{
    check(1 == 1, "one is one");
    printf("FAIL: nothing records this\n");
    return verdict();
}')"
ctest_expect "other than verdict()" "a main that answers zero of its own" \
    "$(ctest_case retzero '#include "xpost_test.h"
int main(void)
{
    check(1 == 1, "one is one");
    return 0;
}')"
ctest_expect "other than verdict()" "a setup path that answers one of its own" \
    "$(ctest_case retone '#include "xpost_test.h"
int main(void)
{
    if (!setup())
    {
        report_failure("setup");
        return 1;
    }
    check(1 == 1, "one is one");
    return verdict();
}')"
ctest_expect "leaves through exit()" "a main that leaves through exit" \
    "$(ctest_case exits '#include "xpost_test.h"
int main(void)
{
    check(1 == 1, "one is one");
    exit(0);
}')"
ctest_expect "asserts nothing" "a test that holds nothing to anything" \
    "$(ctest_case inert '#include "xpost_test.h"
int main(void)
{
    return verdict();
}')"
# and the boundaries, each of which reads as the whole rule going quiet
# if the bracket around a word is dropped or never matches
ctest_expect none "a failure word assembled rather than printed" \
    "$(ctest_case sprintf '#include "xpost_test.h"
int main(void)
{
    sprintf(buf, "FAIL: %s", what);
    check(1 == 1, "one is one");
    return verdict();
}')"
ctest_expect none "a name the assertion is only the tail of" \
    "$(ctest_case tail '#include "xpost_test.h"
int main(void)
{
    int returned = 0;
    check(returned == 0, "nothing came back");
    return verdict();
}')"
ctest_expect "asserts nothing" \
    "a test whose only assertion is a name check is the tail of" \
    "$(ctest_case recheck '#include "xpost_test.h"
int main(void)
{
    recheck(1 == 1, "one is one");
    return verdict();
}')"
ctest_expect "other than verdict()" "a status answered from the first column" \
    "$(ctest_case column0 '#include "xpost_test.h"
int main(void)
{
check(1 == 1, "one is one");
return 0;
}')"

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a test cannot reliably fail"
    exit 1
fi
echo "SUCCESS ($nps test files, $nc C tests, $nrun wrappers, $ncheck guards)"
exit 0
