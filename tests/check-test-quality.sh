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
#      a run that printed a failure and then printed SUCCESS. Seventeen
#      wrappers did, and one of them was passing over a real failure.
#      The rule lives in tests/verdict.sh so there is one of it.
#   7. A C test reports through its exit status, so the same hole one
#      layer down is a path that prints a failure and returns zero: a
#      failure printed without being recorded, or a status answered
#      without reading the record. Each of the eighteen C tests carried
#      its own counter, its own assertion and its own verdict, so each
#      was a place the pair could come apart. The rule lives in
#      tests/xpost_test.h so there is one of it.
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
    if ! grep -qE 'status=\$\?|st=\$\?|\$\? -ne 0|\|\| (exit|fail)|rc=\$\?|ret=\$\?' "$f"; then
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

# 6. the verdict a run printed is judged by one rule, in one place
#
# A wrapper that matches SUCCESS against a run's output itself is
# deciding, on its own, what makes a verdict count -- and what every one
# of them left out was that a run which printed a failure first has
# already failed, whatever it went on to conclude. tests/verdict.sh
# carries that; a wrapper reaches it through verdict_ok.
#
# This catches the spelling the suite uses. A wrapper that reads a
# verdict of some other spelling is outside it, which is why the helper
# is where the rule lives rather than where it is checked: the way to
# stay outside this is to write a wrapper that judges a run some other
# way entirely, and that wrapper would be a new thing to review.
for f in "$dir"/run-*.sh; do
    [ -e "$f" ] || continue
    body=$(tr -d '\r' < "$f" | sed 's/#.*//')
    printf '%s\n' "$body" | grep -qE 'grep[^|]*SUCCESS|=[[:space:]]*"?SUCCESS' \
        || continue
    if ! printf '%s\n' "$body" | grep -q 'verdict_ok'; then
        echo "FAIL: $(basename "$f") matches SUCCESS against a run's output"
        echo "      itself; a run that printed a failure first has already"
        echo "      failed -- judge it with verdict_ok from verdict.sh"
        fail=1
    fi
done

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
    guard_c_source "$1" | sed 's/^[^:]*:[0-9]*://; s/^/ /; s/$/ /' \
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
