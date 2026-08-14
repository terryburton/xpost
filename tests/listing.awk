# How a test listing line is laid out is the meson version's to choose.
# A test carrying suites is written either as
#
#     <project>:<suite>+<suite> / <name>
#     <suite>+<suite> - <project>:<name>
#
# and the separator standing after the first field says which. The suites
# are that first field, less the project name where the layout qualifies
# it with one; the test's own name is what the line ends with, less the
# same qualification. A line with neither separator names no suite, and
# listing_suites answers with nothing for it.
#
# The fields are read rather than the line searched for a separator, so
# that a test whose own name carries " / " or " - " is read by its layout
# and not by its name.
#
# The record meson writes of a run names each test as the listing does,
# so the same reading serves a record's name field. This file is a pair
# of functions and nothing else: hand it to awk with -f ahead of the
# program that calls them.

function listing_suites(line,   n, f, s) {
    n = split(line, f, " ")
    if (n < 3) return ""
    if (f[2] == "/") { s = f[1]; sub(/^[^:]*:/, "", s); return s }
    if (f[2] == "-") return f[1]
    return ""
}

function listing_name(line,   n, f, s) {
    n = split(line, f, " ")
    if (n < 3) return line
    if (f[2] == "/") return substr(line, index(line, " / ") + 3)
    if (f[2] == "-") {
        s = substr(line, index(line, " - ") + 3)
        sub(/^[^:]*:/, "", s)
        return s
    }
    return line
}
