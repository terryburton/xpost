#!/bin/sh
#
# Guard the device-driver skeleton: the compiled-buffer device fleet must
# reach the shared mechanics through xpost_dev_driver.h rather than
# hand-writing them, so the contract stated there stays the only
# statement of how a device folds operands, reaches its private struct,
# and which rectangle FillRect paints. The PostScript device classes are
# held to the same idea from their side: what every class does the same
# way is written once and referred to.
#
# No exemptions. The two files that used to have them were the two that
# broke the contract: the Windows driver, which cannot be compiled here,
# clamped a negative origin without shrinking the extent and treated the
# far edge as exclusive; and the generic rasteriser, whose two compiled
# base-class fills restated the extent arithmetic in floor space beside
# a helper that truncated. Exempting a file from the rule it breaks
# leaves the rule stated and unenforced, which is the failure this guard
# exists to prevent -- so both are covered, the Windows driver textually,
# since that holds whether or not this platform can build it.
#
# Sources are named rather than globbed out of a directory: a built tree
# leaves object files beside them whose debug information matches every
# pattern here, so a scan would read green where nothing was built and
# red where something was.
#
# Usage: check-device-skeleton.sh <source root>

set -eu

src=${1:?usage: check-device-skeleton.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

guard_workdir
trap 'rm -rf "$work"' EXIT
# read a tree whose lines end where the scans below expect them to
guard_mirror_tree "$src"
src=$mirror

libdir="$src/src/lib"
guard_require_dir "$libdir" "the library source directory"

# the compiled devices, and the rasteriser holding the base classes'
# compiled fills; the Windows driver is checked textually alongside
fleet="xpost_dev_bgr.c xpost_dev_jpeg.c xpost_dev_png.c xpost_dev_raster.c xpost_dev_xcb.c"
marking="$fleet xpost_dev_win32.c xpost_dev_generic.c"

# every file that defines a device class
classes="image.ps pgmimage.ps pbmimage.ps ppmimage.ps tiffimage.ps
         nulldev.ps bboxdev.ps pdfwrite.ps svgwrite.ps dscwrite.ps"

fail=0

# 1. Private-struct access goes through xpost_dev_private_get/put: no raw
#    memory accessor in any device source (the helpers in the driver
#    header hold the only calls).
for f in $marking; do
    hits=$(grep -nE '\bxpost_memory_(get|put)\(' "$libdir/$f" || true)
    if [ -n "$hits" ]; then
        echo "check-device-skeleton: raw memory accessor in $f:" >&2
        printf '%s\n' "$hits" >&2
        echo "Reach the private struct through xpost_dev_private_get/put." >&2
        fail=1
    fi
done

# 2. Operand folding goes through xpost_dev_num_to_*: hand-folding is
#    recognisable by its realtype dispatch, which a migrated device no
#    longer needs. The generic rasteriser is exempt from this one rule
#    alone: it inspects operand types for reasons that are not folding.
for f in $fleet xpost_dev_win32.c; do
    hits=$(grep -nE '\brealtype\b' "$libdir/$f" || true)
    if [ -n "$hits" ]; then
        echo "check-device-skeleton: hand-folded numeric operand in $f:" >&2
        printf '%s\n' "$hits" >&2
        echo "Fold operands with the xpost_dev_num_to_* helpers." >&2
        fail=1
    fi
done

# 3a. A device that reaches its own buffer for a pixel reaches it for a
#    rectangle too. The base class fills a rectangle by walking it a pixel
#    at a time and calling PutPix for each, which is the right answer for
#    a device whose page is the base class's own row array and the wrong
#    one for a device that has put its buffer somewhere else: every call
#    goes out through the operator dispatch and comes back in. Every page
#    begins with an erasepage over the whole of it, so a device without
#    its own FillRect spends the page's area in dispatches before a
#    program has drawn anything -- twenty seconds, on a page two thousand
#    square, to reach the state the page starts in.
#
#    A line is not held to this. Its cost is its length, where a
#    rectangle's is the area of the page.
#
#    This is a cost, not a wrong answer, so no rendering shows it and no
#    assertion about what a device paints will catch it. Timing it in the
#    suite would answer differently on a busy machine. What can be said
#    for certain is which methods a device offers, so that is what is
#    asked.
for f in $marking; do
    if grep -q '"PutPix"' "$libdir/$f" && ! grep -q '"FillRect"' "$libdir/$f"; then
        echo "check-device-skeleton: $f reaches its own buffer for a pixel but not" >&2
        echo "      for a rectangle, so it fills one through the base class, a" >&2
        echo "      dispatch per pixel. Give it a FillRect beside its PutPix." >&2
        fail=1
    fi
done

# 3. A file that fills a rectangle paints the contract rectangle: its
#    extent arithmetic must be xpost_dev_rect_normalize, not a private
#    restatement. And nothing outside the header may restate the two
#    steps that arithmetic is made of -- reflecting a negative extent
#    through its origin, and clamping a coordinate to the device -- since
#    a restatement is how the four behaviours came about. The page's own
#    extent is not this arithmetic and is excepted by name below.
for f in $marking; do
    if grep -qE '"FillRect"|_fillrect' "$libdir/$f" &&
       ! grep -q 'xpost_dev_rect_normalize' "$libdir/$f"; then
        echo "check-device-skeleton: $f fills a rectangle without xpost_dev_rect_normalize()." >&2
        echo "The painted rectangle is defined once, in xpost_dev_driver.h." >&2
        fail=1
    fi
    # The extent of a page is a different question from the extent of a
    # painted rectangle, and it has its own single home. A rectangle of
    # negative extent names the same rectangle from the other corner, so
    # it is reflected; a page of negative extent is not a page at all and
    # is refused, by xpost_device_raster_bytes(). That function's body is
    # therefore read past, and the rule stands everywhere else.
    awk '/^[A-Za-z_].*\<xpost_device_raster_bytes\>[ \t]*\(/ { skip = 1 }
         skip && /^}/                                          { skip = 0; next }
         { if (!skip) print FNR ": " $0 }' "$libdir/$f" > "$work/rectscan"
    hits=$(grep -E '(w|h|width|height)[ \t]*(\.int_\.val)?[ \t]*<[ \t]*0|\bfloor[ \t]*\((dx|dy|x|y)\b' \
           "$work/rectscan" || true)
    if [ -n "$hits" ]; then
        echo "check-device-skeleton: $f restates the rectangle arithmetic:" >&2
        printf '%s\n' "$hits" >&2
        echo "Reflecting a negative extent and flooring a coordinate belong to" >&2
        echo "xpost_dev_rect_normalize(); call it instead." >&2
        fail=1
    fi
