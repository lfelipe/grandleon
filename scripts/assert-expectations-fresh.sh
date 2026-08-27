#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Decides whether a host-derived expectation file still answers for the sources
# it was derived from.
#
#   scripts/assert-expectations-fresh.sh --expectation FILE \
#       --regenerate TARGET [--project FILE]
#   scripts/assert-expectations-fresh.sh --self-test
#
# The console checks under `platform/playstation/scripts/` compare a run against
# a file derived on the host *before* the run, which is what stops a check being
# adjusted until it passes. That derivation is only worth anything while the file
# is younger than the code that produces it. When it is not, the check compares
# this build against last week's answer and prints the difference as a transcript
# mismatch: dozens of checkpoint lines, console beside host, in the exact shape a
# real regression takes. It has cost an afternoon, on 2026-08-16, to a file dated
# before a commit that legitimately moved it.
#
# The worse half is quieter. A stale run that happens to still match reports a
# green check that proved nothing.
#
# **It refuses; it does not regenerate.** The friendlier choice was available —
# derive the file and carry on — and it is the wrong one here for two reasons.
# A check that quietly fixes its own inputs is a check that can hide a change,
# and these scripts are otherwise careful never to build the host tools they
# read. Refusing by name is also what this repository does everywhere else. The
# cost of the choice is one command, printed in the refusal.
#
# **The comparison is against sources, not against the tool binary.** Comparing
# the expectation to `grandleon_playstation_expect` would repeat what CMake
# already does, and would miss the case that bites: a tool nobody rebuilt is
# itself older than the client it compiles, so an expectation younger than that
# binary can still be older than the truth. Sweeping the sources collapses both
# questions into one, needs no build directory to be found, and is caught by a
# `git pull` as much as by an edit, because a checkout stamps what it changed.
#
# CMake's own path is narrower than this one, and the two disagree in a way
# worth knowing. It declares each expectation as a custom command depending on
# the tool, and the tool on its sources, so an edit that genuinely feeds a
# transcript rebuilds it. But this sweep looks at whole source roots rather than
# at translation units, so an edit that *cannot* feed a given transcript still
# marks it stale — and for exactly those, `--target ..._expectations` finds its
# output newer than everything it declares and does nothing. The refusal below
# therefore says to remove the file first; see `rederive_it`. The scripts are
# the unguarded path, and they are the ergonomic one: their headers document
# running them directly, because the CMake target rebuilds the whole container
# image chain and the script does not.
#
# The sweep is deliberately a little wide — it is every source root the two
# derivation tools compile or link, not the exact translation units — and it is
# narrowed to C and C++ files plus the named project. A README inside one of
# those roots does not move a transcript, and a refusal it caused would teach
# people to distrust this. A refusal that is wide by one file costs the two
# commands it prints, which is the price of never being wrong the other way.
#
# It reads timestamps, so it is wide in one more way worth knowing before it
# surprises somebody: a checkout or a rebase that rewrites a file with the bytes
# it already had still stamps it, and this will refuse over a change that is not
# one. That is the tolerable direction. Being wrong the other way is what this
# exists to stop, and it is wrong the other way silently.
#
# `--self-test` is the negative control, and it is meant to be run in the check
# rather than by hand. A guard nobody has seen refuse is a guard nobody should
# trust; `assert-harness-verdict.sh` beside this carries its own for the same
# reason. It builds a scratch tree, ages a file against it both ways, and fails
# if the freshness predicate accepts what it must refuse. It costs no emulator
# and about a hundredth of a second.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Everything the two derivation tools under `platform/playstation/tools/`
# compile or link, and so everything that can legitimately move what they write.
# `platform/client` is the shared client the console runs, including the pad
# scripts under `autopilot/`; `tools/game_content` is the compiler that turns the
# project into the package they read; the rest is the engine those queries go to.
#
# A root that stops existing would silently narrow this sweep into a guard that
# passes everything, so their existence is asserted below rather than assumed.
expectation_source_roots() {
    cat <<'ROOTS'
engine
platform/client
platform/playstation/tools
platform/sheet
platform/storage
platform/view
tools/game_content
ROOTS
}

expectation=""
regenerate=""
project=""
self_test=0
declare -a source_roots=()

usage() {
    echo "usage: $(basename "$0") --expectation FILE --regenerate TARGET" >&2
    echo "                       [--project FILE]" >&2
    echo "       $(basename "$0") --self-test" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --expectation) [ "$#" -ge 2 ] || usage; expectation="$2"; shift 2 ;;
        --regenerate) [ "$#" -ge 2 ] || usage; regenerate="$2"; shift 2 ;;
        --project) [ "$#" -ge 2 ] || usage; project="$2"; shift 2 ;;
        # Only the self-test passes this. Callers in the repository take the
        # roots above, so the set they are checked against lives in one place.
        --source) [ "$#" -ge 2 ] || usage; source_roots+=("$2"); shift 2 ;;
        --self-test) self_test=1; shift ;;
        *) usage ;;
    esac
