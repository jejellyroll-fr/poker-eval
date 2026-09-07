#!/usr/bin/env python3
"""Focused regression tests for the solver benchmark parser/validator."""

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_benchmarks as bench


class StrategyRowParsingTests(unittest.TestCase):
    def test_exhaustive_duplicate_rows_are_counted_once(self) -> None:
        uniform = (
            "AhAs\t7\tP1\tCALL=50.0%,RAISE=50.0%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )
        mixed = (
            "KhKd\t7\tP1\tCALL=25.0%,RAISE=75.0%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )
        unique_stdout = f"{uniform}\n{mixed}\n"
        duplicated_stdout = f"{uniform}\n{uniform}\n{mixed}\n{mixed}\n"

        unique_data, unique_fingerprint = bench.parse_strategy_rows(
            unique_stdout, {7: "FLOP"}
        )
        duplicated_data, duplicated_fingerprint = bench.parse_strategy_rows(
            duplicated_stdout, {7: "FLOP"}
        )

        self.assertEqual(duplicated_data, unique_data)
        self.assertEqual(duplicated_fingerprint, unique_fingerprint)
        self.assertEqual(duplicated_data["FLOP"]["strategy_rows"], 2)
        self.assertEqual(duplicated_data["FLOP"]["uniform_rows"], 1)
        self.assertEqual(duplicated_data["FLOP"]["non_uniform_rows"], 1)
        self.assertEqual(duplicated_data["FLOP"]["unique_nodes_with_rows"], 1)
        self.assertEqual(duplicated_data["FLOP"]["unique_boards"], 1)


class ValidationTests(unittest.TestCase):
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
                "report": {"emitted_rows": 20},
                "per_street": per_street,
            },
        }

        failures = bench.validate_result(result)

        self.assertIn("no reported strategy row on RIVER", failures)
        self.assertNotIn("no reported strategy row on PREFLOP", failures)
        self.assertNotIn("no reported strategy row on FLOP", failures)
        self.assertNotIn("no reported strategy row on TURN", failures)


if __name__ == "__main__":
    unittest.main()
