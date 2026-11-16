#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the runner's exit policy, without starting or changing a game."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
MOCK = '''#!/usr/bin/env python3
import os, pathlib, sys
args = sys.argv[1:]
if args[0] == "image":
    sys.exit(0)
if args[-1] == "/opt/ares/ARES_COMMIT":
    print(os.environ["GRANDLEON_ARES_COMMIT"])
    sys.exit(0)
out = pathlib.Path(os.environ["GRANDLEON_N64_BUILD_DIR"]) / "ares"
(out / "case.log").write_text("CHECKPOINT READY" + chr(10))
marker = os.environ.get("MOCK_INPUT_STATUS", "missing")
if marker != "missing":
    (out / "input-sequence.status").write_text(marker + chr(10))
sys.exit(int(os.environ["MOCK_EXIT_STATUS"]))
'''


class RunnerExitPolicy(unittest.TestCase):
    def run_case(self, status, *, allow=True, inputs=False, marker="missing", stale=False):
        with tempfile.TemporaryDirectory(prefix="grandleon-ares-exit-") as tmp:
            root = Path(tmp)
            mock = root / "docker"
            mock.write_text(MOCK)
            mock.chmod(0o755)
            rom = root / "test.z64"
            rom.write_bytes(b"test")
            if stale:
                (root / "ares").mkdir()
                (root / "ares/input-sequence.status").write_text("0\n")
            env = dict(os.environ, GRANDLEON_DOCKER=str(mock),
                       GRANDLEON_ARES_COMMIT="test-revision",
                       GRANDLEON_N64_ARES_IMAGE="test-image",
                       GRANDLEON_N64_BUILD_DIR=str(root),
                       GRANDLEON_N64_ARES_LOG="case.log",
                       GRANDLEON_N64_ARES_REQUIRE="^CHECKPOINT READY",
                       GRANDLEON_N64_ARES_ALLOW_TIMEOUT="1" if allow else "0",
                       GRANDLEON_N64_ARES_INPUT_SEQUENCE="wait:1" if inputs else "",
                       MOCK_EXIT_STATUS=str(status), MOCK_INPUT_STATUS=marker)
            return subprocess.run(["bash", str(HERE / "run-ares.sh"), str(rom)],
                                  env=env, capture_output=True, text=True, timeout=10)

    def test_only_expected_timeout_can_be_accepted(self):
        self.assertEqual(self.run_case(124).returncode, 0)
        self.assertEqual(self.run_case(0).returncode, 0)
        for status in (1, 2, 125, 137):
            with self.subTest(status=status):
                self.assertNotEqual(self.run_case(status).returncode, 0)
        self.assertNotEqual(self.run_case(124, allow=False).returncode, 0)

    def test_controller_sequence_must_finish_successfully(self):
        self.assertEqual(self.run_case(124, inputs=True, marker="0").returncode, 0)
        for marker in ("missing", "1", "124"):
            with self.subTest(marker=marker):
                self.assertNotEqual(self.run_case(124, inputs=True, marker=marker).returncode, 0)
        self.assertNotEqual(self.run_case(0, inputs=True, marker="1").returncode, 0)

    def test_previous_controller_success_cannot_mask_an_incomplete_run(self):
        self.assertNotEqual(self.run_case(124, inputs=True, stale=True).returncode, 0)


if __name__ == "__main__":
    unittest.main()