done

# 4. A file that draws a line walks the contract's line. The window
#    devices each had a walk of their own -- one including both
#    endpoints, one excluding the last -- so a wire drawn on one landed
#    on different pixels than the same wire on the other, and neither
#    matched the base class.
for f in $marking; do
    if grep -q '_drawline' "$libdir/$f" &&
       ! grep -q 'xpost_dev_line_init' "$libdir/$f"; then
        echo "check-device-skeleton: $f draws a line without xpost_dev_line_init()." >&2
        echo "The painted line is defined once, in xpost_dev_driver.h." >&2
        fail=1
    fi
done

# 5. Every marking source includes the contract header it is held to.
for f in $marking; do
    if ! grep -q 'xpost_dev_driver\.h' "$libdir/$f"; then
        echo "check-device-skeleton: $f does not include xpost_dev_driver.h." >&2
        fail=1
    fi
done

# 6. A class dictionary that would not take a method leaves the device
#    incomplete, so the refusal reaches the caller: the value of every
#    xpost_dict_put is either returned or tested, and a test never
#    answers success. Textual, so it holds for the sources this platform
#    cannot compile as well as the ones it can.
for f in $marking; do
    hits=$(awk '
        /xpost_dict_put[ \t]*\(/ {
            if ($0 !~ /=/ && $0 !~ /return/)
                printf "%s:%d: the refusal is discarded\n", FILENAME, FNR
            win = 8; sawif = 0; next
        }
        win > 0 {
            win--
            if ($0 ~ /^[ \t]*if \(ret\)/) { sawif = 1; next }
            if (sawif && $0 ~ /^[ \t]*return 0;/)
                printf "%s:%d: the refusal is answered with success\n", FILENAME, FNR
            sawif = 0
        }
    ' "$libdir/$f")
    if [ -n "$hits" ]; then
        echo "check-device-skeleton: registration refusal ignored in $f:" >&2
        printf '%s\n' "$hits" >&2
        echo "A device that could not register a method must not load." >&2
        fail=1
    fi
done

# 7. The class-to-instance copy is one procedure. A class dictionary
#    stores /.copydict, and Create (and every C driver, which fetches it
#    from the class before specialising the copy) calls it. Each class
#    used to carry its own body, and two of them carried a shorter one
#    that left the output file name off the instance, so a device made
#    from those classes wrote wherever its Emit defaulted to. A class
#    may name .classcopydict; it may not restate it.
copies=0
for f in $classes; do
    p="$src/data/$f"
    [ -f "$p" ] || continue
    if grep -qE '^[ \t]*/\.copydict[ \t]*\{' "$p"; then
        echo "check-device-skeleton: $f writes a class copy of its own:" >&2
        grep -nE '^[ \t]*/\.copydict[ \t]*\{' "$p" >&2
        echo "The copy is .xpostsys /.classcopydict; store that, do not restate it." >&2
        fail=1
    fi
    grep -qE '/\.copydict[ \t]+//\.xpostsys[ \t]+/\.classcopydict[ \t]+get' "$p" &&
        copies=$((copies + 1))
done
if [ "$copies" -lt 5 ]; then
    echo "check-device-skeleton: only $copies classes store the shared class copy;" >&2
    echo "expected every class that defines /.copydict to name .classcopydict." >&2
    fail=1
fi
if [ "$(grep -c '\.xpostsys /\.classcopydict {' "$src/data/device.ps")" != 1 ]; then
    echo "check-device-skeleton: the shared class copy is not defined once in device.ps." >&2
    fail=1
fi

# 8. A device is completed once, identically, on every path that
#    creates one. The finishing a fresh device takes -- the page's
#    default matrix, the compiled rasterisers its raster shape can
#    take, the process colour model it was asked for -- is
#    .completedevice, and only it installs them. It was written twice,
#    for the device the interpreter starts with and the device
#    setpagedevice makes, and the two were not the same: one adopted the
#    process colour model and the other did not, so the same device
#    behaved differently according to how it had been selected.
#
#    Two sites install a compiled rasteriser on a device that is not a
#    page device and never becomes one -- the glyph cache in font.ps and
#    the form cache in init.ps, each a scratch raster the machinery
#    paints into and reads back. They are named here rather than left to
#    slip through a looser pattern.
scratch=0
for f in device.ps font.ps init.ps image.ps pgmimage.ps pbmimage.ps \
         ppmimage.ps tiffimage.ps nulldev.ps bboxdev.ps pdfwrite.ps \
         svgwrite.ps dscwrite.ps paint.ps graphics.ps gstate.ps; do
    p="$src/data/$f"
    [ -f "$p" ] || continue
    while IFS= read -r hit; do
        [ -n "$hit" ] || continue
        case "$f:$hit" in
            device.ps:*"dev /Fill"*)   continue ;;   # .completedevice itself
            font.ps:*"mdev /Fill"*)    scratch=$((scratch + 1)); continue ;;
            init.ps:*"mdev /Fill"*)    scratch=$((scratch + 1)); continue ;;
        esac
        echo "check-device-skeleton: a device is completed outside .completedevice:" >&2
        echo "  $f:$hit" >&2
        echo "A page device is finished by .completedevice (data/device.ps); the only" >&2
        echo "sites that may install a rasteriser directly are the two scratch rasters," >&2
        echo "font.ps's glyph cache and init.ps's form cache, both named mdev." >&2
        fail=1
    done <<EOF
