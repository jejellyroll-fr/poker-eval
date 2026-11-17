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

SPOT = "k9_certified"
ROLES = {
    entry["method"]: (entry["schedule"], entry["role"])
    for entry in probe.METHODS
}
VALUES = {
    "cfr": 900.0,
    "cfr_plus": 800.0,
    "cfr_plus_chance": 700.0,
    "dcfr": 850.0,
    "dcfr_hs15": 780.0,
    "dcfr_hs30": 720.0,
}


def run(method: str, seed: int, target: float | None, seconds: float,
        exploitability: float, iteration: int = 50,
        range_mode: str = "sampled") -> dict:
    return {
        "spot": SPOT,
        "method": method,
        "algorithm": method,
        "schedule": ROLES.get(method, (None, "reference"))[0],
        "role": ROLES.get(method, (None, "reference"))[1],
        "seed": seed,
        "target_mbb": target,
        "seconds": seconds,
        "reached": (exploitability <= target) if target is not None else None,
        "exploitability_mbb_per_game": exploitability,
        "measurement_iteration": iteration,
        "range_mode": range_mode,
    }


def fixed_run(method: str, seed: int, seconds: float, solve: float,
              br_seconds: float, exploitability: float,
              iteration: int = 160) -> dict:
    outcome = run(method, seed, None, seconds, exploitability, iteration)
    outcome.update({
        "complete": True,
        "br_mode": "exact",
        "guarantee": "nash",
        "stopping_reason": "iteration_budget",
        "dcfr_params": {"alpha": 1.5, "beta": 0.0, "gamma": 2.0},
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
    })
    return outcome


class SummaryTests(unittest.TestCase):
    def test_median_and_spread_over_reached_seeds(self) -> None:
        runs = [
            run("dcfr_hs30", 1, 5000.0, 0.30, 4200.0),
            run("dcfr_hs30", 2, 5000.0, 0.10, 4500.0),
            run("dcfr_hs30", 3, 5000.0, 0.20, 4800.0),
        ]
        entry = probe.summarize(runs, [SPOT], [5000.0])[SPOT]["dcfr_hs30"]["5000"]
        self.assertEqual(entry["runs"], 3)
        self.assertEqual(entry["reached"], 3)
        self.assertTrue(entry["all_reached"])
        self.assertAlmostEqual(entry["median_seconds"], 0.20)
        self.assertAlmostEqual(entry["min_seconds"], 0.10)
        self.assertAlmostEqual(entry["max_seconds"], 0.30)

    def test_partial_reach_is_not_reported_as_complete(self) -> None:
        runs = [
            run("dcfr", 1, 1000.0, 0.40, 900.0),
            run("dcfr", 2, 1000.0, 0.50, 1200.0),
        ]
        entry = probe.summarize(runs, [SPOT], [1000.0])[SPOT]["dcfr"]["1000"]
        self.assertEqual(entry["reached"], 1)
        self.assertFalse(entry["all_reached"])
        self.assertAlmostEqual(entry["median_seconds"], 0.40)
        self.assertEqual(entry["exploitability_mbb_per_game"], [900.0, 1200.0])

    def test_unreached_target_has_no_time(self) -> None:
        runs = [run("cfr", 1, 100.0, 1.0, 300.0)]
        entry = probe.summarize(runs, [SPOT], [100.0])[SPOT]["cfr"]["100"]
        self.assertEqual(entry["reached"], 0)
        self.assertIsNone(entry["median_seconds"])

    def test_markdown_marks_unreached(self) -> None:
        runs = [run("cfr", 1, 100.0, 1.0, 300.0)]
        summary = probe.summarize(runs, [SPOT], [100.0])
        table = probe.render_markdown(summary, [SPOT], [100.0])
        self.assertIn("### k9_certified", table)
        self.assertIn("| 100 | cfr | 0/1 | unreached |", table)

    def test_spots_are_rendered_separately(self) -> None:
        runs = [run("cfr", 1, 100.0, 1.0, 50.0)]
        summary = probe.summarize(runs, [SPOT], [100.0])
        table = probe.render_markdown(summary, [SPOT, "a7_dry"], [100.0])
        self.assertNotIn("### a7_dry", table)


