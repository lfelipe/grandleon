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
  repository root, the directory of the file that cites it, or the Godot
  project resource root. A trailing `:123` line
  number is stripped before the check.

Both are the same failure from a reader's point of view: a sentence pointing at
something that is not there. Neither depends on a build, so this is cheap enough
to run in the gate.

    python3 scripts/check_links.py
"""
import argparse
from contextlib import redirect_stdout
import io
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The repository's own top-level directories. A token has to start with one of
# these to be treated as a citation at all, which is what keeps a Lua script
# name inside a test fixture from being mistaken for a file.
TOP = {
    "art", "cmake", "docs", "engine", "games", "platform",
    "schemas", "scripts", "spikes", "tests", "tools", ".github", ".devcontainer",
}

# Trees whose contents are generated, gitignored, or supplied by a contributor
# who may not have supplied them. A citation into one of these is describing a
# path that is correct when the thing exists.
OPTIONAL = (
    "art/provided", "tools/placeholder_art/assets",
    "tools/placeholder_art/gallery", "platform/playstation/scratch/grandleon_",
)

# Files that quote paths belonging to something other than this repository.
SKIP_FILES = (
    "package-lock.json", "spikes/", "tests/fixtures/",
    "scripts/check_links.py", "third_party/",
    # These files describe paths inside pinned upstream checkouts or paths
    # whose *absence* is the cutover contract being tested.
    "cutover-contract.tsv",
    "platform/nintendo64/pyrite/toolchain/Containerfile",
)

# Names that appear in test data and worked examples rather than as citations:
# a script an authored project binds to, a game nobody has written. They look
# like paths because they are paths, inside somebody else's project.
ILLUSTRATIVE = {
    "scripts/rally.lua", "scripts/alternate.lua", "scripts/reinforce.js",
    "scripts/alternate.js", "games/tworivers/", "games/tworivers",
    "platform/nintendo64/src/old_renderer.cpp",
}

LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
BACKTICK = re.compile(r"`([^`\n]+)`")
BARE = re.compile(r"(?<![\w/.$\-])((?:%s)/[\w./\-]+)" % "|".join(
    re.escape(t) for t in sorted(TOP)))

BINARY = (".png", ".jpg", ".gif", ".webp", ".woff", ".woff2", ".ico", ".z64",
          ".bin", ".exe", ".gz", ".zip", ".pdf", ".svg")


def tracked(root=ROOT):
    out = subprocess.run(["git", "-C", root, "ls-files"],
                         capture_output=True, text=True, check=True).stdout
    for path in out.splitlines():
        if path.lower().endswith(BINARY):
            continue
        if any(skip in path for skip in SKIP_FILES):
            continue
        # Patch bodies quote the vendor checkout's paths. Their sibling docs
        # and references pointing TO these patch files remain checked.
        if path.startswith("tools/upstream_patches/") and path.lower().endswith(".patch"):
            continue
        yield path


def built(candidate, root=ROOT):
    """Whether this path is one `.gitignore` covers.

    A reference to a virtualenv, a build directory or a generated artifact
    names something the setup makes, not something missing. It is absent from
    a fresh clone by design, so judging it by whether the file is on disk
    answers a question about this machine rather than about the repository.
    """
    return subprocess.run(
        ["git", "-C", root, "check-ignore", "-q", candidate]
    ).returncode == 0


def exists(*candidates, root=ROOT):
    for candidate in candidates:
        full = os.path.normpath(os.path.join(root, candidate))
        if os.path.commonpath((root, full)) != root:
            continue
        if os.path.exists(full) or built(candidate, root):
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


def scan(root=ROOT):
    problems = []
    for rel in tracked(root):
        try:
            text = open(os.path.join(root, rel), encoding="utf-8").read()
        except (UnicodeDecodeError, OSError):
            continue
        here = os.path.dirname(rel)
        for number, line in enumerate(text.splitlines(), 1):
            if rel.endswith(".md"):
                for target in LINK.findall(line):
                    target = target.split()[0].split("#")[0]
                    if not target or "://" in target or target.startswith("/"):
                        continue
                    if not exists(os.path.join(here, target), root=root):
                        problems.append(f"{rel}:{number}: link -> {target}")
            for token in BACKTICK.findall(line) + [m for m in BARE.findall(line)]:
                token = token.strip().rstrip(".,;:)\"'")
                token = re.sub(r":\d+(-\d+)?$", "", token)
                if not is_citation(token):
                    continue
                candidates = [os.path.join(base, token) for base in ancestors(here)]
                # Bare citations may name Godot resources from another
                # platform's exporter. Markdown links above stay file-relative.
                candidates.append(os.path.join("platform/godot/project", token))
                if not exists(*candidates, root=root):
                    problems.append(f"{rel}:{number}: path -> {token}")
    return sorted(set(problems))


def main(root=ROOT):
    problems = scan(root)
    for problem in problems:
        print(problem)
    print(f"\n{len(problems)} unresolved reference(s)")
    return 1 if problems else 0


def self_test():
    """Exercise real git tracking and both accepted and rejected path forms."""
    with tempfile.TemporaryDirectory(prefix="grandleon-doc-links-") as directory:
        root = Path(directory)
        subprocess.run(["git", "init", "-q", str(root)], check=True)

        def write(path, text):
            file = root / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(text, encoding="utf-8")
            subprocess.run(["git", "-C", str(root), "add", "--", path], check=True)

        write("scripts/run.sh", 'python3 "$scripts/verify-probe-disc.py"\n')
        write("platform/godot/project/art/combat_backdrops/manifest.json", "{}\n")
        write("platform/nintendo64/prepare.py", 'resource = "art/combat_backdrops/manifest.json"\n')
        patch = "tools/upstream_patches/engines/vendor/0001-demo.patch"
        write(patch, "+ tools/ninja.exe\n+ tests/ExternalProjection.c\n")
        write("docs/README.md", "[script](../scripts/run.sh)\n[patch](../" + patch + ")\n")
        write("docs/probe.md", "")
        write("tools/upstream_patches/README.md", "Vendor patches.\n")
        assert scan(str(root)) == [], "valid shell/resource/vendor references must pass"

        controls = (
            ("docs/probe.md", "[broken](missing.md)\n", "link -> missing.md"),
            ("docs/probe.md", '`art/combat_backdrops/missing.json`\n', "path -> art/combat_backdrops/missing.json"),
            ("docs/probe.md", "[broken relative resource](art/combat_backdrops/manifest.json)\n",
             "link -> art/combat_backdrops/manifest.json"),
            ("docs/probe.md", "[missing patch](../tools/upstream_patches/engines/vendor/missing.patch)\n",
             "link -> ../tools/upstream_patches/engines/vendor/missing.patch"),
            ("tools/upstream_patches/README.md", "[broken vendor documentation](missing.md)\n", "link -> missing.md"),
        )
        for path, text, expected in controls:
            original = (root / path).read_text(encoding="utf-8")
            write(path, text)
            problems = scan(str(root))
            assert any(expected in problem for problem in problems), (path, problems)
            with redirect_stdout(io.StringIO()):
                assert main(str(root)) == 1, "a broken reference must fail the command"
            write(path, original)
        assert scan(str(root)) == []
    print("Doc link checker self-test passed: valid references accepted; five negative controls rejected.")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="run isolated git-fixture regression checks")
    args = parser.parse_args()
    sys.exit(self_test() if args.self_test else main())