$(grep -nE '/(FillPoly|FillRect)([ \t]+//\.internaldict|$)' "$p" || true)
EOF
done
if [ "$scratch" -ne 4 ]; then
    echo "check-device-skeleton: $scratch scratch-raster completions, expected 4." >&2
    echo "A new one is another place a device gets finished; give it" >&2
    echo ".completedevice or add it here with its reason." >&2
    fail=1
fi
if [ "$(grep -c '\.privatedict /\.completedevice {' "$src/data/device.ps")" != 1 ]; then
    echo "check-device-skeleton: the device completion is not defined once in device.ps." >&2
    fail=1
fi
# summed with awk rather than bc: bc is not present in every environment
# this runs in, and a guard that cannot run is a guard that is not checking
# counted over the concatenation rather than per file: grep -c prefixes
# each count with the path, and a windows path carries a drive-letter
# colon, so splitting on the colon takes the path for the count and the
# check reads zero on the platform it most needs to run on
callers=$(cat "$src/data/device.ps" "$src/data/init.ps" \
          | grep -c '/\.completedevice get exec')
if [ "$callers" -lt 2 ]; then
    echo "check-device-skeleton: only $callers path completes a device;" >&2
    echo "both the startup device and setpagedevice's must call .completedevice." >&2
    fail=1
