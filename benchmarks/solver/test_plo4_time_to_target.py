#!/usr/bin/env python3
"""Validation coverage for the PLO4 time-to-target probe (issue #284)."""

from __future__ import annotations

import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import plo4_time_to_target as probe


def run(schedule: str, seed: int, target: float, seconds: float,
        exploitability: float, iteration: int = 50) -> dict:
    return {
        "schedule": schedule,
        "seed": seed,
        "target_mbb": target,
        "seconds": seconds,
        "reached": exploitability <= target,
        "exploitability_mbb_per_game": exploitability,
        "measurement_iteration": iteration,
    }


def fixed_run(schedule: str, seed: int, seconds: float, solve: float,
              br_seconds: float, exploitability: float,
              iteration: int = 160) -> dict:
    return {
        "schedule": schedule,
        "seed": seed,
        "target_mbb": None,
        "complete": True,
        "seconds": seconds,
        "br_mode": "exact",
        "guarantee": "nash",
        "exploitability_mbb_per_game": exploitability,
        "measurement_iteration": iteration,
        "checkpoints": [
            {
                "iteration": iteration,
                "elapsed_seconds": solve + br_seconds,
                "solve_seconds": solve,
                "br_seconds": br_seconds,
                "exploitability_mbb_per_game": exploitability,
            }
        ],
        "timing": {
            "elapsed_seconds": solve + br_seconds,
            "solve_seconds": solve,
            "br_seconds": br_seconds,
        },
    }


class SummaryTests(unittest.TestCase):
    def test_median_and_spread_over_reached_seeds(self) -> None:
        runs = [
            run("hs30", 1, 5000.0, 0.30, 4200.0),
            run("hs30", 2, 5000.0, 0.10, 4500.0),
            run("hs30", 3, 5000.0, 0.20, 4800.0),
        ]
        summary = probe.summarize(runs, [5000.0])
        entry = summary["hs30"]["5000"]
        self.assertEqual(entry["runs"], 3)
        self.assertEqual(entry["reached"], 3)
        self.assertTrue(entry["all_reached"])
        self.assertAlmostEqual(entry["median_seconds"], 0.20)
        self.assertAlmostEqual(entry["min_seconds"], 0.10)
        self.assertAlmostEqual(entry["max_seconds"], 0.30)

    def test_partial_reach_is_not_reported_as_complete(self) -> None:
        runs = [
            run("fixed", 1, 1000.0, 0.40, 900.0),
            run("fixed", 2, 1000.0, 0.50, 1200.0),
        ]
        entry = probe.summarize(runs, [1000.0])["fixed"]["1000"]
        self.assertEqual(entry["reached"], 1)
        self.assertFalse(entry["all_reached"])
        self.assertAlmostEqual(entry["median_seconds"], 0.40)
        self.assertEqual(entry["exploitability_mbb_per_game"], [900.0, 1200.0])

    def test_unreached_target_has_no_time(self) -> None:
        runs = [run("hs15", 1, 100.0, 1.0, 300.0)]
        entry = probe.summarize(runs, [100.0])["hs15"]["100"]
        self.assertEqual(entry["reached"], 0)
        self.assertFalse(entry["all_reached"])
        self.assertIsNone(entry["median_seconds"])
        self.assertIsNone(entry["min_seconds"])

    def test_markdown_marks_unreached(self) -> None:
        runs = [run("hs15", 1, 100.0, 1.0, 300.0)]
        summary = probe.summarize(runs, [100.0])
        table = probe.render_markdown(summary, [100.0])
        self.assertIn("| 100 | hs15 | 0/1 | unreached |", table)


