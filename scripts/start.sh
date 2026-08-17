#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# The way in. Clone the repository, run this, be editing and able to build a ROM.
#
#   scripts/start.sh            the editor and the ROM build service
#   scripts/start.sh --build    rebuild the image first
#
# Docker and its Compose plugin are the only things that have to be installed;
# `docs/SETUP.md` says how. Everything else — Node, the editor's dependencies,
# the console toolchain — is fetched into containers on the way.
#
# This is a wrapper around `compose.yaml` and not a second definition of
# anything. What it adds is three numbers Compose cannot work out for itself,
# and the reason it exists rather than a committed `.env`: all three are facts
# about *this machine*, so a file holding them would be wrong for everybody
# else's, and asking somebody to write one first would make the way in two
# commands rather than one.
#
#   the invoking user       the containers write into the checkout, and it
#                           belongs to whoever cloned it. Without this the
#                           image's own user, uid 1001, meets EACCES on the
#                           first write.
#   the docker group        the ROM service starts the toolchain container as a
#                           sibling through the mounted socket, and the socket
#                           is owned by a group whose id differs per machine —
#                           137 on one, 999 on another.
#
# `compose.yaml` says what the containers are and why the checkout is mounted at
# its own path. Read that one for the design; this one only fills in the blanks.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repository_root}"

# Refused by name rather than by whatever Compose says when it is not there.
if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker is not on PATH." >&2
    echo "Install Docker, then run this again. docs/SETUP.md has the steps." >&2
    exit 1
fi
if ! docker compose version >/dev/null 2>&1; then
    echo "error: the Docker Compose plugin is not installed." >&2
    echo "On Ubuntu:  sudo apt-get install docker-compose-v2" >&2
    echo "Elsewhere:  https://docs.docker.com/compose/install/" >&2
    exit 1
fi

socket=/var/run/docker.sock
if [ ! -S "${socket}" ]; then
    echo "error: ${socket} is not there, so no build could be started." >&2
    echo "Is the Docker daemon running?" >&2
    exit 1
fi

# `${PWD}` is what `compose.yaml` mounts the checkout as, on both sides, so it
# has to be the checkout rather than wherever this was typed from.
export PWD="${repository_root}"
export GRANDLEON_UID="$(id -u)"
export GRANDLEON_GID="$(id -g)"
export GRANDLEON_DOCKER_GID="$(stat -c '%g' "${socket}")"

# Every name a second machine might reach this by, because both servers refuse
# a Host they were not told about — the editor with a 403 that reads, from a
# browser, as an editor that never appeared.
#
# All of them, not the "canonical" one. `hostname -f` answers `cruncher` on a
# machine whose own resolver also answers to `cruncher.lan`, and it is the
# second that a peer on the network types. `hostname -A` is the list the
# resolver actually holds; the short name and the mDNS spelling are added
# because they are the two this repository already assumed and neither is
# guaranteed to be in it. Both consumers take a comma-separated list.
if [ -z "${GRANDLEON_HOST:-}" ]; then
    short="$(hostname)"
    GRANDLEON_HOST="$(
        {
            printf '%s\n' "${short}" "${short}.local"
            hostname -f 2>/dev/null || true
            hostname -A 2>/dev/null | tr ' ' '\n' || true
        } | sed '/^$/d' | sort -u | paste -sd,
    )"
fi
export GRANDLEON_HOST

echo "Bringing up the editor and the ROM build service."
echo "  checkout   ${repository_root}"
echo "  as         ${GRANDLEON_UID}:${GRANDLEON_GID}, docker group ${GRANDLEON_DOCKER_GID}"
echo "  editor     http://localhost:${GRANDLEON_EDITOR_PORT:-5173}"
echo "  also as    ${GRANDLEON_HOST}"
echo

build=()
if [ "${1:-}" = "--build" ]; then
    build=(--build)
    shift
fi

exec docker compose up "${build[@]}" "$@"
