#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
patch_root=${GRANDLEON_UPSTREAM_PATCH_ROOT:-$script_dir}
engine_root=$patch_root/engines

usage() {
    cat >&2 <<'EOF'
usage:
  manage.sh verify [engine|all]
  manage.sh apply <engine> <clean-upstream-checkout>
  manage.sh check <engine> [clone-source]
  manage.sh export <engine> <upstream-topic-checkout> <empty-output-directory>
EOF
    exit 2
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

load_engine() {
    requested_engine=$1
    engine_dir=$engine_root/$requested_engine
    env_file=$engine_dir/upstream.env
    series_file=$engine_dir/series
    test -f "$env_file" || die "unknown upstream: $requested_engine"
    test -f "$series_file" || die "missing series file: $series_file"

    UPSTREAM_NAME=
    UPSTREAM_URL=
    UPSTREAM_REVISION=
    # This is a checked-in data file maintained with the patch series.
    # shellcheck disable=SC1090
    . "$env_file"

    test "$UPSTREAM_NAME" = "$requested_engine" ||
        die "$env_file: UPSTREAM_NAME must be $requested_engine"
    test -n "$UPSTREAM_URL" || die "$env_file: UPSTREAM_URL is empty"
    case "$UPSTREAM_REVISION" in
        *[!0-9a-f]*|'') die "$env_file: revision must be a full lowercase SHA-1" ;;
    esac
    test "${#UPSTREAM_REVISION}" -eq 40 ||
        die "$env_file: revision must contain 40 hexadecimal characters"
}

patch_names() {
    sed -e 's/[[:space:]]*#.*$//' -e '/^[[:space:]]*$/d' "$series_file"
}

verify_engine() {
    load_engine "$1"
    seen=' '
    count=0
    for patch_name in $(patch_names); do
        case "$patch_name" in
            *[!A-Za-z0-9._-]*|*/*|''|*.patch.patch)
                die "$series_file: unsafe patch filename: $patch_name"
                ;;
            *.patch) ;;
            *) die "$series_file: patch must end in .patch: $patch_name" ;;
        esac
        case "$seen" in
            *" $patch_name "*) die "$series_file: duplicate patch: $patch_name" ;;
        esac
        test -f "$engine_dir/$patch_name" ||
            die "$series_file: missing patch: $patch_name"
        first_line=$(sed -n '1p' "$engine_dir/$patch_name")
        case "$first_line" in
            'From '[0-9a-f][0-9a-f]*) ;;
            *) die "$engine_dir/$patch_name: not a git format-patch file" ;;
        esac
        seen="$seen$patch_name "
        count=$((count + 1))
    done
    printf 'verified %s at %s with %s patch(es)\n' \
        "$UPSTREAM_NAME" "$UPSTREAM_REVISION" "$count"
}

apply_engine() {
    engine=$1
    checkout=$2
    verify_engine "$engine" >/dev/null
    test -d "$checkout/.git" || die "not a Git checkout: $checkout"
    test -z "$(git -C "$checkout" status --porcelain)" ||
        die "upstream checkout must be clean: $checkout"
    actual_revision=$(git -C "$checkout" rev-parse HEAD)
    test "$actual_revision" = "$UPSTREAM_REVISION" ||
        die "$engine checkout is at $actual_revision, expected $UPSTREAM_REVISION"

    count=0
    for patch_name in $(patch_names); do
        if ! git -C "$checkout" am --3way --keep-cr "$engine_dir/$patch_name"; then
            git -C "$checkout" am --abort >/dev/null 2>&1 || true
            die "failed to apply $patch_name to $engine"
        fi
        count=$((count + 1))
    done
    result_revision=$(git -C "$checkout" rev-parse HEAD)
    printf 'applied %s patch(es) to %s; HEAD=%s\n' \
        "$count" "$engine" "$result_revision"
}

check_engine() {
    engine=$1
    source=${2:-}
    verify_engine "$engine" >/dev/null
    test -n "$source" || source=$UPSTREAM_URL
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/grandleon-upstream-check.XXXXXX")
    trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM
    git clone --quiet --no-checkout "$source" "$scratch/upstream"
    git -C "$scratch/upstream" checkout --quiet --detach "$UPSTREAM_REVISION"
    apply_engine "$engine" "$scratch/upstream"
    rm -rf -- "$scratch"
    trap - EXIT HUP INT TERM
}

export_engine() {
    engine=$1
    checkout=$2
    output=$3
    verify_engine "$engine" >/dev/null
    test -d "$checkout/.git" || die "not a Git checkout: $checkout"
    test -z "$(git -C "$checkout" status --porcelain)" ||
        die "upstream topic checkout must be clean: $checkout"
    git -C "$checkout" cat-file -e "$UPSTREAM_REVISION^{commit}" 2>/dev/null ||
        die "$checkout does not contain pinned revision $UPSTREAM_REVISION"
    git -C "$checkout" merge-base --is-ancestor "$UPSTREAM_REVISION" HEAD ||
        die "topic HEAD is not based on pinned revision $UPSTREAM_REVISION"
    topic_head=$(git -C "$checkout" rev-parse HEAD)
    test "$topic_head" != "$UPSTREAM_REVISION" || die "topic has no commits to export"

    mkdir -p "$output"
    test -z "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit)" ||
        die "output directory must be empty: $output"
    listing=$output/.format-patch-list
    git -C "$checkout" format-patch --no-stat --full-index \
        --output-directory "$output" "$UPSTREAM_REVISION..HEAD" >"$listing"
    : >"$output/series"
    count=0
    while IFS= read -r patch_path; do
        test -n "$patch_path" || continue
        basename -- "$patch_path" >>"$output/series"
        count=$((count + 1))
    done <"$listing"
    rm -f -- "$listing"
    test "$count" -gt 0 || die "git format-patch produced no patches"
    {
        printf 'UPSTREAM_NAME=%s\n' "$UPSTREAM_NAME"
        printf 'UPSTREAM_URL=%s\n' "$UPSTREAM_URL"
        printf 'UPSTREAM_BASE_REVISION=%s\n' "$UPSTREAM_REVISION"
        printf 'UPSTREAM_TOPIC_HEAD=%s\n' "$topic_head"
        printf 'PATCH_COUNT=%s\n' "$count"
    } >"$output/export-metadata.env"
    printf 'exported %s upstream-ready commit(s) for %s to %s\n' \
        "$count" "$engine" "$output"
}

command=${1:-}
case "$command" in
    verify)
        target=${2:-all}
        test "$#" -le 2 || usage
        if test "$target" = all; then
            found=0
            for candidate in "$engine_root"/*; do
                test -d "$candidate" || continue
                verify_engine "$(basename -- "$candidate")"
                found=1
            done
            test "$found" -eq 1 || die "no upstream definitions in $engine_root"
        else
            verify_engine "$target"
        fi
        ;;
    apply)
        test "$#" -eq 3 || usage
        apply_engine "$2" "$3"
        ;;
    check)
        test "$#" -ge 2 && test "$#" -le 3 || usage
        check_engine "$2" "${3:-}"
        ;;
    export)
        test "$#" -eq 4 || usage
        export_engine "$2" "$3" "$4"
        ;;
    *) usage ;;
esac
