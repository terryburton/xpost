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
    if [ ! -d "$1" ]; then
        echo "FAILURES: $2 is not a directory: $1"
        exit 1
    fi
}

guard_require_file() {
    if [ ! -s "$1" ]; then
        echo "FAILURES: $2 is missing or empty: $1"
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
}
