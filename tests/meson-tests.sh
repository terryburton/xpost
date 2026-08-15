# What tests a configured build defines, read from its own description.
#
# Sourced, not run. Provides one function.
#
# A build directory carries meson-info/intro-tests.json, and the name of
# every test in it can be read from there without running meson at all --
# which matters, because the readers below run before and around the test
# suite rather than inside it.
#
# WHY THIS IS SHARED RATHER THAN WRITTEN WHERE IT IS NEEDED. The same
# pipeline lived in tests/gate.sh and tests/check-gate-map.sh, character
# for character. That is the arrangement this suite has been bitten by
# three times: one reader is corrected and the other is not, and the one
# left behind does not fail loudly -- it reads a shape it no longer
# recognises, finds nothing, and reports on nothing. The meson listing
# separator moved in 1.10 and did exactly that to three readers, which is
# why tests/listing.awk exists; this is the same lesson applied to the
# other description meson publishes.
#
# WHY THE PARSE LOOKS LIKE THIS. The file is JSON, and no JSON reader can
# be assumed present on a machine that can build this. What is wanted is
# narrow enough to take from the text: every test object carries a
# "name" followed by a "workdir". But the layout is the writer's choice
# -- some versions put the whole file on one line, others indent it over
# thousands -- and a host whose text files end their lines in CRLF leaves
# a return on each. So the file is flattened to one line of single-spaced
# text first, and the shape is looked for in that, which reads every
# layout rather than one of them.
#
# THE FLOOR IS HERE AND NOT IN THE CALLERS. A parse that stops matching
# answers an empty list, and an empty list is the one answer that makes
# every caller cheerful: a gate selects no tests and finishes fast, a
# roster check holds no rule against anything and passes. So answering
# short is refused where the answer is made, and a caller cannot forget
# to ask.

# meson_test_names <intro-tests.json> <outfile>
#
# Writes the test names, one per line, sorted and unique. Answers 0 on
# success; on failure says why on standard output and answers 1, leaving
# the caller to exit -- a caller in the middle of a mirrored tree may
# have cleaning up to do.
meson_test_names() {
    mtn_json=$1
    mtn_out=$2
    mtn_n=

    if [ ! -r "$mtn_json" ]; then
        echo "FAILURES: no test description to read at $mtn_json."
        echo "      A configured build writes one; a directory without it"
        echo "      is not one, and reading tests out of it would answer"
        echo "      an empty list rather than say so."
        return 1
    fi

    tr '\r\n\t' '   ' < "$mtn_json" | tr -s ' ' |
        sed 's/, "name": "/\
@@/g' | sed -n 's/^@@\([^"]*\)", "workdir".*/\1/p' | sort -u > "$mtn_out"

    mtn_n=$(grep -c . "$mtn_out" 2>/dev/null || true)
    case ${mtn_n:-0} in
        ''|*[!0-9]*) mtn_n=0 ;;
    esac
    if [ "$mtn_n" -lt 2 ]; then
        echo "FAILURES: read $mtn_n test name(s) out of"
        echo "      $mtn_json, which cannot be right. Either the"
        echo "      description has changed shape and this no longer reads"
        echo "      it, or the build defines no tests; whichever it is,"
        echo "      what reads this list would be looking at nothing."
        return 1
    fi
    return 0
}
