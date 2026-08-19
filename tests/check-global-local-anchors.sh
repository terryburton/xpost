#!/bin/sh
# Meson test wrapper: hold tests/global_local_anchors.register to the
# code, and hold the collector to the register.
#
# A global container may hold no reference into local memory
# (PLRM 3.7.2); the store-time barrier refuses the store. The sanctioned
# exception is systemdict, whose local entries the interpreter puts
# through the ignoreinvalidaccess windows the register enumerates. The
# collector leans on that enumeration: a collection of local vm alone
# marks the local roots plus what systemdict holds in local vm, and
# walks nothing else outside the bank.
#
# Four things are held here, each in both directions where a direction
# exists:
#   1. every window in the code is a row in the register, and every row
#      is a window in the code -- derived from the assignments that open
#      the barrier, not from a list kept beside them;
#   2. every dictionary store a window makes targets systemdict, and the
#      names those stores put are exactly the register's name rows;
#   3. each name row survives a collection of local vm alone, read back
#      through systemdict afterwards, with the collector's own
#      reachability verifier and cross-bank scan required to be silent
#      over the same run;
#   4. a store of a local value through an unregistered route -- an
#      ordinary put into a global dictionary or array -- is refused with
#      invalidaccess.
#
#   $1  path to the source tree
#   $2  path to the built xpost binary
set -u
src=$1
xpost=$2
. "$(dirname "$0")/verdict.sh"
. "$(dirname "$0")/guard-paths.sh"

verdict_workdir

# read the sources and the register through a mirror, so a wrong source
# root fails here rather than reporting truths about a tree the caller
# did not mean
guard_mirror_tree "$src"
src=$mirror

lib="$src/src/lib"
guard_require_dir "$lib" "the library source directory"
register="$src/tests/global_local_anchors.register"
guard_require_file "$register" "the register of global-to-local anchors"

fail=0

# the rows, by kind
awk '!/^#/ && NF>=3 && $2=="window" {print $1}' "$register" | sort -u > "$work/reg_windows"
awk '!/^#/ && NF>=3 && $2=="name" {print $1}' "$register" | sort -u > "$work/reg_names"
if [ ! -s "$work/reg_windows" ] || [ ! -s "$work/reg_names" ]; then
    echo "FAILURES: the register has no window rows or no name rows;"
    echo "      this check is reading the wrong file"
    exit 1
fi

# 1. the windows, derived from the code: every assignment that opens the
# barrier, tagged with the function it sits in. The function is the last
# definition line above the assignment -- a line beginning a top-level
# definition, its name just before the parenthesis.
: > "$work/code_windows"
for f in "$lib"/*.c; do
    base=$(basename "$f")
    awk -v base="$base" '
        /^[A-Za-z_][^;={]*\(/ {
            line = $0
            sub(/\(.*/, "", line)
            n = split(line, parts, /[^A-Za-z0-9_]+/)
            fn = ""
            for (i = n; i >= 1; i--)
                if (parts[i] != "") { fn = parts[i]; break }
        }
        /ignoreinvalidaccess = 1/ {
            if (fn == "")
                fn = "OUTSIDE-ANY-FUNCTION"
            print base ":" fn
        }
    ' "$f" >> "$work/code_windows"
done
sort -u "$work/code_windows" -o "$work/code_windows"

if ! cmp -s "$work/reg_windows" "$work/code_windows"; then
    echo "FAILURES: the barrier windows in the code and the register's"
    echo "      window rows differ. A new window is a new route for a"
    echo "      global-to-local reference, which the local-only mark"
    echo "      must anchor; say in tests/global_local_anchors.register"
    echo "      what it stores and why, or close the window."
    echo "  register:"
    sed 's/^/      /' "$work/reg_windows"
    echo "  code:"
    sed 's/^/      /' "$work/code_windows"
    fail=1
fi

# 2. what the windows store: every dictionary put a window makes must
# target sd (systemdict, the sanctioned exception), no window may put
# into an array, and the names stored are exactly the name rows.
: > "$work/code_names"
while IFS=: read -r file func; do
    body="$work/body_$func"
    awk -v fn="$func" '
        !inside && /^[A-Za-z_]/ && index($0, fn "(") { inside = 1 }
        inside { print; if (/^}/) exit }
    ' "$lib/$file" > "$body"
    if [ ! -s "$body" ]; then
        echo "FAILURES: window $file:$func has no definition to read"
        fail=1
        continue
    fi
    if grep -F 'xpost_dict_put' "$body" | grep -vF 'xpost_dict_put(ctx, sd,' | grep -q .; then
        echo "FAILURES: window $file:$func stores into a dictionary other"
        echo "      than systemdict while the barrier is open:"
        grep -F 'xpost_dict_put' "$body" | grep -vF 'xpost_dict_put(ctx, sd,' | sed 's/^/      /'
        fail=1
    fi
    if grep -q 'xpost_array_put' "$body"; then
        echo "FAILURES: window $file:$func stores into an array while the"
        echo "      barrier is open; no array is a sanctioned exception"
        fail=1
    fi
    sed -n 's/.*xpost_dict_put(ctx, sd, xpost_name_cons(ctx, "\([^"]*\)").*/\1/p' \
        "$body" >> "$work/code_names"
done < "$work/reg_windows"
sort -u "$work/code_names" -o "$work/code_names"

