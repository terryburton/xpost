#!/bin/sh
# Meson test wrapper: which of the two routes a page is put out by.
#
# Selecting a device whose page may arrive a band at a time selects
# banding, and what that selects is a record: the marks are written down
# and played into a raster of a band at a time. A page the band budget
# already covers is played into a raster of the whole of it, so such a
# record holds a whole page's drawing in order to hand it to a device
# that was going to hold the whole page anyway -- every byte of it spent
# on a page that comes out the same either way. So the route is weighed,
# and a page under the budget is painted on the device the run asked for
# with nothing recorded at all.
#
# Nothing else in the suite can see that. The page is the same page byte
# for byte whichever route it took, which is the whole claim banding
# makes, so no comparison of pages says which route was taken; and what a
# record costs a small page is small, which makes a meter's reading on an
# ordinary page ambiguous rather than absent. What says it plainly is the
# device the run ended up with: a record names the class it plays into
# and carries the budget it divides that class's rows by, and a device
# painting its own page carries neither.
#
# So this asks the device. Per device that bands by default:
#
#   at the largest page the budget still covers   nothing is recorded
#   at one row more than that                     a record is
#
# and the boundary is not a number written here. It is derived, per
# device, from the two numbers that decide it -- what the device prices
# one of its own rows at, and what the recording class's budget is -- so
# a run against a changed budget moves with it, and a run that derived
# the boundary wrongly would be asking about the wrong page rather than
# passing.
#
# The two spellings that opt out are held on the far side of that
# boundary from where the weighing would have put them: device:whole
# records nothing at a page over the budget, and record:device records at
# a page under it. Each is a run saying which route it wants, and neither
# is the weighing's to overrule.
#
# And the page is compared across the crossing, because a route that
# records nothing is only right if it puts out the same page: the run one
# row over the budget and the run at that page held whole must write the
# same bytes.
#
# The controls, which this runs on itself at the end and requires to
# fail: the instrument that reads which route was taken is stubbed both
# ways, and the gate itself is broken both ways, by running against a
# recording class whose budget covers no page and one whose budget covers
# every page. A check that could not see those four is a check that would
# pass whatever the route did.
#
#   $1  path to the built xpost binary
#
# and, for the controls it invokes on itself:
#
#   --sabotage N $1   run a reduced pass with defect N built in, and
#                     answer as it finds it. The caller requires a
#                     failure.
set -u

sab=0
case ${1:-} in
    --sabotage) sab=$2; shift 2 ;;