fi

# 9. A device's methods are registered from its method table, not one
#    at a time. Written out by hand, each registration carried its own
#    arity, its own operand types and its own put, and five of six
#    devices answered success from a failed PutPix registration -- the
#    device loaded with no PutPix and failed at its first paint. The
#    table states the slot and the kind; xpost_dev_class_install derives
#    the arity from the declared colour space, stops at the first
#    refusal, and checks what it produced.
for f in $fleet xpost_dev_win32.c; do
    if ! grep -q 'Xpost_Dev_Method methods\[\]' "$libdir/$f"; then
        echo "check-device-skeleton: $f has no method table." >&2
        echo "Register a device's suite through xpost_dev_class_install()." >&2
        fail=1
    fi
    if ! grep -q 'xpost_dev_class_install' "$libdir/$f"; then
        echo "check-device-skeleton: $f does not install its class through the contract." >&2
        fail=1
    fi
    # a method slot put into the class dictionary outside the table is a
    # registration the completeness check never sees
    hits=$(grep -nE 'xpost_dict_put\(ctx, classdic, xpost_name_cons\(ctx, "(Create|PutPix|GetPix|DrawLine|DrawRect|FillRect|FillPoly|BlendPix|Emit|Flush|Destroy|Erase)"\)' \
           "$libdir/$f" || true)
    if [ -n "$hits" ]; then
        echo "check-device-skeleton: $f installs a method slot outside its table:" >&2
        printf '%s\n' "$hits" >&2
        fail=1
    fi
done

# 10. The contract's list of slots that read the base class's raster is
#     the classes' list. The completeness check refuses a device that
#     keeps its own buffer and leaves one of them inherited; if a class
#     grows another such method and the header does not hear about it,
#     the check goes on passing while the hole reopens.
#
#     Only the names the pipeline looks up count. A dot-prefixed name is
#     a parameter of the generated raster suite -- .writepage reads the
#     row array too, but nothing reaches it except Emit, which is on the
#     list, so a device that overrides that never runs it.
#
#     A slot is read whether the class states it as a body of its own or
#     fills it with a compiled operator, since such an operator reads the
#     row array out of the instance dictionary it is handed. Both are
#     collected, or a slot filled from C reads as no slot at all: that is
#     what BlendPix was, and a device that keeps its own raster inherited
#     a blend with nothing to blend into.
sed -n 's/^#define XPOST_DEV_RASTER_SLOTS { \(.*\) }$/\1/p' \
    "$libdir/xpost_dev_driver.h" | tr -d '" ' | tr ',' '\n' \
    | grep -v '^$' | sort > "$work/hdr"
classfiles="$src/data/image.ps $src/data/pgmimage.ps $src/data/ppmimage.ps
            $src/data/pbmimage.ps $src/data/tiffimage.ps"
