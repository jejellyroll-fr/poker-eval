#!/usr/bin/env python3
"""Regression coverage for the native pe-preflop-solve report contract."""

from __future__ import annotations

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_benchmarks as bench


class NativeReportValidationTests(unittest.TestCase):
    def _valid_result(self) -> dict[str, object]:
        return {
            "command": [
                "solver",
                "--game", "holdem",
                "--players", "2",
                "--iterations", "64",
                "--samples", "1",
                "--br-samples", "16",
                "--algorithm", "external-mccfr",
                "--backend", "cpu_ref",
                "--precision", "f64",
            ],
            "process": {
                "returncode": 0,
                "solver_report": "case/run-1/solver-report.json",
                "solver_report_error": None,
            },
            "native_solver_report": {
                "schema": bench.NATIVE_REPORT_SCHEMA,
                "game": "holdem",
                "players": 2,
                "algorithm": "external-mccfr",
                "backend": "cpu_ref",
                "backend_validated": True,
                "precision": "f64",
                "simd_detected": "avx2",
                "simd_cfr_integrated": False,
                "iterations": 64,
                "showdown_samples": 1,
                "stack": 100.0,
                "small_blind": 0.5,
                "big_blind": 1.0,
                "ante": 0.0,
                "allow_nonallin_call": False,
                "postflop_streets": False,
                "br_samples": 16,
                # write_report() receives the description-table count.
                "infosets": 8,
                "progress": {"iteration": 64, "complete": True},
                "metrics": {
                    "guarantee": "empirical",
                    # Native JSON keeps %.17g, stdout keeps %.6f.
                    "exploitability_raw": 1.23456749,
                    "exploitability_mbb_per_game": 2.34567849,
                    "big_blind": 1.0,
                },
            },
            "case": {"expect_streets": []},
            "benchmark": {
                "actual_iterations": 64,
                "requested_iterations": 64,
                "complete": True,
                "stop_cause": "max_iterations",
                "solve_elapsed_seconds": 0.1,
                "infosets": 10,
                "description_infosets": 8,
                "metrics": {
                    "guarantee": "empirical",
                    "exploitability_raw": 1.234567,
                    "exploitability_mbb_per_game": 2.345678,
                    "requested_br_samples": 16,
                    "br_samples": 16,
                },
                "memory": {"descriptions_capped": True},
                "report": {
                    "emitted_rows": 1,
                    "completed": True,
                    "invalid_strategy_rows": 0,
                },
                "per_street": {},
            },
        }

    def test_complete_native_report_matches_configuration_and_stdout(self) -> None:
        self.assertEqual(bench.validate_result(self._valid_result()), [])

    def test_schema_only_native_report_is_rejected_as_incomplete(self) -> None:
        result = self._valid_result()
        result["native_solver_report"] = {"schema": bench.NATIVE_REPORT_SCHEMA}

        failures = bench.validate_result(result)

        self.assertIn("native solver report missing field 'game'", failures)
        self.assertIn("native solver report missing field 'progress'", failures)
        self.assertIn("native solver report missing field 'metrics'", failures)

    def test_native_report_conflicts_are_rejected(self) -> None:
        cases = (
            (
                "iterations",
                lambda result: result["native_solver_report"].__setitem__("iterations", 63),
                "native solver report iterations=63 != requested 64",
            ),
            (
                "br_samples",
                lambda result: result["native_solver_report"].__setitem__("br_samples", 8),
                "native solver report br_samples=8 != requested 16",
            ),
            (
                "description_infosets",
                lambda result: result["native_solver_report"].__setitem__("infosets", 9),
                "native solver report infosets=9 != stdout descriptions 8",
            ),
            (
                "progress_iteration",
                lambda result: result["native_solver_report"]["progress"].__setitem__(
                    "iteration", 63
                ),
                "native solver report progress.iteration=63 != stdout 64",
            ),
            (
                "progress_complete",
                lambda result: result["native_solver_report"]["progress"].__setitem__(
                    "complete", False
                ),
                "native solver report progress.complete=False != stdout True",
            ),
            (
                "guarantee",
                lambda result: result["native_solver_report"]["metrics"].__setitem__(
                    "guarantee", "nash"
                ),
                "native solver report metrics.guarantee='nash' != stdout 'empirical'",
            ),
            (
                "exploitability",
                lambda result: result["native_solver_report"]["metrics"].__setitem__(
                    "exploitability_raw", 1.5
                ),
                "native solver report metrics.exploitability_raw=1.5 != stdout 1.234567",
            ),
        )

        baseline = self._valid_result()
        for name, mutate, expected in cases:
            with self.subTest(field=name):
                result = copy.deepcopy(baseline)
                mutate(result)
                self.assertIn(expected, bench.validate_result(result))


if __name__ == "__main__":
    unittest.main()