esac
xpost=${1:?usage: run-band-route-test.sh [--sabotage N] <xpost>}
. "$(dirname "$0")/verdict.sh"
. "$(dirname "$0")/device-fleet.sh"
self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
# an absolute path may begin with a drive letter as well as a slash;
# prepending the working directory to one of those makes every
# invocation a path that does not exist
case $xpost in /* | ?:/* | ?:\\*) ;; *) xpost=$PWD/$xpost ;; esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The width every page here is at. An ordinary one, so the height the
# boundary falls at is an ordinary number of rows rather than a
# degenerate one.
W=612

# What the run says about itself. The route is read off the device in the
# graphics state: a record carries the class it plays into and the budget
# it divides that class's rows by, and a device painting its own page
# carries neither. Both are read, because one of them answering alone
# would be an instrument with nothing holding it -- a run reporting only
# half a record says so and is a failure of its own.
#
# SAB stubs the reading, which is this test's control on its instrument.
cat > "$work/route.ps" <<'PSEOF'
/SAB where { pop }{ /SAB 0 def } ifelse
/played DEVICE /.playclass known def
/priced DEVICE /.bandbytes known def
SAB 1 eq { /played false def /priced false def } if
SAB 2 eq { /played true def /priced true def } if
(ROUTE ) print
played priced eq
    { played { (record) }{ (direct) } ifelse }
    { (split) }
ifelse print
( band=) print currentsystemparams /CurBandHeight get 20 string cvs print
(\n) print flush
0 setgray 10 10 30 30 rectfill
showpage
PSEOF

# The boundary, derived from the numbers that decide it. The device the
# run is on prices one of its own rows, which is the row the budget is
# divided by; the recording class states the budget. The largest page the
# budget still covers is that many rows, and one row more is a page that
# needs a second band.
cat > "$work/bound.ps" <<'PSEOF'
/rowbytes 2 dict begin
    /width DEVICE /width get def
    DEVICE /.rowcost get exec
end exch pop def
/budget .privatedict /.xpost_RECORD get /.bandbytes get def
(BOUND ) print budget rowbytes idiv 20 string cvs print (\n) print flush
PSEOF

fail=0
# the definition that stubs the instrument, empty where nothing is stubbed
SABDEF=
case $sab in 1|2) SABDEF="-DSAB=$sab" ;; esac
# the data directory the checks run against, which the gate's controls
# replace with one carrying a rewritten budget
RUNDATA=${XPOST_DATA_DIR:-}

# route SELECTION GEOMETRY OUTFILE -- leaves the route in $route and the
# rows held at once in $band, or answers non-zero
route_of() {
    if [ -n "$RUNDATA" ]; then
        out=$(XPOST_DATA_DIR=$RUNDATA "$xpost" -q --no-sandbox \
              -g "$2+0+0" -d "$1" -o "$3" $SABDEF "$work/route.ps" \
              </dev/null 2>&1)
    else
        out=$("$xpost" -q --no-sandbox -g "$2+0+0" -d "$1" -o "$3" \
              $SABDEF "$work/route.ps" </dev/null 2>&1)
    fi
    st=$?
    verdict_run "$st" "$out" "$1 at $2" || return 1
    route=$(printf '%s\n' "$out" | sed -n 's/.*ROUTE \([a-z][a-z]*\) band=.*/\1/p')
    band=$(printf '%s\n' "$out" | sed -n 's/.*ROUTE [a-z][a-z]* band=\([0-9][0-9]*\).*/\1/p')
    if [ -z "$route" ] || [ -z "$band" ]; then
        echo "FAILURES: $1 at $2 said nothing about the route it took"
        return 1
    fi
    if [ "$route" = split ]; then
        echo "FAILURES: $1 at $2 is half a record: it names a class to play"
        echo "      into or a budget to divide, but not both"
        return 1
    fi
    return 0
}

# took SELECTION GEOMETRY OUTFILE WANTED WHY
took() {
    route_of "$1" "$2" "$3" || return 1
    if [ "$route" != "$4" ]; then
        echo "FAILURES: $1 at $2 $5"
        echo "      wanted $4, and the run reports $route (band=$band)"
        return 1
    fi
    return 0
}

one_device() {
    dev=$1

    # The boundary this device's rows and the class's budget put the page
    # at. Taken on a page small enough to cost nothing, through the
    # spelling that holds the page whole, so the device answering is the
    # one whose rows are being priced -- and against the tree's own data
    # directory, so a control that rewrote the budget is weighed against
    # the boundary the real one puts the page at.
    b_out=$("$xpost" -q --no-sandbox -g "${W}x64+0+0" -d "$dev:whole" \
            -o "$work/bound.out" "$work/bound.ps" </dev/null 2>&1)
    b_st=$?
    case $b_out in
        *"wrong device"* | *"unknown device"*)
            echo "SKIP $dev (not built in)"; return 2 ;;
    esac
    verdict_run "$b_st" "$b_out" "$dev pricing a row" || return 1
    fits=$(printf '%s\n' "$b_out" | sed -n 's/.*BOUND \([0-9][0-9]*\).*/\1/p')
    if [ -z "$fits" ] || [ "$fits" -lt 2 ]; then
        echo "FAILURES: $dev did not price a row, so there is no page to ask"
        echo "      about: the boundary came back as '$fits'"
        return 1
    fi
    over=$((fits + 1))

    took "$dev" "${W}x${fits}" "$work/fits.out" direct \
         "is the largest page the budget covers and must record nothing" \
        || return 1
    took "$dev" "${W}x${over}" "$work/over.out" record \
         "is one row past the budget and must be recorded" || return 1
    if [ "$band" -ge "$over" ] || [ "$band" -lt 1 ]; then
        echo "FAILURES: $dev at ${W}x${over} reports holding $band rows at"
        echo "      once of a $over row page, which is not a band of it"
        return 1
    fi

    # The two spellings that say which route they want, each held on the
    # side of the boundary the weighing would have sent it to the other.
    took "$dev:whole" "${W}x${over}" "$work/whole.out" direct \
         "asks for the page whole and must record nothing at any page" \
        || return 1
    took "record:$dev" "${W}x${fits}" "$work/rec.out" record \
         "asks for a record by name and must be given one at any page" \
        || return 1

    # The page a route recorded and the page it did not are the same
    # page. Compared where the two routes differ, which is the run one
    # row past the budget.
    if ! cmp -s "$work/over.out" "$work/whole.out"; then
        echo "FAILURES: $dev writes a different page recorded than held"
        echo "      whole, at ${W}x${over}"
        return 1
    fi
    return 0
}

