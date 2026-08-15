#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Report references, in tracked files, to repository paths that are not there.

Two kinds are checked:

* **Markdown links.** `[text](some/path.md)` in any tracked `.md` file must
  resolve, relative to the file that carries it. Anchors, URLs and absolute
  paths are ignored.
* **Path citations.** A backticked or bare token in any tracked text file that
  looks like a repository path, meaning it names one of this repository's
  top-level directories and carries a slash, must resolve, either from the
  repository root or from the directory of the file that cites it. A trailing `:123` line
  number is stripped before the check.

Both are the same failure from a reader's point of view: a sentence pointing at
something that is not there. Neither depends on a build, so this is cheap enough
to run in the gate.

    python3 scripts/check_links.py
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The repository's own top-level directories. A token has to start with one of
# these to be treated as a citation at all, which is what keeps a Lua script
# name inside a test fixture from being mistaken for a file.
TOP = {
    "art", "cmake", "docs", "editor", "engine", "games", "platform",
    "schemas", "scripts", "spikes", "tests", "tools", ".github", ".devcontainer",
}

# Trees whose contents are generated, gitignored, or supplied by a contributor
# who may not have supplied them. A citation into one of these is describing a
# path that is correct when the thing exists.
OPTIONAL = (
    "art/provided", "editor/dist", "editor/node_modules", "editor/public/board",
    "editor/src/generated", "tools/placeholder_art/assets",
    "tools/placeholder_art/gallery", "platform/playstation/scratch/grandleon_",
)

# Files that quote paths belonging to something other than this repository.
SKIP_FILES = (
    "package-lock.json", "spikes/", "tests/fixtures/",
    "scripts/check_links.py",
)

# Names that appear in test data and worked examples rather than as citations:
# a script an authored project binds to, a game nobody has written. They look
# like paths because they are paths, inside somebody else's project.
ILLUSTRATIVE = {
    "scripts/rally.lua", "scripts/alternate.lua", "scripts/reinforce.js",
    "scripts/alternate.js", "games/tworivers/", "games/tworivers",
    "editor/build",
}

LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
BACKTICK = re.compile(r"`([^`\n]+)`")
BARE = re.compile(r"(?<![\w/.\-])((?:%s)/[\w./\-]+)" % "|".join(
    re.escape(t) for t in sorted(TOP)))

BINARY = (".png", ".jpg", ".gif", ".webp", ".woff", ".woff2", ".ico", ".z64",
          ".bin", ".exe", ".gz", ".zip", ".pdf", ".svg", ".gltf")


def tracked():
    out = subprocess.run(["git", "-C", ROOT, "ls-files"],
                         capture_output=True, text=True, check=True).stdout
    for path in out.splitlines():
        if path.lower().endswith(BINARY):
            continue
        if any(skip in path for skip in SKIP_FILES):
            continue
        yield path


def built(candidate):
    """Whether this path is one `.gitignore` covers.

    A reference to a virtualenv, a build directory or a generated artifact
    names something the setup makes, not something missing. It is absent from
    a fresh clone by design, so judging it by whether the file is on disk
    answers a question about this machine rather than about the repository.
    """
    return subprocess.run(
        ["git", "-C", ROOT, "check-ignore", "-q", candidate]
    ).returncode == 0


def exists(*candidates):
    for candidate in candidates:
        full = os.path.normpath(os.path.join(ROOT, candidate))
        if not full.startswith(ROOT):
            continue
        if os.path.exists(full) or built(candidate):
            return True
    return False


def is_citation(token):
    if "/" not in token or " " in token or "://" in token:
        return False
    if token.startswith(("-", "$", "<", "*", "/", "~", "@", ".")):
        return False
    if any(mark in token for mark in ("<", "*", "{", "...", "…")):
        return False
    if token in ILLUSTRATIVE:
        return False
    if token.split("/", 1)[0] not in TOP:
        return False
    return not any(token.startswith(tree) for tree in OPTIONAL)


def ancestors(directory):
    """The directory itself, each of its parents, and the repository root."""
    seen = [""]
    while directory:
        seen.append(directory)
        directory = os.path.dirname(directory)
    return seen


def main():
    problems = []
    for rel in tracked():
        try:
            text = open(os.path.join(ROOT, rel), encoding="utf-8").read()
        except (UnicodeDecodeError, OSError):
            continue
        here = os.path.dirname(rel)
        for number, line in enumerate(text.splitlines(), 1):
            if rel.endswith(".md"):
                for target in LINK.findall(line):
                    target = target.split()[0].split("#")[0]
                    if not target or "://" in target or target.startswith("/"):
                        continue
                    if not exists(os.path.join(here, target)):
                        problems.append(f"{rel}:{number}: link -> {target}")
            for token in BACKTICK.findall(line) + [m for m in BARE.findall(line)]:
                token = token.strip().rstrip(".,;:)\"'")
                token = re.sub(r":\d+(-\d+)?$", "", token)
                if not is_citation(token):
                    continue
                if not exists(*[os.path.join(base, token)
                                for base in ancestors(here)]):
                    problems.append(f"{rel}:{number}: path -> {token}")
    for problem in sorted(set(problems)):
        print(problem)
    print(f"\n{len(set(problems))} unresolved reference(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