if ! cmp -s "$work/reg_names" "$work/code_names"; then
    echo "FAILURES: the names the windows store and the register's name"
    echo "      rows differ. Each name a window puts into systemdict is a"
    echo "      local dictionary a local-only collection must keep alive;"
    echo "      enter it in tests/global_local_anchors.register with its"
    echo "      reason, or stop storing it."
    echo "  register:"
    sed 's/^/      /' "$work/reg_names"
    echo "  code:"
    sed 's/^/      /' "$work/code_names"
    fail=1
fi

[ "$fail" -ne 0 ] && exit 1

# 3. survival: churn garbage, collect local vm alone, and read every
# name row back through systemdict. The reads never write, so nothing
# here roots the anchored dictionaries through the save machinery: the
# anchor is the only thing keeping them. The collector's independent
# reachability verifier and the cross-bank scan run over the same
# collections and must be silent; the census line is the evidence they
# ran at all.
{
    echo '100 { 50 array pop 50 string pop 5 dict pop { 1 2 add } pop } repeat'
    echo '1 vmreclaim'
    echo '100 { 50 array pop 50 string pop 5 dict pop } repeat'
    echo '1 vmreclaim'
    while IFS= read -r name; do
        cat <<EOF
systemdict /$name known
{ systemdict /$name get dup type /dicttype eq
  { { pop pop } forall (row OK $name) = }
  { pop (row FAIL $name: not a dictionary) = } ifelse }
{ (row FAIL $name: not in systemdict) = } ifelse
EOF
    done < "$work/reg_names"
    cat <<'PROBES'
% the operator table (the register's table row) holds procedures in
% either bank; the wrapped accessors of per-context local state keep
% theirs in local vm, reachable through no container, so each must
% still answer after the collections above. The churn between the
% collections and these reads matters: storage a sweep wrongly took
% back keeps its contents until something reuses it, so the reuse is
% forced before the read
300 { 3 array pop 5 array pop 8 array pop 5 dict pop } repeat
systemdict /.gscratch known
{ .gscratch type /dicttype eq
  { (row OK table .gscratch) = } { (row FAIL table .gscratch) = } ifelse }
{ (row OK table .gscratch absent in this configuration) = } ifelse
/graphicsdict where
{ pop graphicsdict type /dicttype eq
  { (row OK table graphicsdict) = } { (row FAIL table graphicsdict) = } ifelse }
{ (row OK table graphicsdict absent in this configuration) = } ifelse
/DEVICE where
{ pop DEVICE type /dicttype eq
  { (row OK table DEVICE) = } { (row FAIL table DEVICE) = } ifelse }
{ (row OK table DEVICE absent in this configuration) = } ifelse
PROBES
    echo '(SUCCESS) ='
} > "$work/survival.ps"

out=$(XPOST_GC_VERIFY=1 XPOST_GC_XBANK_CHECK=1 XPOST_GC_CENSUS=1 \
      "$xpost" -q --no-sandbox -d null "$work/survival.ps" </dev/null 2>&1)
status=$?
printf '%s\n' "$out"
if [ "$status" -ne 0 ]; then
    echo "FAILURES: the survival run exited with status $status"
    exit 1
fi
verdict_ok "$out" "the survival run" || exit 1
for name in $(cat "$work/reg_names") "table"; do
    if ! printf '%s\n' "$out" | grep -qF "row OK $name"; then
        echo "FAILURES: the register row $name did not survive the"
        echo "      collection of local vm alone"
        fail=1
    fi
done
if ! printf '%s\n' "$out" | grep -q '^CENSUS:'; then
    echo "FAILURES: the collector diagnostics did not run"
    fail=1
fi
if printf '%s\n' "$out" | grep -qE '^(VERIFY GAP|VERIFY:|XBANK:)'; then
    echo "FAILURES: the collector diagnostics reported the above"
    fail=1
fi

[ "$fail" -ne 0 ] && exit 1

# 4. the unregistered route: an ordinary store of a local value into a
# global container must be refused at store time. This is what makes
# the register complete -- a reference that cannot be stored cannot need
# an anchor.
cat > "$work/refusal.ps" <<'EOF'
/la [ 1 2 3 ] def
mark
true setglobal
{ 2 dict /k la put } stopped
false setglobal
{ (dict store: ) print $error /errorname get = }
{ (row FAIL: a local value was stored into a global dictionary) = } ifelse
cleartomark
mark
true setglobal
{ 3 array 0 la put } stopped
false setglobal
{ (array store: ) print $error /errorname get = }
{ (row FAIL: a local value was stored into a global array) = } ifelse
cleartomark
(SUCCESS) =
EOF

out=$("$xpost" -q --no-sandbox -d null "$work/refusal.ps" </dev/null 2>&1)
status=$?
printf '%s\n' "$out"
if [ "$status" -ne 0 ]; then
    echo "FAILURES: the refusal run exited with status $status"
    exit 1
fi
verdict_ok "$out" "the refusal run" || exit 1
if ! printf '%s\n' "$out" | grep -q '^dict store: invalidaccess'; then
    echo "FAILURES: the dictionary store of a local value into global vm"
    echo "      was not refused with invalidaccess"
    fail=1
fi
if ! printf '%s\n' "$out" | grep -q '^array store: invalidaccess'; then
    echo "FAILURES: the array store of a local value into global vm"
    echo "      was not refused with invalidaccess"
    fail=1
fi

[ "$fail" -ne 0 ] && exit 1

echo "GLOBAL-LOCAL ANCHORS CLEAN"