class CostReportingTests(unittest.TestCase):
    """Issue #284: memory and certification overhead belong in the table."""

    @staticmethod
    def _measured(
        seed: int, seconds: float, solve: float, br_seconds: float,
        peak_rss: int, exploitability: float,
    ) -> dict:
        outcome = run("dcfr_hs30", seed, 5000.0, seconds, exploitability)
        outcome.update({
            "peak_rss_bytes": peak_rss,
            "storage_bytes": 4096,
            "infosets": 4,
            "stopping_reason": "target_reached",
            "target_bracket": {
                "target_mbb": 5000.0,
                "last_above": None,
                "first_below": {
                    "iteration": 40,
                    "elapsed_seconds": seconds,
                    "solve_seconds": solve,
                    "br_seconds": br_seconds,
                    "exploitability_mbb_per_game": exploitability,
                },
                "bracketed": True,
            },
        })
        return outcome

    def test_summary_reports_memory_and_certification_cost(self) -> None:
        runs = [
            self._measured(1, 0.30, 0.22, 0.08, 100 * 1024 * 1024, 4200.0),
            self._measured(2, 0.20, 0.12, 0.08, 200 * 1024 * 1024, 4300.0),
        ]
        entry = probe.summarize(runs, [SPOT], [5000.0])[SPOT]["dcfr_hs30"]["5000"]
        self.assertAlmostEqual(
            entry["median_solve_seconds_to_first_below"], 0.17
        )
        self.assertAlmostEqual(entry["median_br_seconds_to_first_below"], 0.08)
        self.assertEqual(entry["peak_rss_bytes"]["max"], 200 * 1024 * 1024)
        self.assertAlmostEqual(entry["solver_storage_bytes"]["median"], 4096.0)
        self.assertEqual(entry["stopping_reasons"], ["target_reached"])

    def test_markdown_includes_certification_and_memory(self) -> None:
        runs = [
            self._measured(1, 0.30, 0.22, 0.08, 200 * 1024 * 1024, 4200.0)
        ]
        table = probe.render_markdown(
            probe.summarize(runs, [SPOT], [5000.0]), [SPOT], [5000.0]
        )
        self.assertIn("| solve s | BR s | peak MiB |", table)
        self.assertIn("| 0.220 | 0.080 | 200.0 |", table)

    def test_unknown_memory_is_not_invented(self) -> None:
        entry = probe.summarize(
            [run("cfr", 1, 5000.0, 0.1, 4200.0)], [SPOT], [5000.0]
        )[SPOT]["cfr"]["5000"]
        self.assertIsNone(entry["peak_rss_bytes"]["median"])
        self.assertIsNone(entry["solver_storage_bytes"]["median"])


class MethodEffectTests(unittest.TestCase):
    def test_identical_methods_are_flagged(self) -> None:
        runs = [
            run(method, seed, 5000.0, 0.1, 700.0)
            for method in probe.METHOD_LABELS for seed in (1, 2)
        ]
        self.assertTrue(probe.methods_are_inert(runs))

    def test_different_methods_are_a_comparison(self) -> None:
        runs = [
            run(method, seed, 5000.0, 0.1, VALUES[method])
            for method in probe.METHOD_LABELS for seed in (1, 2)
        ]
        self.assertFalse(probe.methods_are_inert(runs))

    def test_missing_method_is_not_called_inert(self) -> None:
        runs = [
            run("cfr", seed, 5000.0, 0.1, 700.0)
            for seed in (1, 2)
        ]
        self.assertFalse(probe.methods_are_inert(runs))

    def test_identical_groups_are_listed_per_spot(self) -> None:
        runs = []
        for seed in (1, 2):
            runs.append(run("cfr_plus", seed, None, 0.1, 700.0))
            runs.append(run("cfr_plus_chance", seed, None, 0.1, 700.0))
            runs.append(run("cfr", seed, None, 0.1, 800.0))
        groups = probe.identical_method_groups(runs)
        self.assertEqual(len(groups), 1)
        self.assertEqual(groups[0]["methods"], ["cfr_plus", "cfr_plus_chance"])
        self.assertEqual(groups[0]["spot"], SPOT)

    def test_different_methods_are_not_grouped(self) -> None:
        runs = [
            run("cfr_plus", 1, None, 0.1, 700.0),
            run("cfr_plus_chance", 1, None, 0.1, 690.0),
        ]
        self.assertEqual(probe.identical_method_groups(runs), [])


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

    def test_spots_can_be_selected_or_all(self) -> None:
        self.assertEqual(probe.parse_spots("all"), list(probe.spot_ids()))
        self.assertEqual(
            probe.parse_spots("k9_certified,a7_dry"),
            ["k9_certified", "a7_dry"],
        )
        with self.assertRaises(Exception):
            probe.parse_spots("nope")

    def test_fixed_horizon_requires_divisible_interval(self) -> None:
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            probe.main([
                "probe", "--fixed-horizon", "--iterations", "160",
                "--interval", "3",
            ])

    def test_spots_declare_five_distinct_cards(self) -> None:
        for spot in probe.SPOTS:
            cards = [
                spot["board"][index:index + 2]
                for index in range(0, len(spot["board"]), 2)
            ]
            self.assertEqual(len(cards), 5, spot["id"])
            self.assertEqual(len(set(cards)), 5, spot["id"])
            self.assertLessEqual(spot["bet_size"], spot["stack"], spot["id"])