# A build without a device's library has no such selection to make.
# Which those are is read off the interpreter rather than assumed: handed
# a device name it does not have, it lists the ones it does.
have=$("$xpost" -q --no-sandbox -d '' /dev/null </dev/null 2>&1)
ASK=
for dev in $DEVICE_FLEET_BANDS; do
    if printf '%s\n' "$have" | grep -qx "[[:space:]]*$dev"; then
        ASK="$ASK $dev"
    else
        echo "SKIP $dev (not in this build)"
    fi
done
if [ -z "$ASK" ]; then
    echo "FAILURES: no device that bands by default is in this build, so"
    echo "      nothing was asked"
    exit 1
fi

# The rewritten recording class a gate control runs against: a budget of
# one byte covers no page and a budget of two thousand million covers
# every page this asks about. Built out of the tree's own data directory,
# so that everything but the budget is what the run would have used.
if [ "$sab" -eq 3 ] || [ "$sab" -eq 4 ]; then
    if [ -z "$RUNDATA" ]; then
        echo "FAILURES: the gate's controls need the data directory named"
        exit 1
    fi
    case $sab in 3) budget=1 ;; *) budget=2000000000 ;; esac
    d=$work/data
    mkdir -p "$d"
    cp -R "$RUNDATA"/. "$d/"
    sed "s|^\( *\)/\.bandbytes  *[0-9][0-9]*|\1/.bandbytes $budget|" \
        "$RUNDATA/recorddev.ps" > "$d/recorddev.ps"
    if cmp -s "$RUNDATA/recorddev.ps" "$d/recorddev.ps"; then
        echo "FAILURES: the control could not rewrite the band budget, so"
        echo "      it is not a control on the gate"
        exit 1
    fi
    RUNDATA=$d
fi

# The reduced pass a control runs: one device is enough to show that a
# broken gate or a stubbed instrument is seen.
if [ "$sab" -ne 0 ]; then
    for dev in $ASK; do ASK=$dev; break; done
fi

asked=0
for dev in $ASK; do
    one_device "$dev"
    case $? in
        0) asked=$((asked + 1)) ;;
        2) ;;
        *) fail=1 ;;
    esac
done

if [ "$asked" -eq 0 ]; then
    echo "FAILURES: no device answered, so nothing was held to anything"
    exit 1
fi

if [ "$sab" -ne 0 ]; then
    [ "$fail" -eq 0 ] || exit 1
    echo "the sabotage went unseen"
    exit 0
fi

echo "band-route: held on $asked device(s)"

# ---------------------------------------------------------------------
# The controls
#
# Four defects, one per thing that could go quiet: the instrument
# answering direct whatever happened, the instrument answering record
# whatever happened, a gate that records every page, and a gate that
# records none. Each is required to be seen.
# ---------------------------------------------------------------------
for n in 1 2 3 4; do
    s_out=$("$self" --sabotage "$n" "$xpost" 2>&1)
    s_st=$?
    if [ "$s_st" -eq 0 ]; then
        echo "FAILURES: sabotage $n was not noticed, so the check that"
        echo "      should have caught it sees nothing:"
        printf '%s\n' "$s_out" | sed 's/^/      /'
        fail=1
    fi
done

[ "$fail" -eq 0 ] || exit 1
echo SUCCESS
