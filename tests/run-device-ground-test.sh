#!/bin/sh
# Meson test wrapper: the whole device roster agrees on what the page's
# ground is.
#
# The ground is the colour erasepage left, and it is what a read answers
# wherever a device holds no pixel -- off the page, over a row the device
# does not hold, on a raster that has been released. Every device has to
# answer the same thing there, because it is one page and one erase; a
# device answering a colour of its own is describing a different page
# from the rest of the fleet.
#
# That is not a property any one device can be checked against, and a
# check per device would be each device held to its own idea of the
# ground -- which is what let one family of devices call the ground black
# while another called it whatever the page was cleared to, each of them
# self-consistent and the two of them disagreeing. So every device is
# asked the same three questions (tests/device_ground_test.ps) and the
# answers are held against each other here.
#
# What is compared is a ratio, not a level. A device's channel scale is
# its own -- a byte on most of them, sixteen bits on the window device --
# so the readings are compared as fractions of each device's own full
# scale, by cross-multiplication rather than division. The full scale is
# read off the device too, as what it answers for a page cleared to white.
#
# The tolerance is one level of the coarser of the two channels being
# compared. Each device folds the ground by truncation, so a device with
# a finer channel lands nearer the true fraction than a coarser one can
# express, and the two differ by less than the coarse device's step. Two
# devices whose fractions differ by more than that are not rounding
# differently: they are describing different pages.
#
# A device that answers the same reading whatever the page was cleared to
# is not reporting the ground at all, and there is no fraction to compare
# -- its full scale would be its ground. Those devices are named, with
# the reason each cannot answer, rather than dropped: a device that goes
# quiet reads exactly like one that never spoke.
#
#   $1  path to the built xpost binary
#   $2  path to device_ground_test.ps
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"
. "$(dirname "$0")/device-fleet.sh"