class CommandTests(unittest.TestCase):
    @staticmethod
    def _method(label: str) -> dict:
        return next(
            entry for entry in probe.METHODS if entry["method"] == label
        )

    def test_command_pins_spot_and_method(self) -> None:
        spot = probe.spot_by_id("a7_dry")
        command = probe.build_command(
            Path("solver"), spot, self._method("dcfr_hs30"), 7, 5000.0,
            100, 8, 512, Path("out.json"),
        )
        self.assertEqual(command[command.index("--board") + 1], "As7c5d3h2s")
        self.assertEqual(command[command.index("--algorithm") + 1], "dcfr")
        self.assertEqual(
            command[command.index("--dcfr-schedule") + 1], "hs30"
        )
        self.assertEqual(
            command[command.index("--target-mbb") + 1], f"{5000.0:.17g}"
        )

    def test_non_dcfr_method_has_no_schedule_flag(self) -> None:
        spot = probe.spot_by_id("k9_certified")
        command = probe.build_command(
            Path("solver"), spot, self._method("cfr_plus"), 7, None,
            160, 2, 512, Path("out.json"),
        )
        self.assertEqual(command[command.index("--algorithm") + 1], "cfr+")
        self.assertNotIn("--dcfr-schedule", command)
        self.assertNotIn("--target-mbb", command)


