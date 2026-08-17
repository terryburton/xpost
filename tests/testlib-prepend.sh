# The one place the shared test framework is put in front of a suite that
# asks for it.
#
# tests/run-ps-test.sh runs most of the PostScript suites here, but it is not
# the only thing that runs one: eighty-five bespoke runners in this directory
# run the rest, each because its suite needs an environment variable, a device,
# a work directory or a second invocation. A marked suite handed straight to
# the interpreter gets no framework, so its first assert is an undefined name
# and the whole file reports nothing else.
#
# The marker is therefore honoured here, once, and every runner that runs a
# suite goes through it. tests/check-testlib.sh holds them to that. Sourced and
# then called as
#
#     . "$(dirname "$0")/testlib-prepend.sh"
#     testlib_prepend "$script" "$work"
#
# where $work is a directory the caller already cleans up. It sets
# testlib_run to the path to hand the interpreter: the suite itself when it
# does not ask for the framework, and the two concatenated when it does.
#
# THE DIRECTORY MUST NOT BE ONE THE SUITE ITSELF INSPECTS. The combined
# program is a file, and a suite whose subject is the contents of a directory
# counts it: one suite here fills a scratch directory with 65535 names, runs
# from inside it, and checks that exactly that many are enumerated. Hand this
# somewhere separate whenever the suite looks at where it runs.
#
# Cleanup is the caller's, deliberately. Most of these runners already make a
# work directory and remove it on exit, and a trap set here would replace the
# one they set rather than joining it.
testlib_prepend() {
    testlib_run=$1
    case $(head -n 1 "$1" 2>/dev/null) in
        '%!testlib'*)
            testlib_lib=$(dirname "$0")/testlib.ps
            if [ ! -f "$testlib_lib" ]; then
                echo "FAILURES: $1 asks for the shared test framework and"
                echo "      $testlib_lib is not there"
                exit 1
            fi
            if [ ! -d "$2" ]; then
                echo "FAILURES: testlib_prepend was given no writable directory"
                echo "      to build the combined program in"
                exit 1
            fi
            testlib_run=$2/testlib-$(basename "$1")
            cat "$testlib_lib" "$1" > "$testlib_run" || exit 1 ;;
    esac
}
