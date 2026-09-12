#!/usr/bin/env python3
"""Regression coverage for strategy-frequency and output-name validation."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unicodedata
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_benchmarks as bench


class StrategyFrequencyValidationTests(unittest.TestCase):
    def test_strategy_rows_reject_actor_mismatches_from_tree(self) -> None:
        row = (
            "AhAs\t7\tP2\tCALL=50.0%,RAISE=50.0%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )

        data, _, details = bench.parse_strategy_rows(
            row,
            {7: "FLOP"},
            node_actors={7: "P1"},
        )

        self.assertEqual(data["FLOP"]["strategy_rows"], 0)
        self.assertEqual(details["actor_mismatch_rows"], 1)

    def test_tree_player_is_converted_to_report_actor_label(self) -> None:
        node_streets, node_actors, decisions = bench.tree_nodes(
            Path(__file__).parents[2] / "poker_eval_tree.json"
        )

        self.assertEqual(node_streets[0], "PREFLOP")
        self.assertEqual(node_actors[0], "P1")
        self.assertEqual(node_actors[3], "P2")
        self.assertEqual(decisions["PREFLOP"], 4)

    def test_invalid_frequencies_are_not_counted_as_learning(self) -> None:
        nan_row = (
            "AhAs\t7\tP1\tCALL=nan%,RAISE=nan%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )
        zero_sum_row = (
            "KhKd\t7\tP1\tCALL=0.0%,RAISE=0.0%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )

        data, _, details = bench.parse_strategy_rows(
            f"{nan_row}\n{zero_sum_row}\n",
            {7: "FLOP"},
            exhaustive_report=False,
        )

        self.assertEqual(data["FLOP"]["strategy_rows"], 2)
        self.assertEqual(data["FLOP"]["uniform_rows"], 0)
        self.assertEqual(data["FLOP"]["non_uniform_rows"], 0)
        self.assertEqual(data["FLOP"]["invalid_strategy_rows"], 2)
        self.assertEqual(details["invalid_strategy_rows"], 2)

    def test_validator_rejects_invalid_strategy_frequencies(self) -> None:
        row = (
            "AhAs\t7\tP1\tCALL=nan%,RAISE=nan%\t"
            "CALL=pending,RAISE=pending\tKs7d2c"
        )
        stdout = "\n".join(
            [
                "iterations=64 complete=1 infosets=1",
                (
                    "solve_loop_end cause=max_iterations iteration=64 "
                    "memory_mb=1.0 storage_mb=0.5 adapter_mb=0.5"
                ),
                (
                    "guarantee=empirical exploitability_raw=1.0 "
                    "exploitability_mbb=2.0 br_samples=16"
                ),
                "report_phase=starting rows=1 infosets=1",
                row,
                "report_phase=complete rows=1",
            ]
        )
        parsed = bench.parse_stdout(
            stdout,
            {street: 1 if street == "FLOP" else 0 for street in bench.STREETS},
            {7: "FLOP"},
            process_elapsed_seconds=1.0,
            solve_elapsed_seconds=0.25,
            requested_iterations=64,
            report_rows_requested=10,
        )
        result = {
            "process": {
                "returncode": 0,
                "solver_report": "case/run-1/solver-report.json",
                "solver_report_error": None,
            },
            "native_solver_report": {"schema": bench.NATIVE_REPORT_SCHEMA},
            "case": {"expect_streets": ["FLOP"]},
            "benchmark": parsed,
        }

        self.assertEqual(parsed["per_street"]["FLOP"]["non_uniform_rows"], 0)
        self.assertEqual(parsed["report"]["invalid_strategy_rows"], 1)
        self.assertIn(
            "invalid strategy frequencies in 1 row(s)",
            bench.validate_result(result),
        )

    def test_rounded_probability_distribution_is_accepted(self) -> None:
        values = bench.strategy_frequencies("CHECK=33.3%,CALL=33.3%,RAISE=33.4%")
        self.assertEqual(values, [33.3, 33.3, 33.4])


class ReservedOutputNameTests(unittest.TestCase):
    def test_aggregate_output_names_are_rejected_case_insensitively_before_cleanup(self) -> None:
        reserved_variants = [
            name
            for managed in bench.MANAGED_SUMMARY_FILES
            for name in (managed, managed.upper(), managed.title())
        ]
        for case_id in reserved_variants:
            with self.subTest(case_id=case_id), tempfile.TemporaryDirectory() as tmp:
                out_dir = Path(tmp)
                managed_dir = out_dir / "holdem_flop"
                managed_dir.mkdir()
                sentinel = managed_dir / "benchmark.json"
                sentinel.write_text("keep managed evidence", encoding="utf-8")

                with self.assertRaisesRegex(ValueError, "unsafe benchmark case id"):
                    bench.prepare_output_dir(
                        out_dir,
                        [{"id": "holdem_flop"}, {"id": case_id}],
                    )

                self.assertEqual(
                    sentinel.read_text(encoding="utf-8"),
                    "keep managed evidence",
                )

    def test_case_ids_that_differ_only_by_case_are_rejected_before_cleanup(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            managed_dir = out_dir / "holdem_flop"
            managed_dir.mkdir()
            sentinel = managed_dir / "benchmark.json"
            sentinel.write_text("keep managed evidence", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "duplicate benchmark case id"):
                bench.prepare_output_dir(
                    out_dir,
                    [{"id": "foo"}, {"id": "FOO"}],
                )

            self.assertEqual(
                sentinel.read_text(encoding="utf-8"),
                "keep managed evidence",
            )

    def test_case_ids_that_differ_only_by_unicode_normalization_are_rejected(self) -> None:
        composed = "caf\N{LATIN SMALL LETTER E WITH ACUTE}"
        decomposed = unicodedata.normalize("NFD", composed)

        with self.assertRaisesRegex(ValueError, "duplicate benchmark case id"):
            bench.validate_manifest_case_ids([
                {"id": composed},
                {"id": decomposed},
            ])

        self.assertEqual(bench.safe_case_id({"id": decomposed}), composed)


if __name__ == "__main__":
    unittest.main()
