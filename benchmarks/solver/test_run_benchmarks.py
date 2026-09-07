#!/usr/bin/env python3
"""Focused regression tests for the solver benchmark parser/validator."""

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_benchmarks as bench


class StrategyRowParsingTests(unittest.TestCase):
    def test_exhaustive_duplicate_sweep_preserves_real_multiplicity(self) -> None:
        same = (
            "AhAs\t7\tP1\tCALL=50.0%,RAISE=50.0%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )
        mixed = (
            "KhKd\t7\tP1\tCALL=25.0%,RAISE=75.0%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )
        # Two distinct solver infosets may render to the same stable row.
        # One sweep therefore legitimately contains [same, same, mixed].
        one_sweep = f"{same}\n{same}\n{mixed}\n"
        two_sweeps = one_sweep + one_sweep

        expected, expected_fingerprint, expected_details = bench.parse_strategy_rows(
            one_sweep, {7: "FLOP"}, exhaustive_report=False
        )
        actual, actual_fingerprint, actual_details = bench.parse_strategy_rows(
            two_sweeps, {7: "FLOP"}, exhaustive_report=True
        )

        self.assertEqual(actual, expected)
        self.assertEqual(actual_fingerprint, expected_fingerprint)
        self.assertEqual(actual["FLOP"]["strategy_rows"], 3)
        self.assertEqual(actual["FLOP"]["uniform_rows"], 2)
        self.assertEqual(actual["FLOP"]["non_uniform_rows"], 1)
        self.assertEqual(actual["FLOP"]["unique_nodes_with_rows"], 1)
        self.assertEqual(actual["FLOP"]["unique_boards"], 1)
        self.assertEqual(actual_details["raw_strategy_rows"], 6)
        self.assertEqual(actual_details["normalized_strategy_rows"], 3)
        self.assertTrue(actual_details["duplicate_exhaustive_sweep_removed"])
        self.assertEqual(expected_details["normalized_strategy_rows"], 3)

    def test_non_exhaustive_report_keeps_repeated_rendered_rows(self) -> None:
        row = (
            "AhAs\t7\tP1\tCALL=50.0%,RAISE=50.0%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )
        data, _, details = bench.parse_strategy_rows(
            f"{row}\n{row}\n", {7: "FLOP"}, exhaustive_report=False
        )

        self.assertEqual(data["FLOP"]["strategy_rows"], 2)
        self.assertFalse(details["duplicate_exhaustive_sweep_removed"])


class TelemetryParsingTests(unittest.TestCase):
    def test_solver_strategy_count_wins_over_capped_description_count(self) -> None:
        stdout = "\n".join(
            [
                "iterations=10000 complete=1 infosets=2194",
                (
                    "solve_loop_end cause=max_iterations iteration=10000 "
                    "memory_mb=100.0 storage_mb=60.0 adapter_mb=40.0"
                ),
                (
                    "stop_detail cause=max_iterations interrupted=0 "
                    "iteration=10000 held_mb=100.0 budget_mb=512.0 "
                    "descriptions_mb=1.0 descriptions_capped=1"
                ),
                (
                    "guarantee=empirical exploitability_raw=1.0 "
                    "exploitability_mbb=2.0 br_samples=16"
                ),
                "report_phase=starting rows=5829 infosets=2194",
                "report_phase=complete rows=0",
            ]
        )

        parsed = bench.parse_stdout(
            stdout,
            {street: 0 for street in bench.STREETS},
            {},
            process_elapsed_seconds=7.18,
            solve_elapsed_seconds=0.60,
            requested_iterations=10000,
            report_rows_requested=0,
        )

        self.assertEqual(parsed["infosets"], 5829)
        self.assertEqual(parsed["description_infosets"], 2194)
        self.assertAlmostEqual(
            parsed["memory"]["bytes_per_infoset"],
            100.0 * bench.MB / 5829,
        )
        self.assertAlmostEqual(
            parsed["memory"]["storage_bytes_per_infoset"],
            60.0 * bench.MB / 5829,
        )

    def test_throughput_uses_solve_time_not_full_process_time(self) -> None:
        stdout = "\n".join(
            [
                "iterations=10000 complete=1 infosets=100",
                (
                    "solve_loop_end cause=max_iterations iteration=10000 "
                    "memory_mb=10.0 storage_mb=6.0 adapter_mb=4.0"
                ),
                "report_phase=starting rows=100 infosets=100",
                "report_phase=complete rows=200",
            ]
        )

        parsed = bench.parse_stdout(
            stdout,
            {street: 0 for street in bench.STREETS},
            {},
            process_elapsed_seconds=7.18,
            solve_elapsed_seconds=0.50,
            requested_iterations=10000,
            report_rows_requested=0,
        )

        self.assertEqual(parsed["elapsed_seconds"], 7.18)
        self.assertEqual(parsed["solve_elapsed_seconds"], 0.50)
        self.assertAlmostEqual(parsed["post_solve_elapsed_seconds"], 6.68)
        self.assertAlmostEqual(parsed["iterations_per_second"], 20000.0)
        self.assertNotAlmostEqual(
            parsed["iterations_per_second"], 10000.0 / 7.18
        )


class ValidationTests(unittest.TestCase):
    def _valid_timing(self) -> dict[str, float]:
        return {"solve_elapsed_seconds": 0.1}

    def test_expected_full_tree_street_requires_materialized_rows(self) -> None:
        per_street = {
            street: {"decision_nodes": 1, "strategy_rows": 1}
            for street in bench.STREETS
        }
        per_street["RIVER"]["strategy_rows"] = 0
        result = {
            "process": {"returncode": 0},
            "case": {"expect_streets": list(bench.STREETS)},
            "benchmark": {
                "actual_iterations": 64,
                "requested_iterations": 64,
                "stop_cause": "max_iterations",
                "infosets": 10,
                **self._valid_timing(),
                "report": {"emitted_rows": 20},
                "per_street": per_street,
            },
        }

        failures = bench.validate_result(result)

        self.assertIn("no reported strategy row on RIVER", failures)
        self.assertNotIn("no reported strategy row on PREFLOP", failures)
        self.assertNotIn("no reported strategy row on FLOP", failures)
        self.assertNotIn("no reported strategy row on TURN", failures)

    def test_missing_report_start_solver_count_is_rejected(self) -> None:
        result = {
            "process": {"returncode": 0},
            "case": {"expect_streets": []},
            "benchmark": {
                "actual_iterations": 64,
                "requested_iterations": 64,
                "stop_cause": "max_iterations",
                "infosets": None,
                **self._valid_timing(),
                "report": {"emitted_rows": 0},
                "per_street": {},
            },
        }

        failures = bench.validate_result(result)

        self.assertIn("missing solver strategy count from report start", failures)

    def test_missing_solve_timing_markers_is_rejected(self) -> None:
        result = {
            "process": {"returncode": 0},
            "case": {"expect_streets": []},
            "benchmark": {
                "actual_iterations": 64,
                "requested_iterations": 64,
                "stop_cause": "max_iterations",
                "infosets": 10,
                "solve_elapsed_seconds": None,
                "report": {"emitted_rows": 1},
                "per_street": {},
            },
        }

        failures = bench.validate_result(result)

        self.assertIn("missing solver timing markers", failures)


if __name__ == "__main__":
    unittest.main()