class FixedHorizonTests(unittest.TestCase):
    def _runs(self) -> list[dict]:
        return [
            fixed_run("cfr", 1, 3.0, 2.0, 1.0, 800.0),
            fixed_run("dcfr", 1, 3.2, 2.2, 1.0, 750.0),
        ]

    def test_summary_separates_solve_from_br(self) -> None:
        summary = probe.summarize_fixed(self._runs(), [SPOT])
        entry = summary[SPOT]["dcfr"]
        self.assertEqual(entry["completed"], 1)
        self.assertAlmostEqual(entry["median_solve_seconds"], 2.2)
        self.assertAlmostEqual(
            entry["median_solve_ms_per_iteration"], 2.2 / 160.0 * 1000.0
        )
        self.assertAlmostEqual(entry["median_br_seconds"], 1.0)
        self.assertEqual(entry["median_final_iteration"], 160.0)

    def test_markdown_reports_completion_and_horizon(self) -> None:
        table = probe.render_fixed_markdown(
            probe.summarize_fixed(self._runs(), [SPOT]), [SPOT]
        )
        self.assertIn("### k9_certified", table)
        self.assertIn("| dcfr | control | 1/1 | 160 |", table)

    def test_intermediate_reports_go_to_a_scratch_directory(self) -> None:
        seen: list[Path] = []

        def fake_run_once(solver, spot, method, seed, target,
                          iterations, interval, combos, workdir):
            seen.append(Path(workdir))
            return fixed_run(method["method"], seed, 0.1, 0.08, 0.02,
                             VALUES[method["method"]], iterations)

        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            output = Path(directory) / "baseline.json"
            with (
                mock.patch.object(probe, "preflight", return_value=None),
                mock.patch.object(probe, "run_once", side_effect=fake_run_once),
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                probe.main([
                    "probe", "--solver", str(solver), "--fixed-horizon",
                    "--iterations", "160", "--seeds", "1",
                    "--spot", SPOT, "--output", str(output),
                ])
            self.assertEqual(
                [path.name for path in output.parent.glob("report-*.json")], []
            )

        self.assertTrue(seen)
        self.assertEqual(len(set(seen)), 1)
        self.assertFalse(seen[0].exists())

    def test_omit_checkpoints_keeps_summary_and_metrics(self) -> None:
        def fake_run_once(solver, spot, method, seed, target,
                          iterations, interval, combos, workdir):
            return fixed_run(method["method"], seed, 0.1, 0.08, 0.02,
                             VALUES[method["method"]], iterations)

        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            output = Path(directory) / "trimmed.json"
            with (
                mock.patch.object(probe, "preflight", return_value=None),
                mock.patch.object(probe, "run_once", side_effect=fake_run_once),
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                status = probe.main([
                    "probe", "--solver", str(solver), "--fixed-horizon",
                    "--iterations", "160", "--seeds", "1", "--spot", SPOT,
                    "--omit-checkpoints", "--output", str(output),
                ])
            self.assertEqual(status, 0)
            document = json.loads(output.read_text(encoding="utf-8"))

        self.assertTrue(document["checkpoints_omitted"])
        for outcome in document["runs"]:
            self.assertNotIn("checkpoints", outcome)
        self.assertEqual(
            document["summary"][SPOT]["dcfr_hs30"][
                "median_exploitability_mbb_per_game"
            ],
            720.0,
        )

    def test_unavailable_method_is_recorded_per_spot(self) -> None:
        def fake_preflight(solver, spot, method, workdir):
            if method["method"] == "cfr_plus_chance":
                return "pe-plo4-lane-a exited with 1: solve failed"
            return None

        def fake_run_once(solver, spot, method, seed, target,
                          iterations, interval, combos, workdir):
            return fixed_run(method["method"], seed, 0.1, 0.08, 0.02,
                             VALUES[method["method"]], iterations)

        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            output = Path(directory) / "report.json"
            with (
                mock.patch.object(probe, "preflight", side_effect=fake_preflight),
                mock.patch.object(probe, "run_once", side_effect=fake_run_once),
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                status = probe.main([
                    "probe", "--solver", str(solver), "--fixed-horizon",
                    "--iterations", "160", "--seeds", "1", "--spot", SPOT,
                    "--output", str(output),
                ])
            self.assertEqual(status, 0)
            document = json.loads(output.read_text(encoding="utf-8"))

        unavailable = document["methods"]["unavailable"][SPOT]
        self.assertIn("cfr_plus_chance", unavailable)
        self.assertIn("solve failed", unavailable["cfr_plus_chance"])
        self.assertNotIn(
            "cfr_plus_chance", document["summary"][SPOT]
        )
        self.assertIn("cfr_plus", document["summary"][SPOT])


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
        self.assertEqual(bracket["first_below"]["iteration"], 15)

    def test_unreached_target_has_no_first_below(self) -> None:
        bracket = probe.target_bracket(
            [{"iteration": 5, "elapsed_seconds": 0.1,
              "exploitability_mbb_per_game": 2500.0}], 1000.0
        )
        self.assertFalse(bracket["bracketed"])
        self.assertIsNone(bracket["first_below"])

    def test_target_already_met_has_no_last_above(self) -> None:
        bracket = probe.target_bracket(
            [{"iteration": 5, "elapsed_seconds": 0.1,
              "exploitability_mbb_per_game": 800.0}], 1000.0
        )
        self.assertTrue(bracket["bracketed"])
        self.assertIsNone(bracket["last_above"])


class SeedEffectTests(unittest.TestCase):
    """Issue #284: repeated seeds must actually change a sampled range."""

    def test_identical_seeds_on_a_sampled_range_are_flagged(self) -> None:
        runs = [
            run("cfr", 1, 5000.0, 0.1, 800.0),
            run("cfr", 2, 5000.0, 0.1, 800.0),
        ]
        self.assertTrue(probe.seeds_are_inert(runs))

    def test_enumerated_range_is_not_flagged(self) -> None:
        runs = [
            run("cfr", 1, 5000.0, 0.1, 800.0, range_mode="enumerated"),
            run("cfr", 2, 5000.0, 0.1, 800.0, range_mode="enumerated"),
        ]
        self.assertFalse(probe.seeds_are_inert(runs))

    def test_single_seed_is_not_a_distribution(self) -> None:
        self.assertFalse(
            probe.seeds_are_inert([run("cfr", 1, 5000.0, 0.1, 800.0)])
        )

    def test_different_measurements_are_a_distribution(self) -> None:
        runs = [
            run("cfr", 1, 5000.0, 0.1, 800.0),
            run("cfr", 2, 5000.0, 0.1, 790.0),
        ]
        self.assertFalse(probe.seeds_are_inert(runs))

    def test_mixed_range_modes_are_not_flagged(self) -> None:
        runs = [
            run("cfr", 1, 5000.0, 0.1, 800.0, range_mode="sampled"),
            run("cfr", 2, 5000.0, 0.1, 800.0, range_mode="enumerated"),
        ]
        self.assertFalse(probe.seeds_are_inert(runs))


class MainTests(unittest.TestCase):
    def _main(self, exploitability: dict[str, float],
              extra: list[str] | None = None) -> int:
        def fake_run_once(solver, spot, method, seed, target,
                          iterations, interval, combos, workdir):
            outcome = run(method["method"], seed, target, 0.1,
                          exploitability[method["method"]])
            outcome.update({"br_mode": "exact", "guarantee": "nash"})
            return outcome

        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            output = Path(directory) / "report.json"
            with (
                mock.patch.object(probe, "preflight", return_value=None),
                mock.patch.object(probe, "run_once", side_effect=fake_run_once),
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                return probe.main([
                    "probe", "--solver", str(solver), "--targets", "1000",
                    "--seeds", "1", "--iterations", "10", "--spot", SPOT,
                    "--output", str(output),
                ] + (extra or []))

    def test_inert_methods_fail_the_run(self) -> None:
        self.assertEqual(
            self._main({method: 800.0 for method in probe.METHOD_LABELS}), 1
        )

    def test_active_methods_succeed(self) -> None:
        self.assertEqual(self._main(VALUES), 0)

    def test_identical_methods_are_reported(self) -> None:
        values = dict(VALUES)
        values["cfr_plus_chance"] = values["cfr_plus"]
        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            output = Path(directory) / "report.json"

            def fake_run_once(solver, spot, method, seed, target,
                              iterations, interval, combos, workdir):
                outcome = run(method["method"], seed, target, 0.1,
                              values[method["method"]])
                outcome.update({"br_mode": "exact", "guarantee": "nash"})
                return outcome

            with (
                mock.patch.object(probe, "preflight", return_value=None),
                mock.patch.object(probe, "run_once", side_effect=fake_run_once),
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                status = probe.main([
                    "probe", "--solver", str(solver), "--targets", "1000",
                    "--seeds", "1", "--iterations", "10", "--spot", SPOT,
                    "--output", str(output),
                ])
            document = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(status, 0)
        self.assertFalse(document["diagnostics"]["methods_are_inert"])
        groups = document["diagnostics"]["identical_method_groups"]
        self.assertEqual(groups[0]["methods"], ["cfr_plus", "cfr_plus_chance"])

    def test_document_records_spots_and_methods(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            solver = Path(directory) / "solver"
            solver.write_text("", encoding="utf-8")
            output = Path(directory) / "report.json"

            def fake_run_once(solver, spot, method, seed, target,
                              iterations, interval, combos, workdir):
                outcome = run(method["method"], seed, target, 0.1,
                              VALUES[method["method"]])
                outcome.update({"br_mode": "exact", "guarantee": "nash"})
                return outcome

            with (
                mock.patch.object(probe, "preflight", return_value=None),
                mock.patch.object(probe, "run_once", side_effect=fake_run_once),
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                probe.main([
                    "probe", "--solver", str(solver), "--targets", "1000",
                    "--seeds", "1", "--iterations", "10",
                    "--spot", "k9_certified,a7_dry",
                    "--output", str(output),
                ])
            document = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(document["schema"], probe.SCHEMA)
        self.assertEqual(
            [spot["id"] for spot in document["spots"]],
            ["k9_certified", "a7_dry"],
        )
        self.assertEqual(
            document["methods"]["compared"][0]["method"], "cfr"
        )
        self.assertEqual(
            {entry["method"] for entry in document["methods"]["unsupported"]},
            {"hs_pcfr_plus", "deep_predictive_cfr", "monker_import"},
        )
        self.assertIn("cpu_model", document["environment"])
        self.assertFalse(document["diagnostics"]["seeds_are_inert"])


if __name__ == "__main__":
    unittest.main()
