#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Decides whether a console harness log says what it had to say.
#
#   scripts/assert-harness-verdict.sh --log FILE --require PATTERN \
#       [--min-checks N] [--what NAME]
#   scripts/assert-harness-verdict.sh --self-test
#
# Every console here judges a run by what its log says. A word is not enough:
# `RESULT PASS 0/0` contains the word, and it is exactly what a ROM whose
# assertion table was emptied prints. Whether a ROM asserted anything at all is
# a question only the count can answer. This is the one place that decision is
# written, so the consoles cannot come to mean different things by it, and so it
# can be shown to fail.
#
# What it requires, in order:
#
#   1. the log exists and is readable. A harness that never started leaves no
#      log, and reading a missing file as "no match" reports the wrong cause;
#   2. some line matches PATTERN. Give an anchored pattern: `RESULT PASS`
#      unanchored is also satisfied by a diagnostic line that mentions it,
#      which the self-test below demonstrates;
#   3. with --min-checks N, that line carries a `PASS a/b` count, that a and b
#      are equal, and that b is at least N.
#
# **A floor, never an equality.** The count moves between runs of one binary:
# the Nintendo 64 autopilot reports 88 or 89 from one unchanged ROM, because
# how far a timed run gets decides how many checkpoints it reaches. An equality
# would make an honest run red, and an assertion that goes red on honest runs is
# an assertion somebody deletes. A floor tolerates the movement and still makes
# `0/0`, a gutted table and a half-finished run impossible.
#
# **Adopting it costs one line.** A caller that today does
#
#     grep -q "^HARNESS RESULT PASS" "${log}" || fail
#
# does instead
#
#     scripts/assert-harness-verdict.sh --log "${log}" \
#         --require "^HARNESS RESULT PASS " --min-checks 10 --what "${name}"
#
# and gets the count guard, the missing-log case and the named cause with it.
# The pattern is a POSIX basic regular expression, as `grep` takes it.
#
# `--self-test` is the negative control, and it is meant to be run at the top of
# a console's check rather than by hand: it feeds this script a battery of logs
# that must be refused (absent, silent, `0/0`, partial, under the floor,
# countless, and a diagnostic echo of the verdict) and fails if any of them is
# accepted. It costs no emulator and about a hundredth of a second, so a console
# can afford to prove its predicate still works on every run.

set -euo pipefail

log=""
require=""
min_checks=0
what=""
self_test=0

usage() {
    echo "usage: $(basename "$0") --log FILE --require PATTERN" >&2
    echo "                       [--min-checks N] [--what NAME]" >&2
    echo "       $(basename "$0") --self-test" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --log) [ "$#" -ge 2 ] || usage; log="$2"; shift 2 ;;
        --require) [ "$#" -ge 2 ] || usage; require="$2"; shift 2 ;;
        --min-checks) [ "$#" -ge 2 ] || usage; min_checks="$2"; shift 2 ;;
        --what) [ "$#" -ge 2 ] || usage; what="$2"; shift 2 ;;
        --self-test) self_test=1; shift ;;
        *) usage ;;
    esac
done

