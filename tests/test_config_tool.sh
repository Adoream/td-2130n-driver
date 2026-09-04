#!/bin/sh
set -eu

mkdir -p build/test-output
root="$(mktemp -d build/test-output/config.XXXXXX)"
trap 'rm -rf "$root"' EXIT HUP INT TERM
inf="$root/opt/brother/PTouch/td2130n/inf"
mkdir -p "$inf"
cp tests/fixtures/td2130-config-schema.fixture "$inf/brtd2130nfunc"
cp tests/fixtures/td2130-config-rc.fixture "$inf/brtd2130nrc"
cp "$inf/brtd2130nrc" "$root/original-rc"

./build/td2130-config >/dev/null
set +e
./build/td2130-config -copy 2 >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 2

./build/td2130-config -P Test -copy 3 -cutlabel 30 -cutend OFF \
    -trimtape ON -compress ON -brit -25 -cont 50 -half DITHER \
    -mirro ON -rotate ON -peeler ON -quality QUALITY -reso 203 \
    -feed 30 -media 40x50 -collate ON --root "$root"
grep -q '^Copies=3$' "$inf/brtd2130nrc"
grep -q '^Brightness=-25$' "$inf/brtd2130nrc"
grep -q '^Contrast=50$' "$inf/brtd2130nrc"
grep -q '^Halftone=DITHER$' "$inf/brtd2130nrc"
grep -q '^Trimtape=ON$' "$inf/brtd2130nrc"
grep -q '^Compress=ON$' "$inf/brtd2130nrc"
grep -q '^MirrorPrinting=ON$' "$inf/brtd2130nrc"
grep -q '^RotatePrinting=ON$' "$inf/brtd2130nrc"
grep -q '^Peeler=ON$' "$inf/brtd2130nrc"
grep -q '^Quality=QUALITY$' "$inf/brtd2130nrc"
grep -q '^Feed=30$' "$inf/brtd2130nrc"
grep -q '^Collate=ON$' "$inf/brtd2130nrc"
grep -q '^Resolution=203$' "$inf/brtd2130nrc"
grep -q '^MediaSize=40x50$' "$inf/brtd2130nrc"
test "$(grep -c '^Copies=' "$inf/brtd2130nrc")" -eq 1
grep -q '^KeepThis=unchanged$' "$inf/brtd2130nrc"
test -f "$inf/brtd2130nrc.old"
cmp "$root/original-rc" "$inf/brtd2130nrc.old"

before="$(cksum "$inf/brtd2130nrc")"
set +e
./build/td2130-config -P Test -copy 99 --root "$root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 12
test "$before" = "$(cksum "$inf/brtd2130nrc")"

set +e
./build/td2130-config -P Test -cutend ON --root "$root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 11
test "$before" = "$(cksum "$inf/brtd2130nrc")"

set +e
./build/td2130-config -P Test -cutend BAD --root "$root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 2
test "$before" = "$(cksum "$inf/brtd2130nrc")"

sed 's/^Quality={SPEED,QUALITY}$/Quality={SPEED,QUALITY,BROKEN}/' \
    "$inf/brtd2130nfunc" > "$inf/brtd2130nfunc.new"
mv "$inf/brtd2130nfunc.new" "$inf/brtd2130nfunc"
set +e
./build/td2130-config -P Test -quality BROKEN --root "$root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 2
test "$before" = "$(cksum "$inf/brtd2130nrc")"

awk '{ if ($0 == "[CustomTapeEnd]") print "BrL06ZZZZZZZZZZ/Custom"; print }' \
    "$inf/brtd2130nfunc" > "$inf/brtd2130nfunc.new"
mv "$inf/brtd2130nfunc.new" "$inf/brtd2130nfunc"
set +e
./build/td2130-config -P Test -media Custom --root "$root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 2
test "$before" = "$(cksum "$inf/brtd2130nrc")"

awk '{ if ($0 == "[CustomTapeEnd]") print "BrL069999999999/Custom"; print }' \
    "$inf/brtd2130nfunc" > "$inf/brtd2130nfunc.new"
mv "$inf/brtd2130nfunc.new" "$inf/brtd2130nfunc"
./build/td2130-config -P Test -media Custom --root "$root"
grep -q '^MediaSize=BrL069999999999$' "$inf/brtd2130nrc"

before="$(cksum "$inf/brtd2130nrc")"
set +e
./build/td2130-config -P Test -media Missing --root "$root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 2
test "$before" = "$(cksum "$inf/brtd2130nrc")"

long_value="$(awk 'BEGIN { for (i = 0; i < 128; ++i) printf "A" }')"
set +e
./build/td2130-config -P Test -media "$long_value" --root "$root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 2
test "$before" = "$(cksum "$inf/brtd2130nrc")"

./build/td2130-config -P Test -copy 2 -copy 4 --root "$root"
grep -q '^Copies=4$' "$inf/brtd2130nrc"
test "$(grep -c '^Copies=' "$inf/brtd2130nrc")" -eq 1

./build/td2130-config -P Test --root "$root" --show >/dev/null
./build/td2130-config -P Test --root "$root" >/dev/null

official_root="$(mktemp -d build/test-output/config-official.XXXXXX)"
official_inf="$official_root/opt/brother/PTouch/td2130n/inf"
mkdir -p "$official_inf"
sed 's/^Resolution={203,300}$/Resolution={300}/' \
    tests/fixtures/td2130-config-schema.fixture > "$official_inf/brtd2130nfunc"
cp tests/fixtures/td2130-config-rc.fixture "$official_inf/brtd2130nrc"
set +e
./build/td2130-config -P Test -reso 203 --root "$official_root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 11
rm -rf "$official_root"

crlf_root="$(mktemp -d build/test-output/config-crlf.XXXXXX)"
crlf_inf="$crlf_root/opt/brother/PTouch/td2130n/inf"
mkdir -p "$crlf_inf"
awk '{ printf "%s\r\n", $0 }' tests/fixtures/td2130-config-schema.fixture \
    > "$crlf_inf/brtd2130nfunc"
awk '{ printf "%s\r\n", $0 }' tests/fixtures/td2130-config-rc.fixture \
    > "$crlf_inf/brtd2130nrc"
./build/td2130-config -P Test -copy 2 --root "$crlf_root"
test "$(tr -cd '\r' < "$crlf_inf/brtd2130nrc" | wc -c)" -eq \
     "$(tr -cd '\n' < "$crlf_inf/brtd2130nrc" | wc -c)"
rm -rf "$crlf_root"

duplicate_root="$(mktemp -d build/test-output/config-duplicate.XXXXXX)"
duplicate_inf="$duplicate_root/opt/brother/PTouch/td2130n/inf"
mkdir -p "$duplicate_inf"
cp tests/fixtures/td2130-config-schema.fixture "$duplicate_inf/brtd2130nfunc"
cp tests/fixtures/td2130-config-rc.fixture "$duplicate_inf/brtd2130nrc"
printf '\n[td2130n]\n' >> "$duplicate_inf/brtd2130nrc"
set +e
./build/td2130-config -P Test -copy 2 --root "$duplicate_root" >/dev/null 2>&1
status=$?
set -e
test "$status" -eq 5
rm -rf "$duplicate_root"

echo "config tool tests passed"
