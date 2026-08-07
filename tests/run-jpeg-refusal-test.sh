#!/bin/sh
# Meson test wrapper: a page the JPEG library will not compress.
#
# The library reports a fatal condition by calling the error handler it
# was given and then, if that handler returns, ending the process. The
# handler the device installs does not return: it jumps back to the call
# that started the compression, which abandons the page and reports the
# failure as a PostScript error. Nothing had ever asked it to, so the
# only thing standing between a page the library refuses and a killed
# interpreter had never once run.
#
# A page of no height is such a page -- there is nothing to compress --
# and it is asked for through the geometry option so that no device
# machinery has to be reached into to arrange it. What the run has to
# show is not the refusal but what follows it: the program catches the
# error, the interpreter goes on to its next statement, and the process
# reaches its own end rather than the library's.
#
# The same program at an ordinary size emits its page, so the refusal is
# the page's doing and not the program's.
#
#   $1  path to the built xpost binary
set -u
xpost=$1
# an absolute path may begin with a drive letter as well as a slash;
# prepending the working directory to one of those makes every
# invocation a path that does not exist
case $xpost in /* | ?:/* | ?:\\*) ;; *) xpost=$PWD/$xpost ;; esac
. "$(dirname "$0")/verdict.sh"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/page.ps" <<'PSEOF'
% The page is shown inside stopped, so a refusal is the program's to
% report; the arithmetic after it is there to be reached.
{ showpage } stopped { (the page was refused) = }{ (the page was emitted) = }
ifelse
1 2 add 3 eq { (the interpreter runs on) = }{ (FAIL: arithmetic) = } ifelse
quit
PSEOF

fail=0
note() { echo "FAIL: $1"; fail=1; }

# what the program said, matched at the end of a line only: the page
# semantics announce a showpage without ending the line they announce it
# on, so what the program prints next continues that line
said() { printf '%s\n' "$s_out" | grep -q "$1\$"; }

show() {    # $1 what to call it in a complaint, $2 the geometry
    s_out=$("$xpost" -q --no-sandbox -g "$2" -d jpeg -o "$work/out.jpg" \
            "$work/page.ps" </dev/null 2>&1)
    verdict_run "$?" "$s_out" "$1" || fail=1
}

# a page with no rows: the library refuses it and the interpreter carries
# the refusal rather than being ended by it
rm -f "$work/out.jpg"
show "a page of no height" 100x0+0+0
said 'the page was refused' || note "a page of no height was not refused"
said 'the interpreter runs on' \
    || note "the interpreter did not reach the statement after the refusal"

# an ordinary page through the same device and the same program
rm -f "$work/out.jpg"
show "a page of ordinary size" 100x50+0+0
said 'the page was emitted' || note "an ordinary page was refused"
said 'the interpreter runs on' \
    || note "the interpreter did not reach the statement after an ordinary page"
[ -s "$work/out.jpg" ] || note "an ordinary page left no JPEG behind"

[ "$fail" = 0 ] || { echo "FAILURES: the runs above"; exit 1; }
echo "SUCCESS"