done

# The newest source under the given roots that is younger than the expectation,
# or nothing. `-quit` stops at the first hit, so the ordinary passing case walks
# only as far as the first file it looks at.
first_newer_source() {
    local target="$1"
    shift
    find "$@" \
        -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) \
        -newer "${target}" \
        -print -quit 2>/dev/null
}

if [ "${self_test}" -eq 1 ]; then
    [ -z "${expectation}" ] && [ -z "${regenerate}" ] || usage

    scratch="$(mktemp -d)"
    trap 'rm -rf "${scratch}"' EXIT
    self="${BASH_SOURCE[0]}"
    refusals=0
    failures=0

    refuse() {
        local name="$1"
        shift
        if "${self}" "$@" >/dev/null 2>&1; then
            echo "error: the freshness check accepted ${name}." >&2
            failures=$((failures + 1))
        else
            refusals=$((refusals + 1))
        fi
    }
    accept() {
        local name="$1"
        shift
        if ! "${self}" "$@" >/dev/null 2>&1; then
            echo "error: the freshness check refused ${name}." >&2
            failures=$((failures + 1))
        fi
    }
    # Refuses, *and* says the thing that puts it right. The two refusals need
    # different instructions -- an absent file is one the regenerate target
    # always writes, a stale one is one it can believe current and skip -- and
    # printing the absent one's advice for a stale file is a loop with no exit.
    # Asserted rather than described, because wording is what rots quietest.
    refuse_saying() {
        local name="$1"
        local phrase="$2"
        shift 2
        local said
        if said="$("${self}" "$@" 2>&1)"; then
            echo "error: the freshness check accepted ${name}." >&2
            failures=$((failures + 1))
            return
        fi
        refusals=$((refusals + 1))
        case "${said}" in
            *"${phrase}"*) ;;
            *)
                echo "error: refusing ${name} never said '${phrase}'." >&2
                failures=$((failures + 1))
                ;;
        esac
    }

    mkdir -p "${scratch}/src" "${scratch}/other"
    printf 'int main(void) { return 0; }\n' > "${scratch}/src/client.cpp"
    printf 'a README, which moves no transcript\n' > "${scratch}/src/README.md"
    printf '{}\n' > "${scratch}/project.json"

    # The whole tree is older than this, so it is the file a passing run has.
    touch -d '2020-06-01 00:00:00' \
        "${scratch}/src/client.cpp" "${scratch}/src/README.md" \
        "${scratch}/project.json"
    touch -d '2020-06-02 00:00:00' "${scratch}/fresh.txt"
    # Derived before the source it is derived from moved: the afternoon this
    # exists for.
    touch -d '2020-05-31 00:00:00' "${scratch}/stale.txt"

    refuse "an expectation that does not exist" \
        --expectation "${scratch}/absent.txt" --regenerate t \
        --source "${scratch}/src"
    refuse "an expectation older than a source it is derived from" \
        --expectation "${scratch}/stale.txt" --regenerate t \
        --source "${scratch}/src"
    # And each is told how to fix the one it has. A file that is merely absent
    # is derived; a file that is stale is removed first, because the sweep here
    # is wider than the dependencies CMake tracks and the target will otherwise
    # look at an output it has no reason to rewrite and report success.
    refuse_saying "a stale expectation" "rm ${scratch}/stale.txt" \
        --expectation "${scratch}/stale.txt" --regenerate t \
        --source "${scratch}/src"
    refuse_saying "an absent expectation" "Derive it first" \
        --expectation "${scratch}/absent.txt" --regenerate t \
        --source "${scratch}/src"
    refuse "an expectation older than the project it compiled" \
        --expectation "${scratch}/stale.txt" --regenerate t \
        --source "${scratch}/other" --project "${scratch}/project.json"
    # A guard pointed at nothing walks no files and finds nothing newer, which
    # is indistinguishable from up to date. That is how this fails open, so it
    # is the case most worth holding.
    refuse "a sweep over a source root that does not exist" \
        --expectation "${scratch}/fresh.txt" --regenerate t \
        --source "${scratch}/no-such-root"
    refuse "an expectation older than a source root it names" \
        --expectation "${scratch}/stale.txt" --regenerate t \
        --source "${scratch}/src" --source "${scratch}/other"
    refuse "a project path that names nothing" \
        --expectation "${scratch}/fresh.txt" --regenerate t \
        --source "${scratch}/src" --project "${scratch}/no-such-project.json"

    # The default list is what every caller in this repository sweeps, and it is
    # the one thing the cases above cannot reach, because they all replace it.
    # An empty list would hand `find` no path and make it walk the working
    # directory; a root that has been renamed would narrow the sweep silently.
    # Both are the fail-open direction, so both are counted rather than trusted.
    default_roots=0
    while IFS= read -r root; do
        default_roots=$((default_roots + 1))
        if [ ! -d "${repository_root}/${root}" ]; then
            echo "error: the default source root ${root} is not a directory." >&2
            failures=$((failures + 1))
        fi
    done < <(expectation_source_roots)
    if [ "${default_roots}" -eq 0 ]; then
        echo "error: the default source root list is empty." >&2
        failures=$((failures + 1))
    fi

    accept "an expectation newer than every source" \
        --expectation "${scratch}/fresh.txt" --regenerate t \
        --source "${scratch}/src" --project "${scratch}/project.json"
    # The narrowing is asserted rather than described: an expectation older than
    # a README beside the sources is still fresh, because prose does not move a
    # transcript. This is the one thing the sweep deliberately does not see.
    touch -d '2020-06-03 00:00:00' "${scratch}/src/README.md"
    accept "an expectation older only than a README" \
        --expectation "${scratch}/fresh.txt" --regenerate t \
        --source "${scratch}/src"

    if [ "${failures}" -ne 0 ]; then
        echo "error: the freshness check accepts expectations that answer for nothing." >&2
        exit 1
    fi
    echo "Negative control: the freshness check refuses ${refusals} of ${refusals} expectations that answer for nothing."
    exit 0
