#!/bin/sh
set -eu

usage() {
    printf '%s\n' \
        'usage: cutover-audit.sh <contract.tsv> <source-root> <built-artifact>' >&2
    exit 2
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

validate_relative_path() {
    candidate=$1
    case "$candidate" in
        ''|/*|..|../*|*/../*|*/..)
            die "$contract: path must stay inside the source root: $candidate"
            ;;
    esac
}

test "$#" -eq 3 || usage
contract=$1
source_root=$2
artifact=$3
test -f "$contract" || die "missing cutover contract: $contract"
test -d "$source_root" || die "missing source root: $source_root"
test -f "$artifact" || die "missing built artifact: $artifact"
source_root=$(CDPATH= cd -- "$source_root" && pwd)

engine=
required_count=0
forbidden_count=0
identity_count=0
tab=$(printf '\t')

while IFS="$tab" read -r rule value detail || test -n "${rule:-}${value:-}${detail:-}"; do
    case "${rule:-}" in
        ''|'#'*) continue ;;
        engine)
            test -n "${value:-}" || die "$contract: empty engine name"
            engine=$value
            ;;
        required-path)
            test -n "${value:-}" || die "$contract: empty required path"
            validate_relative_path "$value"
            test -e "$source_root/$value" ||
                die "$engine cutover is missing required path: $value"
            required_count=$((required_count + 1))
            ;;
        forbidden-path)
            test -n "${value:-}" || die "$contract: empty forbidden path"
            validate_relative_path "$value"
            test ! -e "$source_root/$value" ||
                die "$engine cutover still contains legacy path: $value"
            forbidden_count=$((forbidden_count + 1))
            ;;
        forbidden-text)
            test -n "${value:-}" && test -n "${detail:-}" ||
                die "$contract: forbidden-text needs a scope and regex"
            validate_relative_path "$value"
            test -e "$source_root/$value" ||
                die "$contract: forbidden-text scope does not exist: $value"
            if rg -n --hidden -g '!.git' -- "$detail" "$source_root/$value" >/dev/null; then
                die "$engine cutover still references legacy renderer in $value: $detail"
            fi
            forbidden_count=$((forbidden_count + 1))
            ;;
        artifact-text)
            test -n "${value:-}" || die "$contract: empty artifact identity"
            LC_ALL=C grep -aF -- "$value" "$artifact" >/dev/null ||
                die "$engine artifact does not identify its renderer: $value"
            identity_count=$((identity_count + 1))
            ;;
        *) die "$contract: unknown rule: $rule" ;;
    esac
done <"$contract"

test -n "$engine" || die "$contract: missing engine rule"
test "$required_count" -gt 0 || die "$contract: no required-path rules"
test "$forbidden_count" -gt 0 || die "$contract: no legacy source gates"
test "$identity_count" -gt 0 || die "$contract: no artifact-text identity gate"
printf 'verified fail-closed %s cutover in %s and %s\n' \
    "$engine" "$source_root" "$artifact"