class InertScheduleTests(unittest.TestCase):
    def test_identical_measurements_flag_the_schedule_dimension(self) -> None:
        runs = [
            run(schedule, seed, 5000.0, 0.10, 750.0, 60)
            for schedule in probe.SCHEDULES for seed in (1, 2)
        ]
        self.assertTrue(probe.schedules_are_inert(runs))

    def test_different_measurements_are_a_comparison(self) -> None:
        runs = [
            run("fixed", 1, 5000.0, 0.10, 750.0, 60),
            run("hs15", 1, 5000.0, 0.12, 740.0, 60),
            run("hs30", 1, 5000.0, 0.09, 730.0, 50),
        ]
        self.assertFalse(probe.schedules_are_inert(runs))

    def test_missing_schedules_are_not_called_inert(self) -> None:
        runs = [run("fixed", 1, 5000.0, 0.10, 750.0)]
        self.assertFalse(probe.schedules_are_inert(runs))


class ParsingTests(unittest.TestCase):
    def test_targets_are_sorted_and_positive(self) -> None:
        self.assertEqual(probe.parse_targets("1000,15000,5000"),
                         [15000.0, 5000.0, 1000.0])
        with self.assertRaises(Exception):
            probe.parse_targets("0")
        with self.assertRaises(Exception):
            probe.parse_targets("-5")

    def test_seeds_reject_negatives(self) -> None:
        self.assertEqual(probe.parse_seeds("1,2,3"), [1, 2, 3])
        with self.assertRaises(Exception):
            probe.parse_seeds("-1")

    def test_fixed_horizon_requires_divisible_interval(self) -> None:
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            probe.main([
                "probe", "--fixed-horizon", "--iterations", "160",
                "--interval", "3",
            ])


class CommandTests(unittest.TestCase):
    def test_command_pins_the_game_and_schedule(self) -> None:
        command = probe.build_command(
            Path("solver"), "hs30", 7, 5000.0, 100, 8, 512,
            Path("out.json"),
        )
        self.assertIn("--dcfr-schedule", command)
        self.assertEqual(command[command.index("--dcfr-schedule") + 1], "hs30")
        self.assertEqual(command[command.index("--board") + 1], probe.BOARD)
        self.assertEqual(command[command.index("--pot") + 1], f"{probe.POT:.17g}")
        self.assertEqual(command[command.index("--combos") + 1], "512")
        self.assertEqual(
            command[command.index("--target-mbb") + 1], f"{5000.0:.17g}"
        )
        self.assertEqual(command[command.index("--algorithm") + 1], "dcfr")

    def test_fixed_horizon_omits_target(self) -> None:
        command = probe.build_command(
            Path("solver"), "fixed", 7, None, 160, 2, 512,
            Path("out.json"),
        )
        self.assertNotIn("--target-mbb", command)
        self.assertEqual(command[command.index("--iterations") + 1], "160")


class FixedHorizonTests(unittest.TestCase):
    def test_summary_separates_solve_from_br(self) -> None:
        runs = [
            fixed_run("fixed", 1, 3.0, 2.0, 1.0, 250.0),
            fixed_run("fixed", 2, 3.2, 2.2, 1.0, 240.0),
            fixed_run("fixed", 3, 3.1, 2.1, 1.0, 245.0),
        ]

        entry = probe.summarize_fixed(runs)["fixed"]

        self.assertEqual(entry["completed"], 3)
        self.assertTrue(entry["all_completed"])
        self.assertAlmostEqual(entry["median_exploitability_mbb_per_game"], 245.0)
        self.assertAlmostEqual(entry["median_solve_seconds"], 2.1)
        self.assertAlmostEqual(entry["median_solve_ms_per_iteration"],
                               2.1 / 160.0 * 1000.0)
        self.assertAlmostEqual(entry["median_br_seconds"], 1.0)
        self.assertEqual(entry["median_final_iteration"], 160.0)

    def test_markdown_reports_completion_and_horizon(self) -> None:
        summary = probe.summarize_fixed(
            [fixed_run("fixed", 1, 3.0, 2.0, 1.0, 250.0)]
        )
        table = probe.render_fixed_markdown(summary)
        self.assertIn("| fixed | 1/1 | 160 |", table)
        self.assertIn("250.000", table)


