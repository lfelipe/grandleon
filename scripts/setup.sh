#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Brings a checkout to the point where every gate in CODING.md can run.
#
#   scripts/setup.sh          prepare this checkout
#   scripts/setup.sh --check  report what is missing, change nothing
#
# Works the same in the primary checkout and in a `git worktree`. It is safe to
# re-run: every step reports what is already in place instead of redoing it.
#
# In a worktree the two expensive artefacts, the pinned Chromium and the
# placeholder-art virtual environment, are borrowed from the primary checkout
# rather than fetched again. That is the same reasoning scripts/local-ci.sh
# already applies: their versions are pinned by lockfiles, not by the checkout
# they happen to sit in, so one copy per machine is the honest number. Both
# paths are gitignored, and both are borrowed by symlink, so the borrowing is
# visible rather than magic.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
check_only=0
case "${1:-}" in
    "") ;;
    --check) check_only=1 ;;
    *)
        echo "usage: $(basename "$0") [--check]" >&2
        exit 2
        ;;
esac

# The primary checkout is the one holding the shared object store; in a
# worktree, --git-common-dir points back at it.
primary_checkout="${repository_root}"
if common_dir="$(git -C "${repository_root}" \
    rev-parse --path-format=absolute --git-common-dir 2>/dev/null)"; then
    primary_checkout="$(cd "$(dirname "${common_dir}")" && pwd)"
fi
is_worktree=0
[ "${repository_root}" = "${primary_checkout}" ] || is_worktree=1

missing=0
step() { printf '\n== %s\n' "$1"; }
note() { printf '   %s\n' "$1"; }
absent() {
    printf '   MISSING: %s\n' "$1"
    missing=$((missing + 1))
}

if [ "${is_worktree}" -eq 1 ]; then
    printf 'worktree %s\n' "${repository_root}"
    printf 'borrowing from %s\n' "${primary_checkout}"
else
    printf 'primary checkout %s\n' "${repository_root}"
fi

# --- Node dependencies -------------------------------------------------------
#
# Both trees are needed. tools/source_schema is the easy one to forget, because
# nothing in the editor references it and the ctest entry that needs it,
# grandleon.source_schema, lives in a different directory again.

node_dependencies() {
    local directory="${repository_root}/$1"
    local stamp="${directory}/node_modules/.grandleon-setup"
    local fingerprint
    fingerprint="$(sha256sum "${directory}/package-lock.json" | cut -d' ' -f1)"
    if [ -f "${stamp}" ] && [ "$(cat "${stamp}")" = "${fingerprint}" ]; then
        note "$1/node_modules matches package-lock.json"
        return
    fi
    if [ "${check_only}" -eq 1 ]; then
        # Distinguish the two reasons, because they send a reader to different
        # places. An absent tree is the failure this script exists to prevent;
        # a present tree with no stamp is just a checkout that predates the
        # stamp, and saying "missing" of a directory sitting right there costs
        # someone a hunt for a problem they do not have.
        if [ -d "${directory}/node_modules" ]; then
            absent "$1/node_modules is not verified against package-lock.json (npm ci)"
        else
            absent "$1/node_modules (npm ci)"
        fi
        return
    fi
    note "npm ci --prefix $1"
    npm ci --prefix "${directory}" >/dev/null
    printf '%s\n' "${fingerprint}" >"${stamp}"
}

step "Node dependencies"
node_dependencies tools/source_schema
node_dependencies editor

# --- Chromium ----------------------------------------------------------------

browsers="${repository_root}/.playwright-browsers"
donor_browsers="${primary_checkout}/.playwright-browsers"

chromium_installed() {
    local candidate
    for candidate in "$1"/chromium-*/INSTALLATION_COMPLETE; do
        [ -e "${candidate}" ] && return 0
    done
    return 1
}

step "Chromium for the browser suite"
if chromium_installed "${browsers}"; then
    note ".playwright-browsers already holds an installed Chromium"
elif [ "${is_worktree}" -eq 1 ] && chromium_installed "${donor_browsers}"; then
    if [ "${check_only}" -eq 1 ]; then
        absent ".playwright-browsers (borrow from the primary checkout)"
    else
        # A directory tree left behind without the binary is the usual state of
        # a fresh worktree, and it is what makes the browser suite fail in a way
        # that looks like a Playwright bug. Discarding it is safe: the branch
        # above already established that it holds no installed Chromium, and
        # whatever else it holds is a download cache the donor also has.
        rm -rf "${browsers}"
        note "borrowing .playwright-browsers from the primary checkout"
        ln -s "${donor_browsers}" "${browsers}"
    fi
elif [ "${check_only}" -eq 1 ]; then
    absent ".playwright-browsers (playwright install chromium)"
else
    note "installing Chromium into .playwright-browsers (about 650 MB)"
    PLAYWRIGHT_BROWSERS_PATH="${browsers}" \
        npx --prefix "${repository_root}/editor" playwright install chromium
fi

# --- Placeholder-art virtual environment -------------------------------------

venv="${repository_root}/tools/placeholder_art/.venv"
donor_venv="${primary_checkout}/tools/placeholder_art/.venv"

venv_usable() {
    [ -x "$1/bin/python" ] && "$1/bin/python" -c 'import PIL' >/dev/null 2>&1
}

step "Placeholder-art virtual environment"
if venv_usable "${venv}"; then
    note "tools/placeholder_art/.venv can import Pillow"
elif [ "${is_worktree}" -eq 1 ] && venv_usable "${donor_venv}"; then
    if [ "${check_only}" -eq 1 ]; then
        absent "tools/placeholder_art/.venv (borrow from the primary checkout)"
    else
        rm -rf "${venv}"
        note "borrowing tools/placeholder_art/.venv from the primary checkout"
        ln -s "${donor_venv}" "${venv}"
    fi
elif [ "${check_only}" -eq 1 ]; then
    absent "tools/placeholder_art/.venv (python3 -m venv)"
else
    note "creating tools/placeholder_art/.venv"
    rm -rf "${venv}"
    python3 -m venv "${venv}"
    "${venv}/bin/pip" install --quiet \
        --requirement "${repository_root}/tools/placeholder_art/requirements.txt"
fi

# ccache, if the machine has one. Not installed here and not required: the
# CMake configure finds it or it does not, and a machine without it builds
# exactly as it always did. Reported because the gate compiles every
# translation unit twice, under two compilers, in a clone made for that run,
# and this is the difference between doing that and looking it up.
step "ccache"
if command -v ccache > /dev/null 2>&1; then
    note "$(ccache --version | head -n 1), used automatically by the configure"
else
    note "not installed; builds are unaffected, only slower on a second pass"
fi

# --- What this deliberately does not do --------------------------------------
#
# The container toolchains are not prepared here: Emscripten for WebAssembly,
# libdragon for the Nintendo 64, and the PlayStation's own.
# They are pulled or built by their own CMake targets on first use, they are
# pinned by digest, and they cost minutes rather than seconds. Preparing them
# eagerly would make this script slow for the majority of changes that never
# touch a console. See CODING.md, "Reproducible toolchains".

if [ "${check_only}" -eq 1 ]; then
    if [ "${missing}" -eq 0 ]; then
        printf '\nReady to gate.\n'
        exit 0
    fi
    printf '\n%d item(s) missing; run scripts/setup.sh\n' "${missing}"
    exit 1
fi

printf '\nReady to gate. The whole gate, on a port nobody else is using:\n'
printf '  scripts/local-ci.sh --preview-port <port>\n'
