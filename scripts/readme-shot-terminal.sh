#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Captures the terminal client, for the README: real client, real package,
# scripted commands, its ANSI output rendered to a PNG by Chromium.
set -euo pipefail
repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_png="$1"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

# Built, not assumed. Both of these are read out of `build/`, which is a tree
# somebody may have left at any commit, and a stale client against a current
# package is the one combination that looks like a picture rather than like a
# mistake: the reader refuses bytes it was never taught, the session stops after
# the opening scene, and the result is a plausible screenshot of a client that
# cannot start the game.
cmake --build "${repository_root}/build" \
    --target grandleon_content_compile grandleon_play >/dev/null

"${repository_root}/build/grandleon_content_compile" \
    "${repository_root}/games/tarnholt/source/project.json" \
    "${work}/game.gpk" >/dev/null

# And the session has to have worked. A `|| true` here would mean a client that
# failed to open the game was photographed failing to open the game, silently,
# into the README.
printf 'units\nmove 3 1 3\nquit\n' | \
    "${repository_root}/build/grandleon_play" "${work}/game.gpk" \
    --campaign=tarnholt_line > "${work}/session.txt"

# The picture is of a board being played, so the board has to be in it. A
# session that ends at the opening scene is the failure above wearing a
# screenshot, and it is worth naming rather than shipping.
if ! grep -q "moved to" "${work}/session.txt"; then
    echo "the terminal session never moved anybody; refusing to photograph it" >&2
    exit 1
fi

node "${repository_root}/scripts/readme-shot-terminal.mjs" \
    "${work}/session.txt" "${out_png}"