fi

[ -n "${expectation}" ] && [ -n "${regenerate}" ] || usage

# How to put it right, in the wording the missing-file check already uses.
derive_it() {
    echo "Derive it first:" >&2
    echo "    cmake --build build --target ${regenerate}" >&2
}

# The same, for a file that is stale rather than absent -- which is a different
# instruction, and printing the other one is a loop with no way out of it.
#
# The sweep above is deliberately wider than any one expectation's own
# dependencies: it looks at whole source roots, so editing a file that could not
# possibly move a given transcript still marks it stale. That is the
# conservative direction and it is the right one. But CMake's dependencies are
# the narrow, accurate ones, so for exactly those edits the regenerate target
# looks at its output, finds it newer than the tool and the project it declares,
# and does nothing at all. The guard then refuses again on a file nothing moved.
#
# Observed on 2026-08-27: an edit to `platform/client/autopilot/campaign_pad.h`
# -- which the turn run does not even read -- left `fordlight_autopilot.txt`
# refused, and `--target grandleon_playstation_turn_expectations` reported
# "Built target" without writing anything. Removing the file first is what
# breaks the tie, because an output that is absent is one CMake always rebuilds.
rederive_it() {
    echo "Remove it and derive it again:" >&2
    echo "    rm ${expectation}" >&2
    echo "    cmake --build build --target ${regenerate}" >&2
    echo "The removal is not ceremony. This sweep is wider than the" >&2
    echo "dependencies CMake tracks, so the target can believe an output it" >&2
    echo "will not rewrite is current, and building it again changes nothing." >&2
}

if [ "${#source_roots[@]}" -eq 0 ]; then
    while IFS= read -r root; do
        source_roots+=("${repository_root}/${root}")
    done < <(expectation_source_roots)
fi

# A root that has moved or been renamed makes the sweep below find nothing and
# report freshness, which is the one way this guard fails in the direction that
# costs an afternoon. It is checked rather than assumed.
for root in "${source_roots[@]}"; do
    if [ ! -d "${root}" ]; then
        echo "error: ${root} is not a directory, so nothing was compared." >&2
        echo "A source root in $(basename "$0") has moved; fix the list there." >&2
        exit 1
    fi
done

# And the same for the project, which `-nt` would answer 'not newer' about if it
# did not exist — a missing project would quietly drop out of the comparison.
if [ -n "${project}" ] && [ ! -f "${project}" ]; then
    echo "error: ${project} does not exist, so it was not compared." >&2
    echo "The caller named the wrong project; fix it there." >&2
    exit 1
fi

if [ ! -f "${expectation}" ]; then
    echo "error: ${expectation} does not exist." >&2
    derive_it
    exit 1
fi

newer="$(first_newer_source "${expectation}" "${source_roots[@]}" || true)"
if [ -z "${newer}" ] && [ -n "${project}" ] && \
   [ "${project}" -nt "${expectation}" ]; then
    newer="${project}"
fi

if [ -n "${newer}" ]; then
    echo "error: ${expectation} is older than a source it is derived from." >&2
    echo "  derived:  $(date -r "${expectation}" '+%Y-%m-%d %H:%M:%S')" >&2
    echo "  moved at: $(date -r "${newer}" '+%Y-%m-%d %H:%M:%S')" \
        "${newer#"${repository_root}/"}" >&2
    echo "It answers for a build that is no longer here, so a mismatch against" >&2
    echo "it would say nothing about this one." >&2
    rederive_it
    exit 1
fi
