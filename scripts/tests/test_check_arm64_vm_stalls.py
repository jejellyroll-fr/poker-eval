#!/usr/bin/env python3
"""Lock the verdicts of check_arm64_vm_stalls.py on small hand-written logs.

Each fixture under fixtures/arm64-stall-check/ is a slice of the real job log
format: an ISO timestamp, then the output. They are deliberately tiny, so the
expected verdict is readable from the file itself rather than from the 9 830
lines of a real dispatch.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE.parent / "check_arm64_vm_stalls.py"
FIXTURES = HERE / "fixtures" / "arm64-stall-check"


def run(name, *args):
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(FIXTURES / name), *args],
        capture_output=True,
        text=True,
    )


class CleanLog(unittest.TestCase):
    def test_passes_and_reports_coverage(self):
        done = run("clean.log")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("2 sample(s)", done.stdout)
        self.assertIn("Coverage: first sample 0.0 s", done.stdout)
        self.assertIn("No window over 10 s.", done.stdout)

    def test_writes_the_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            summary = Path(tmp) / "summary.md"
            done = run("clean.log", "--summary", str(summary))
            self.assertEqual(done.returncode, 0)
            text = summary.read_text()
            self.assertIn("Coverage:", text)
            self.assertIn("No VM sampling gap", text)


class Windows(unittest.TestCase):
    def test_gap_between_samples_fails(self):
        done = run("stall.log")
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn("stall 2026-11-22T08:00:02", done.stdout)
        self.assertIn("(88 s)", done.stdout)
        self.assertIn("{ 1.10 0.60 0.45 }", done.stdout)

    def test_threshold_above_the_gap_passes(self):
        self.assertEqual(run("stall.log", "--threshold", "100").returncode, 0)


class Coverage(unittest.TestCase):
    def test_sampler_starting_late_fails(self):
        done = run("late.log")
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn("coverage start", done.stdout)
        self.assertIn("(120 s)", done.stdout)

    def test_sampler_dying_early_fails(self):
        done = run("dead.log")
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn("coverage end", done.stdout)
        self.assertIn("(240 s)", done.stdout)

    def test_job_stopped_at_its_limit_fails(self):
        done = run("killed.log")
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn("no `Total Test time` line", done.stdout)
        self.assertIn("the suite end is not recorded", done.stdout)


class NoSample(unittest.TestCase):
    def test_has_its_own_status(self):
        done = run("nosamples.log")
        self.assertEqual(done.returncode, 2)
        self.assertIn("No VM sample in this log", done.stdout)


class EscapeSequences(unittest.TestCase):
    def test_are_stripped_before_parsing(self):
        done = run("ansi.log")
        self.assertEqual(done.returncode, 0, done.stdout)
        self.assertIn("2 sample(s)", done.stdout)
        self.assertNotIn("\x1b", done.stdout)


if __name__ == "__main__":
    unittest.main()
