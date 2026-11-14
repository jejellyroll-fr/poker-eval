#!/usr/bin/env python3
"""Validation coverage for check_lane_a_schedules.py (issue #284)."""

from __future__ import annotations

import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_lane_a_schedules as guard


def report(schedule: str, exploitability: float) -> dict:
    return {
        "dcfr_schedule": schedule,
        "progress": {"target_reached": False},
        "metrics": {
            "br_mode": "exact",
            "guarantee": "nash",
            "exploitability_mbb_per_game": exploitability,
            "br_gap": [exploitability / 2.0, exploitability / 2.0],
        },
    }


class MainTests(unittest.TestCase):
    def _main(self, exploitability: dict[str, float]) -> int:
        def fake_solve(solver, schedule, combos, iterations, workdir):
            return report(schedule, exploitability[schedule])

        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            with mock.patch.object(guard, "solve", side_effect=fake_solve):
                with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    return guard.main(["guard", "--solver", str(solver)])

    def test_identical_schedules_fail(self) -> None:
        self.assertEqual(self._main({"fixed": 800.0, "hs30": 800.0}), 1)

    def test_different_schedules_pass(self) -> None:
        self.assertEqual(self._main({"fixed": 800.0, "hs30": 700.0}), 0)

    def test_missing_solver_fails(self) -> None:
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.assertEqual(guard.main(["guard", "--solver", "/missing"]), 2)


if __name__ == "__main__":
    unittest.main()