# an absolute path may begin with a drive letter as well as a slash;
# prepending the working directory to one of those makes every
# invocation a path that does not exist
case $xpost in /* | ?:/* | ?:\\*) ;; *) xpost=$PWD/$xpost ;; esac
case $script in /* | ?:/* | ?:\\*) ;; *) script=$PWD/$script ;; esac

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The marking roster, plus the window device. The window device is not on
# the roster because the roster is what can be made without a display,
# and it is exactly the device this check exists for: it keeps a raster
# it cannot read back, which is the case where what a read answers is the
# ground and nothing but the ground.
devices="$DEVICE_FLEET_MARKING xcb"

# The members that cannot answer, with the reason each cannot. null
# paints nothing and bbox records a page's extent rather than its pixels,
# so neither has a page to clear; the vector writers keep a document
# rather than a raster and answer a read with a fixed value; and the
# alpha device clears its page to transparent rather than to a colour, so
# what it was cleared to is not a colour a read can report.
NO_GROUND='null bbox pdfwrite svgwrite pngalpha'

# What this interpreter was built with. A device needing a library the
# build did not find is absent, and is named below rather than quietly
# making the roster shorter.
built=$("$xpost" -q $ns -d '' /dev/null </dev/null 2>&1) || :

# The window device needs a display as well as a build. One is used if
# the environment has it and conjured if it does not; where neither is
# possible the device is named as one that could not be asked.
xcb_run=
xcb_why='it is not built into this interpreter'
if printf '%s\n' "$built" | grep -qx '[[:space:]]*xcb'; then
    if [ -n "${DISPLAY:-}" ]; then
        xcb_run=direct
    elif command -v xvfb-run >/dev/null 2>&1; then
        xcb_run=xvfb
    else
        xcb_why='there is no display for it to open a window on'
    fi
fi

# the devices this run cannot put the question to, which is what the
# answers are held against once they are in
unasked_want=$NO_GROUND
for d in $devices; do
    case " $NO_GROUND " in *" $d "*) continue ;; esac
    case $d in
        xcb) [ -n "$xcb_run" ] || unasked_want="$unasked_want xcb" ;;
        *)   printf '%s\n' "$built" | grep -qx "[[:space:]]*$d" ||
                 unasked_want="$unasked_want $d" ;;
    esac
done

# Put the question to one device. Answers 0 having recorded its three
# readings, 2 having said why it could not be asked, 1 on a failure.
ask() {
    ag_dev=$1

    case $ag_dev in
        xcb)
            case $xcb_run in
                direct) ag_out=$("$xpost" -q $ns -d xcb "$script" \
                                 </dev/null 2>&1) ;;
                xvfb)   ag_out=$(xvfb-run -a "$xpost" -q $ns -d xcb "$script" \
                                 </dev/null 2>&1) ;;
                *)      echo "UNASKED $ag_dev: $xcb_why"; return 2 ;;
            esac
            ag_st=$?
            ;;
        *)
            ag_out=$("$xpost" -q $ns -d "$ag_dev" -o "$work/out.$ag_dev" \
                     "$script" </dev/null 2>&1)
            ag_st=$?
            ;;
    esac

    case $ag_out in
        *"wrong device"*)
            echo "UNASKED $ag_dev: it is not built into this interpreter"
            return 2 ;;
    esac
    verdict_run "$ag_st" "$ag_out" "$ag_dev" || return 1

    ag_vals=
    for ag_tag in full high low; do
        ag_line=$(printf '%s\n' "$ag_out" | sed -n "s/^GROUND $ag_tag://p")
        if [ -z "$ag_line" ]; then
            echo "FAIL $ag_dev: it reported no $ag_tag reading"
            return 1
        fi
        # A page is cleared to a grey, and a grey carries the same value
        # in every channel, so a reading is one number repeated. A device
        # folding one channel differently is a disagreement with itself
        # and is reported here rather than averaged away.
        ag_first=
        for ag_c in $ag_line; do
            if [ -z "$ag_first" ]; then
                ag_first=$ag_c
            elif [ "$ag_c" != "$ag_first" ]; then
                echo "FAIL $ag_dev: its $ag_tag reading is$ag_line, and a page"
                echo "      cleared to a grey carries that grey in every channel"
                return 1
            fi
        done
        ag_vals="$ag_vals $ag_first"
    done

    # A device answering the same for all three pages is answering
    # something of its own rather than reporting what the page was
    # cleared to; there is no scale in it to take a fraction of.
    set -- $ag_vals
    if [ "$1" = "$2" ] && [ "$2" = "$3" ]; then
        echo "UNASKED $ag_dev: it answers $1 whatever the page was cleared to"
        return 2
    fi
    if [ "$1" -le "$2" ] || [ "$2" -le "$3" ]; then
        echo "FAIL $ag_dev: it read $1 for white, $2 for a lighter grey and $3"
        echo "      for a darker one, which is not an order those pages are in"
        return 1
    fi

    echo "$ag_vals" > "$work/read.$ag_dev"
    echo "OK   $ag_dev (white $1, 0.75 $2, 0.25 $3)"
    return 0
}

fail=0
fleet_each ask $devices || fail=1
fleet_hold_unasked "$unasked_want" || fail=1

# The answers against each other. Every device that answered is compared
# with the first of them, which makes every pair equal to within twice
# the tolerance and names the two devices in any disagreement.
ref=
for d in $devices; do
    [ -f "$work/read.$d" ] || continue
    read -r f h l < "$work/read.$d"
    if [ -z "$ref" ]; then
        ref=$d; rf=$f; rh=$h; rl=$l
        continue
    fi
    # a fraction of each device's own full scale, cross-multiplied so the
    # comparison is in whole numbers, and held to one level of the
    # coarser channel
    tol=$rf
    [ "$f" -gt "$tol" ] && tol=$f
    for pair in "high $h $rh" "low $l $rl"; do
        set -- $pair
        diff=$(( $2 * rf - $3 * f ))
        [ "$diff" -lt 0 ] && diff=$(( - diff ))
        if [ "$diff" -gt "$tol" ]; then
            echo "FAILURES: $d and $ref disagree about the ground: $d reads"
            echo "      $2 of $f for the $1 page and $ref reads $3 of $rf,"
            echo "      which are different colours and not different scales"
            fail=1
        fi
    done
done

if [ -z "$ref" ]; then
    echo "FAILURES: no device reported a ground, so nothing was compared"
    exit 1
fi
[ "$fail" = 0 ] || { echo "FAILURES: the devices above"; exit 1; }
echo "SUCCESS ($fleet_asked devices, $fleet_unasked could not be asked)"
exit 0
