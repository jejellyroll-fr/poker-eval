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


PARAMETERS = {
    "fixed": (1.5, 0.0, 2.0),
    "hs30": (1.06, -1.04, 29.9),
}


def report(schedule: str, exploitability: float) -> dict:
    alpha, beta, gamma = PARAMETERS[schedule]
    return {
        "dcfr_schedule": schedule,
        "dcfr_params": {
            "iteration": 5,
            "alpha": alpha,
            "beta": beta,
            "gamma": gamma,
        },
        "precision": "f64",
        "peak_rss_bytes": 1048576,
        "progress": {"target_reached": False},
        "metrics": {
            "br_mode": "exact",
            "guarantee": "nash",
            "exploitability_mbb_per_game": exploitability,
            "br_gap": [exploitability / 2.0, exploitability / 2.0],
            "measurement_iteration": 5,
        },
        "timing": {
            "elapsed_seconds": 0.1,
            "solve_seconds": 0.05,
            "br_seconds": 0.05,
            "checkpoints": 1,
        },
        "checkpoints": [
            {
                "iteration": 5,
                "elapsed_seconds": 0.1,
                "solve_seconds": 0.05,
                "br_seconds": 0.05,
                "alpha": alpha,
                "beta": beta,
                "gamma": gamma,
                "exploitability_mbb_per_game": exploitability,
            }
        ],
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


class CheckpointValidationTests(unittest.TestCase):
    def test_missing_history_is_rejected(self) -> None:
        malformed = report("fixed", 800.0)
        del malformed["checkpoints"]

        with self.assertRaisesRegex(AssertionError, "no checkpoint history"):
            guard.measurement("fixed", malformed)

    def test_solve_and_br_time_cannot_exceed_elapsed(self) -> None:
        malformed = report("fixed", 800.0)
        malformed["checkpoints"][0]["br_seconds"] = 0.2

        with self.assertRaisesRegex(AssertionError, "solve \+ BR exceeds"):
            guard.measurement("fixed", malformed)

    def test_timing_summary_must_match_history(self) -> None:
        malformed = report("fixed", 800.0)
        malformed["timing"]["checkpoints"] = 2

        with self.assertRaisesRegex(AssertionError, "wrong count"):
            guard.measurement("fixed", malformed)

    def test_missing_parameters_are_rejected(self) -> None:
        malformed = report("fixed", 800.0)
        malformed["checkpoints"][0].pop("alpha")

        with self.assertRaisesRegex(AssertionError, "no finite DCFR parameters"):
            guard.measurement("fixed", malformed)

    def test_root_parameters_must_describe_the_last_checkpoint(self) -> None:
        malformed = report("fixed", 800.0)
        malformed["dcfr_params"]["alpha"] = 2.0

        with self.assertRaisesRegex(AssertionError, "disagree with the last"):
            guard.measurement("fixed", malformed)

    def test_identical_trajectories_fail_the_guard(self) -> None:
        def fake_solve(solver, schedule, combos, iterations, workdir):
            shared = report("fixed", 800.0 if schedule == "fixed" else 700.0)
            shared["dcfr_schedule"] = schedule
            return shared

        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            with mock.patch.object(guard, "solve", side_effect=fake_solve):
                with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    self.assertEqual(
                        guard.main(["guard", "--solver", str(solver)]), 1
                    )


if __name__ == "__main__":
    unittest.main()
