#!/usr/bin/env python3
"""Validation coverage for the PLO4 time-to-target probe (issue #284)."""

from __future__ import annotations

import io
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
            with mock.patch.object(probe, "run_once", side_effect=fake_run_once):
                with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
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


if __name__ == "__main__":
    unittest.main()