if [ "${self_test}" -eq 1 ]; then
    [ -z "${log}" ] && [ -z "${require}" ] || usage

    scratch="$(mktemp -d)"
    trap 'rm -rf "${scratch}"' EXIT
    self="${BASH_SOURCE[0]}"
    refusals=0
    failures=0

    # Refused, and the reason it must be refused. Each of these is a way a
    # green run could have measured nothing.
    refuse() {
        local name="$1"
        shift
        if "${self}" "$@" >/dev/null 2>&1; then
            echo "error: the verdict check accepted ${name}." >&2
            failures=$((failures + 1))
        else
            refusals=$((refusals + 1))
        fi
    }
    accept() {
        local name="$1"
        shift
        if ! "${self}" "$@" >/dev/null 2>&1; then
            echo "error: the verdict check refused ${name}." >&2
            failures=$((failures + 1))
        fi
    }

    printf 'RESULT PASS 39/39\n' > "${scratch}/good.log"
    printf 'RESULT PASS 41/41\n' > "${scratch}/more.log"
    printf 'RESULT PASS 0/0\n' > "${scratch}/empty.log"
    printf 'RESULT PASS 20/20\n' > "${scratch}/short.log"
    printf 'RESULT PASS 38/39\n' > "${scratch}/partial.log"
    printf 'RESULT PASS\n' > "${scratch}/countless.log"
    printf 'info: booting\ninfo: nothing was checked\n' > "${scratch}/silent.log"
    printf 'info: this run wants RESULT PASS 39/39\n' > "${scratch}/echo.log"

    refuse "a log that does not exist" \
        --log "${scratch}/absent.log" --require "^RESULT PASS " --min-checks 39
    refuse "a log with no verdict line" \
        --log "${scratch}/silent.log" --require "^RESULT PASS " --min-checks 39
    refuse "an emptied assertion table (0/0)" \
        --log "${scratch}/empty.log" --require "^RESULT PASS " --min-checks 39
    refuse "a table gutted down to 20 assertions" \
        --log "${scratch}/short.log" --require "^RESULT PASS " --min-checks 39
    refuse "a run that passed 38 of 39" \
        --log "${scratch}/partial.log" --require "^RESULT PASS " --min-checks 39
    refuse "a verdict with no count at all" \
        --log "${scratch}/countless.log" --require "^RESULT PASS " --min-checks 39
    # The anchoring control. An unanchored `RESULT PASS` is satisfied by a
    # diagnostic that merely names the verdict, which is why every caller here
    # passes `^`.
    refuse "a diagnostic echo of the verdict" \
        --log "${scratch}/echo.log" --require "^RESULT PASS " --min-checks 39

    accept "the run the floor was derived from" \
        --log "${scratch}/good.log" --require "^RESULT PASS " --min-checks 39
    accept "a run that grew an assertion" \
        --log "${scratch}/more.log" --require "^RESULT PASS " --min-checks 39

    if [ "${failures}" -ne 0 ]; then
        echo "error: the verdict check accepts logs that measure nothing." >&2
        exit 1
    fi
    echo "Negative control: the verdict check refuses ${refusals} of ${refusals} logs that measure nothing."
    exit 0
fi

[ -n "${log}" ] && [ -n "${require}" ] || usage
case "${min_checks}" in
    ''|*[!0-9]*) echo "--min-checks must be a number" >&2; exit 2 ;;
esac

subject="the harness"
[ -n "${what}" ] && subject="${what}"

if [ ! -r "${log}" ]; then
    echo "error: ${subject} left no readable log at ${log}; it did not run." >&2
    exit 1
fi

verdict="$(grep -m1 -o "${require}.*" "${log}" || true)"
if [ -z "${verdict}" ]; then
    echo "error: ${subject} never said '${require}'." >&2
    echo "Log: ${log}" >&2
    exit 1
fi

if [ "${min_checks}" -gt 0 ]; then
    counts="$(
        printf '%s\n' "${verdict}" \
            | sed -n 's|.*PASS \([0-9][0-9]*\)/\([0-9][0-9]*\).*|\1 \2|p'
    )"
    if [ -z "${counts}" ]; then
        echo "error: ${subject} said '${verdict}' and named no assertion count." >&2
        echo "This check requires at least ${min_checks}. Log: ${log}" >&2
        exit 1
    fi
    passed="${counts% *}"
    total="${counts#* }"
    if [ "${passed}" -ne "${total}" ]; then
        echo "error: ${subject} passed ${passed} of ${total} assertions." >&2
        echo "Log: ${log}" >&2
        exit 1
    fi
    if [ "${total}" -lt "${min_checks}" ]; then
        echo "error: ${subject} made ${total} assertions and this check requires at least ${min_checks}." >&2
        echo "Either the assertion table shrank or the run stopped early." >&2
        echo "Log: ${log}" >&2
        exit 1
    fi
fi

printf '%s\n' "${verdict}"
