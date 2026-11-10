#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tool=$script_dir/../manage.sh
scratch=$(mktemp -d "${TMPDIR:-/tmp}/grandleon-upstream-test.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM

upstream=$scratch/upstream
topic=$scratch/topic
patch_root=$scratch/patch-root
engine_dir=$patch_root/engines/example
mkdir -p "$upstream" "$engine_dir"

git -C "$upstream" init --quiet
git -C "$upstream" config user.name 'Patch Test'
git -C "$upstream" config user.email 'patch-test@example.invalid'
printf 'base\n' >"$upstream/value.txt"
git -C "$upstream" add value.txt
git -C "$upstream" commit --quiet -m 'Base'
base=$(git -C "$upstream" rev-parse HEAD)

git clone --quiet "$upstream" "$topic"
git -C "$topic" config user.name 'Patch Test'
git -C "$topic" config user.email 'patch-test@example.invalid'
printf 'first\n' >>"$topic/value.txt"
git -C "$topic" commit --quiet -am 'Add first line'
printf 'second\n' >>"$topic/value.txt"
git -C "$topic" commit --quiet -am 'Add second line'

cat >"$engine_dir/upstream.env" <<EOF
UPSTREAM_NAME=example
UPSTREAM_URL=$upstream
UPSTREAM_REVISION=$base
EOF
git -C "$topic" format-patch --quiet --no-stat --full-index \
    --output-directory "$engine_dir" "$base..HEAD"
find "$engine_dir" -maxdepth 1 -name '*.patch' -printf '%f\n' | sort >"$engine_dir/series"

GRANDLEON_UPSTREAM_PATCH_ROOT=$patch_root "$tool" verify example >/dev/null
git clone --quiet "$upstream" "$scratch/apply"
GRANDLEON_UPSTREAM_PATCH_ROOT=$patch_root "$tool" apply example "$scratch/apply" >/dev/null
expected='base
first
second'
actual=$(cat "$scratch/apply/value.txt")
test "$actual" = "$expected"

GRANDLEON_UPSTREAM_PATCH_ROOT=$patch_root "$tool" check example "$upstream" >/dev/null
GRANDLEON_UPSTREAM_PATCH_ROOT=$patch_root "$tool" export example "$topic" "$scratch/export" >/dev/null
test "$(sed -e '/^[[:space:]]*$/d' "$scratch/export/series" | wc -l)" -eq 2
grep -q "UPSTREAM_BASE_REVISION=$base" "$scratch/export/export-metadata.env"
grep -q 'PATCH_COUNT=2' "$scratch/export/export-metadata.env"

cutover_root=$scratch/cutover-root
mkdir -p "$cutover_root/new-engine" "$cutover_root/exports"
printf 'new renderer only\n' >"$cutover_root/exports/build.sh"
printf 'header GRANDLEON_RENDERER=example trailer\n' >"$scratch/game.bin"
printf 'engine\texample\nrequired-path\tnew-engine\nforbidden-path\tlegacy-engine\nforbidden-text\texports\told_renderer|legacy_renderer\nartifact-text\tGRANDLEON_RENDERER=example\n' \
    >"$scratch/cutover.tsv"
"$script_dir/../cutover-audit.sh" \
    "$scratch/cutover.tsv" "$cutover_root" "$scratch/game.bin" >/dev/null
mkdir "$cutover_root/legacy-engine"
if "$script_dir/../cutover-audit.sh" \
    "$scratch/cutover.tsv" "$cutover_root" "$scratch/game.bin" >/dev/null 2>&1; then
    printf '%s\n' 'cutover audit accepted a legacy renderer path' >&2
    exit 1
fi

printf '%s\n' 'upstream patch workflow tests passed'