# the methods in the classes whose body reads ImgData
awk '
    /^[ \t]*\/[A-Za-z.][A-Za-z0-9._]*[ \t]*\{/ { m = $1; sub(/^\//, "", m) }
    m != "" && /ImgData/ { print m; m = "" }
' $classfiles | grep -v '^\.' | sort -u > "$work/cls"

# the compiled operators that read the row array, under the names
# PostScript reaches them by: which C function each registered name
# names, and whether that function's body reads ImgData. The rasteriser
# is read twice, once for each question.
awk '
    NR == FNR {
        if ($0 ~ /^}/) { if (fn != "" && saw) reads[fn] = 1; fn = ""; saw = 0 }
        else if ($0 ~ /^[A-Za-z_]/ && $0 ~ /\(/ && $0 !~ /;[ \t]*$/ &&
                 match($0, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
            fn = substr($0, RSTART, RLENGTH)
            sub(/[ \t]*\(.*$/, "", fn)
            saw = 0
        }
        if (fn != "" && $0 ~ /nameImgData/) saw = 1
        next
    }
    match($0, /xpost_operator_cons\(ctx, "\.[A-Za-z0-9_]+",[ \t]*\(Xpost_Op_Func\)[A-Za-z0-9_]+/) {
        s = substr($0, RSTART, RLENGTH)
        ps = s; sub(/^[^"]*"/, "", ps); sub(/".*$/, "", ps)
        f = s; sub(/^.*\(Xpost_Op_Func\)/, "", f)
        if (reads[f]) print ps
    }
' "$libdir/xpost_dev_generic.c" "$libdir/xpost_dev_generic.c" \
    | sort -u > "$work/rowops"
if [ ! -s "$work/rowops" ]; then
    echo "FAILURES: no compiled operator reads the row array; fix the guard" >&2
    exit 1
fi
# the class slots those operators are stored in
sed -n 's|^[ \t]*/\([A-Za-z][A-Za-z0-9_]*\)[ \t]*//\.internaldict[ \t]*/\(\.[A-Za-z0-9_]*\)[ \t]*get.*|\2 \1|p' \
    $classfiles | sort -u > "$work/clsops"
if [ ! -s "$work/clsops" ]; then
    echo "FAILURES: no class slot is filled from the rasteriser; fix the guard" >&2
    exit 1
fi
while read -r op slot; do
    grep -qx "$op" "$work/rowops" && echo "$slot"
done < "$work/clsops" >> "$work/cls"
sort -u -o "$work/cls" "$work/cls"

if [ ! -s "$work/hdr" ] || [ ! -s "$work/cls" ]; then
    echo "FAILURES: the raster-slot lists could not be read; fix the guard" >&2
    exit 1
fi
if ! cmp -s "$work/hdr" "$work/cls"; then
    echo "check-device-skeleton: XPOST_DEV_RASTER_SLOTS and the class methods" >&2
    echo "that read the row array disagree:" >&2
    missing=$(comm -13 "$work/hdr" "$work/cls")
    stale=$(comm -23 "$work/hdr" "$work/cls")
    [ -n "$missing" ] && {
        echo "  a class method reads the row array and the contract does not name it:" >&2
        printf '%s\n' "$missing" | sed 's/^/      /' >&2
        echo "  a device with its own buffer would inherit it and answer undefined." >&2
    }
    [ -n "$stale" ] && {
        echo "  the contract names a slot no class method reads:" >&2
        printf '%s\n' "$stale" | sed 's/^/      /' >&2
    }
    fail=1
fi

# 11. A device that keeps its raster in a buffer of its own names every
#     one of those slots in its own method table.
#
#     xpost_dev_class_install says the same thing and refuses a device
#     that does not, but only where the device can be built and loaded.
#     A driver this platform cannot compile is never held to it at all,
#     and that is where the hole lasted: both window devices inherited
#     the blend, and each declared one bit of text alpha, so the text
#     path never reached the slot and nothing else asked.
#
#     Which devices are held is read from the install call rather than
#     listed: the argument after the component count says whether the
#     raster is the device's own. A file whose call this cannot read
#     fails here rather than being passed over.
compiled=0
for f in $fleet xpost_dev_win32.c; do
    own=$(sed -n 's/.*xpost_dev_class_install(ctx, classdic, [0-9][0-9]*, \([01]\),.*/\1/p' \
          "$libdir/$f" | sort -u | tr -d '\n')
    case "$own" in
        0) continue ;;
        1) ;;
        *) echo "check-device-skeleton: cannot read from $f whether its raster is" >&2
           echo "its own; xpost_dev_class_install is the one call that says so." >&2
           fail=1
           continue ;;
    esac
    compiled=$((compiled + 1))
    while read -r slot; do
        if ! grep -qE "\{[ \t]*\"$slot\"[ \t]*," "$libdir/$f"; then
            echo "check-device-skeleton: $f keeps its own raster and its method" >&2
            echo "table has no $slot, so it inherits one that reads the base" >&2
            echo "class's row array and answers undefined when it is reached." >&2
            fail=1
        fi
    done < "$work/hdr"
done
if [ "$compiled" -eq 0 ]; then
    echo "FAILURES: no device was held to the raster slots; fix the guard" >&2
    exit 1
fi

# 12. What the completion installs a compiled rasteriser under is asked
#     once, and asked before the install.
#
#     Neither compiled fill serves every device. The rectangle fills
#     write the raster as the array of row strings the base classes keep
#     under /ImgData, so a device with a buffer of its own has nothing
#     for them to write; the polygon fill paints in the colour spaces it
#     knows a component count for. A device that does not match keeps
#     what it brought, and a device given the compiled one anyway
#     answers at its first mark rather than at load.
#
#     The condition belongs to the fill, not to a colour space, and
#     writing it out beside each fill is how the two came to disagree:
#     the colour path stated it and the grey path did not, so a device
#     declaring DeviceGray with a buffer of its own had a working
#     rectangle fill replaced by one that answered undefined. So the
#     count is the rule -- one statement of each condition, ahead of
#     every install -- rather than a check that each install has its
#     own, which is what the two-statement form would pass.
#
#     This is a shape a rendering cannot show: every device here that
#     keeps its own buffer declares DeviceRGB, so the grey path is
#     unreached by the fleet and no page comes out different.
#     tests/device_completion_test.ps asks what the completion does; this
#     asks that there is one place where it is decided.
awk '
    /\.privatedict \/\.completedevice \{/          { on = 1 }
    !on                                            { next }
    /\/ImgData known/               { img++;  imgl = FNR }
    /\/operatortype ne/             { own++;  ownl = FNR }
    /\/\.fillrect[a-z]* get put/    { rect++; if (!rectl) rectl = FNR }
    /\/Device[A-Za-z]* eq/          { if (!csl) csl = FNR }
    /\/\.fillpoly get put/          { poly++; polyl = FNR }
    /^\} bind put/                  { on = 0 }
    END { print img+0, imgl+0, own+0, ownl+0, rect+0, rectl+0, \
                csl+0, poly+0, polyl+0 }
' "$src/data/device.ps" > "$work/completion"
read -r c_img c_imgl c_own c_ownl c_rect c_rectl c_csl c_poly c_polyl \
     < "$work/completion"
if [ "$c_rect" -lt 2 ] || [ "$c_poly" -ne 1 ]; then
    echo "check-device-skeleton: .completedevice installs $c_rect compiled" >&2
    echo "rectangle fills and $c_poly polygon fill; expected both rectangle" >&2
    echo "fills and the one polygon fill. A fill that stopped being installed" >&2
    echo "is a device rasterising a page through the interpreter." >&2
    fail=1
elif [ "$c_img" -ne 1 ] || [ "$c_own" -ne 1 ]; then
    echo "check-device-skeleton: the compiled rectangle fills are installed" >&2
    echo "under the raster condition stated $c_img times and the condition on" >&2
    echo "the fill the device brought stated $c_own times; expected one of" >&2
    echo "each. Both belong to the fill rather than to a colour space, so" >&2
    echo "both are asked once and the colour space chooses only which fill" >&2
    echo "is installed." >&2
    fail=1
elif [ "$c_rectl" -lt "$c_imgl" ] || [ "$c_rectl" -lt "$c_ownl" ]; then
    echo "check-device-skeleton: a compiled rectangle fill is installed at" >&2
    echo "data/device.ps:$c_rectl, ahead of the conditions at line $c_imgl and" >&2
    echo "line $c_ownl, so it lands on a device those conditions would have" >&2
    echo "spared: one holding its raster in a buffer of its own, or one that" >&2
    echo "brought a compiled rectangle fill already." >&2
    fail=1
elif [ "$c_csl" -eq 0 ] || [ "$c_csl" -gt "$c_polyl" ]; then
    echo "check-device-skeleton: the compiled polygon fill is installed at" >&2
    echo "data/device.ps:$c_polyl with no colour-space test ahead of it, so a" >&2
    echo "device declaring a space that fill paints no colour in loses the" >&2
    echo "polygon fill it brought." >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "check-device-skeleton: ok (fleet behind the driver contract, $copies classes behind one copy, $callers paths behind one completion, $compiled devices behind the raster slots)"