class TargetBracketTests(unittest.TestCase):
    def test_bracket_reports_last_above_and_first_below(self) -> None:
        checkpoints = [
            {"iteration": 5, "elapsed_seconds": 0.1,
             "exploitability_mbb_per_game": 2500.0},
            {"iteration": 10, "elapsed_seconds": 0.3,
             "exploitability_mbb_per_game": 1271.0},
            {"iteration": 15, "elapsed_seconds": 0.5,
             "exploitability_mbb_per_game": 800.0},
        ]

        bracket = probe.target_bracket(checkpoints, 1000.0)

        self.assertTrue(bracket["bracketed"])
        self.assertEqual(bracket["last_above"]["iteration"], 10)
        self.assertEqual(bracket["last_above"]["exploitability_mbb_per_game"],
                         1271.0)
        self.assertEqual(bracket["first_below"]["iteration"], 15)
        self.assertEqual(bracket["first_below"]["elapsed_seconds"], 0.5)

    def test_unreached_target_has_no_first_below(self) -> None:
        bracket = probe.target_bracket(
            [{"iteration": 5, "elapsed_seconds": 0.1,
              "exploitability_mbb_per_game": 2500.0}], 1000.0
        )

        self.assertFalse(bracket["bracketed"])
        self.assertEqual(bracket["last_above"]["iteration"], 5)
        self.assertIsNone(bracket["first_below"])

    def test_target_already_met_has_no_last_above(self) -> None:
        bracket = probe.target_bracket(
            [{"iteration": 5, "elapsed_seconds": 0.1,
              "exploitability_mbb_per_game": 800.0}], 1000.0
        )

        self.assertTrue(bracket["bracketed"])
        self.assertIsNone(bracket["last_above"])
        self.assertEqual(bracket["first_below"]["iteration"], 5)


class MainTests(unittest.TestCase):
    def _main(self, exploitability_by_schedule: dict[str, float]) -> int:
        def fake_run_once(solver, schedule, seed, target,
                          iterations, interval, combos, workdir):
            return {
                "schedule": schedule,
                "seed": seed,
                "target_mbb": target,
                "requested_schedule": schedule,
                "horizon": 1000,
                "seconds": 0.1,
                "reached": True,
                "exploitability_mbb_per_game":
                    exploitability_by_schedule[schedule],
                "measurement_iteration": 10,
                "iterations_run": 10,
                "br_mode": "exact",
                "guarantee": "nash",
                "infosets": 4,
                "storage_bytes": 1024,
            }

        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            output = Path(directory) / "report.json"
            with (
                mock.patch.object(probe, "run_once", side_effect=fake_run_once),
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                return probe.main([
                    "probe", "--solver", str(solver), "--targets", "1000",
                    "--seeds", "1", "--iterations", "10",
                    "--output", str(output),
                ])

    def test_inert_schedules_fail_the_run(self) -> None:
        self.assertEqual(
            self._main({"fixed": 800.0, "hs15": 800.0, "hs30": 800.0}), 1
        )

    def test_active_schedules_succeed(self) -> None:
        self.assertEqual(
            self._main({"fixed": 800.0, "hs15": 750.0, "hs30": 700.0}), 0
        )

    def test_fixed_horizon_writes_fixed_mode(self) -> None:
        def fake_run_once(solver, schedule, seed, target,
                          iterations, interval, combos, workdir):
            return fixed_run(schedule, seed, 0.1, 0.08, 0.02,
                             {"fixed": 800.0, "hs15": 750.0, "hs30": 700.0}[
                                 schedule
                             ], iterations)

        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            output = Path(directory) / "fixed.json"
            with (
                mock.patch.object(probe, "run_once", side_effect=fake_run_once),
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                status = probe.main([
                    "probe", "--solver", str(solver), "--fixed-horizon",
                    "--iterations", "160", "--seeds", "1",
                    "--output", str(output),
                ])
            self.assertEqual(status, 0)
            document = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(document["schema"], "pe-plo4-time-to-target/v3")
        self.assertEqual(document["campaign_mode"], "fixed-horizon")
        self.assertEqual(document["game"]["targets_mbb_per_game"], [])
        self.assertEqual(document["summary"]["fixed"]["median_final_iteration"],
                         160.0)


if __name__ == "__main__":
    unittest.main()
