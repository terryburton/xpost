#!/bin/sh
# Meson test wrapper: the byte-identity render gate. Render the golden
# page (golden_page.ps) through every deterministic output device and
# require each output's sha256 to match tests/golden/manifest.sha256.
#
# This is the instrument behind every "no behavioral change" refactor:
# a restructuring that claims zero cost must leave these bytes exactly
# as they were. Devices whose bytes depend on external library versions
# (png, jpeg) are exercised elsewhere and excluded here; the window
# devices need a display and are likewise out.
#
# Regenerate after an INTENDED rendering change (declare it in the same
# commit) with:
#     tests/run-golden-render.sh <xpost> <page.ps> <goldendir> --regen
#
#   $1  path to the built xpost binary
#   $2  path to golden_page.ps
#   $3  path to the tests/golden directory (the manifest's home)
#   $4  optionally --regen
set -u
xpost=$1
page=$2
golden=$3
regen=${4:-}

devices='pgm ppm pbm tiff pdfwrite svgwrite dscwrite'

# The two object widths are two personalities, and they do not render
# byte for byte alike: the wide build's integers reach further, so
# arithmetic that leaves the narrow build's range as a real stays exact
# there and rounds a colour differently. Each personality is held to its
# own manifest rather than one being declared the right answer.
probe=$(mktemp)
cat > "$probe" <<'PROBEEOF'
2147483647 1 add type /integertype eq
    { (XPOSTWIDTH=wide) }{ (XPOSTWIDTH=narrow) } ifelse =
quit
PROBEEOF
width=$("$xpost" -q --no-sandbox -d null "$probe" </dev/null 2>/dev/null \
        | grep -o 'XPOSTWIDTH=[a-z]*' | head -1 | cut -d= -f2)
rm -f "$probe"
case $width in
    wide)   manifest="$golden/manifest-large.sha256" ;;
    narrow) manifest="$golden/manifest.sha256" ;;
    *)      echo "FAILURES: could not tell which object width this build has"
            exit 1 ;;
esac

if command -v sha256sum >/dev/null 2>&1; then
    sum() { sha256sum "$1" | cut -d' ' -f1; }
else
    sum() { shasum -a 256 "$1" | cut -d' ' -f1; }
fi

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
fail=0
out_manifest="$work/manifest.sha256"

for dev in $devices; do
    out="$work/golden.$dev"
    err=$("$xpost" -q $ns -d "$dev" -o "$out" "$page" </dev/null 2>&1)
    status=$?
    if [ "$status" -ne 0 ]; then
        echo "FAILURES: the interpreter exited with status $status"
        exit 1
    fi
    if printf '%s' "$err" | grep -q '%%\[ Error'; then
        echo "FAIL $dev: $(printf '%s' "$err" | grep '%%\[ Error' | head -1)"
        fail=1
        continue
    fi
    if [ ! -s "$out" ]; then
        echo "FAIL $dev: produced no output"
        fail=1
        continue
    fi
    printf '%s  %s\n' "$(sum "$out")" "$dev" >> "$out_manifest"
done

if [ "$fail" -ne 0 ]; then
    rm -rf "$work"
    echo "FAILURES: a device did not render the golden page"
    exit 1
fi

if [ "$regen" = "--regen" ]; then
    mkdir -p "$golden"
    cp "$out_manifest" "$manifest"
    echo "regenerated $manifest:"
    cat "$manifest"
    rm -rf "$work"
    exit 0
fi

if [ ! -s "$manifest" ]; then
    rm -rf "$work"
    echo "FAILURES: no usable manifest at $manifest (run with --regen to create)"
    exit 1
fi

# the manifest must cover every device rendered, or a truncated file would
# silently reduce the gate to whatever lines survived
for dev in $devices; do
    if ! grep -q " $dev\$" "$manifest"; then
        echo "FAIL: $dev is rendered but absent from the manifest"
        fail=1
    fi
done
if [ "$fail" -ne 0 ]; then
    rm -rf "$work"
    echo "FAILURES: the manifest does not cover the rendered devices"
    exit 1
fi

# compare per device so a mismatch names its device
while read -r want dev; do
    got=$(grep " $dev\$" "$out_manifest" | cut -d' ' -f1)
    if [ "$got" != "$want" ]; then
        echo "FAIL $dev: rendered bytes differ from the golden manifest"
        fail=1
    else
        echo "OK   $dev"
    fi
done < "$manifest"

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: rendered output drifted from the golden bytes"
    exit 1
fi
echo "SUCCESS"
exit 0
