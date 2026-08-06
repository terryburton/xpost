# Sourced by the run-*.sh wrappers: judge the verdict a run printed.
#
# A run reports its own result, and a wrapper that looks for SUCCESS
# anywhere in the output accepts a run that printed a failure and then
# printed SUCCESS. Both halves of that happen. A test whose assertion
# fails says so and carries on to a verdict it computes from something
# else; a test whose failure branch ends the run reaches its verdict
# anyway if the run does not stop where it was told to. Either way the
# failure is on screen, in a log nobody reads because the test passed.
#
# So the verdict is not a line to be found: it is a line that counts only
# when the same run printed no failure. That is the whole rule, kept here
# so that it is the same rule in every wrapper -- one enforced in some of
# them and not in others is the same hole with a longer way in.
#
# Position is deliberately not the rule. The verdict is not the last
# thing a run prints: the startup banner precedes it, the prompt of an
# interpreter that reaches its executive follows it, and a wrapper that
# folds the log channel in gets whatever the device said on its way out.
# Nor does the verdict have a line to itself -- the showpage banner of
# the default page semantics ends without one, so a run that showed a
# page prints its verdict on the end of that. None of it is the test's to
# arrange, and a rule about it would judge the interpreter's framing
# rather than the test's answer. What the rule does need is that the word
# is the run's own and not the tail of a longer one, which is what the
# leading boundary below is for.
#
# The verdict is SUCCESS everywhere but the one check whose PostScript
# side answers PASS, FAIL or INCONCLUSIVE because it has a third thing
# to say, and says it at the head of a line it goes on to explain in.
# That wrapper names the shape it is looking for; the rule around it is
# the same one either way.

# What a run prints to report a failure. Every spelling the suite uses
# starts with one of these: FAIL, FAILURE, FAILURES, MISMATCH.
#
# The line boundary is spelt as two whole branches rather than as (^|...)
# inside one. A caret anchors only at the start of an expression; what it
# means anywhere else is left undefined, and a matcher that reads it as
# neither an anchor nor a literal matches no line at all. What that costs
# here is not a wrong answer but no answer: a rule that never fires, and
# a suite that comes back clean because nothing was asked.
VERDICT_FAILURE_RE='^(FAIL|MISMATCH)|[^A-Za-z](FAIL|MISMATCH)'

# Judge one run's output.
#   $1  the output, as captured
#   $2  what to call the run in a complaint (optional)
#   $3  what the run's success line looks like (optional); the default
#       is SUCCESS ending a line, bounded so that it is the run's own
#       word and not the tail of a longer one
# Prints what was wrong and returns non-zero unless the run reported
# success exactly once and reported no failure.
verdict_ok() {
    _verdict_out=$1
    _verdict_who=${2:-the run}
    _verdict_re=${3:-'^SUCCESS[[:space:]]*$|[^A-Za-z]SUCCESS[[:space:]]*$'}

    if printf '%s\n' "$_verdict_out" | grep -qE "$VERDICT_FAILURE_RE"; then
        echo "FAILURES: $_verdict_who printed a failure:"
        printf '%s\n' "$_verdict_out" | grep -E "$VERDICT_FAILURE_RE" \
            | sed 's/^/      /'
        return 1
    fi

    # a trailing carriage return is a line ending, not content: a run on a
    # platform whose text output carries one still reported success
    _verdict_n=$(printf '%s\n' "$_verdict_out" \
                 | grep -cE "$_verdict_re") || _verdict_n=0
    if [ "$_verdict_n" -eq 0 ]; then
        echo "FAILURES: $_verdict_who did not report success"
        return 1
    fi
    # A run reports once. Twice means two runs' output was judged as one,
    # or that a program which should have stopped at its first verdict
    # went on to reach a second.
    if [ "$_verdict_n" -gt 1 ]; then
        echo "FAILURES: $_verdict_who reported success $_verdict_n times;"
        echo "      one run reports one verdict"
        return 1
    fi
    return 0
}
