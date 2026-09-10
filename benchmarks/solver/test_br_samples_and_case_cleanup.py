#!/usr/bin/env python3
"""Regression coverage for BR-sample validation and stale case-path cleanup."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_benchmarks as bench


class CasefoldCleanupTests(unittest.TestCase):
    def test_previous_case_spelling_and_current_spelling_are_both_pruned(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            for case_id in ("Foo", "foo"):
                run_dir = out_dir / case_id / "run-1"
                run_dir.mkdir(parents=True)
                (run_dir / "benchmark.json").write_text("stale", encoding="utf-8")

            (out_dir / "selection.json").write_text(
                "{\n"
                f'  "schema": "{bench.SELECTION_SCHEMA}",\n'
                '  "cases": ["Foo"]\n'
                "}\n",
                encoding="utf-8",
            )

            bench.prepare_output_dir(out_dir, [{"id": "foo"}])

            self.assertFalse((out_dir / "Foo").exists())
            self.assertFalse((out_dir / "foo").exists())
            self.assertFalse((out_dir / "selection.json").exists())

    def test_first_run_refuses_unowned_case_directory_without_deleting_it(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            unrelated = out_dir / "tools"
            unrelated.mkdir()
            sentinel = unrelated / "pe-preflop-solve"
            sentinel.write_text("keep solver", encoding="utf-8")

            with self.assertRaisesRegex(
                ValueError, "refusing to overwrite unowned benchmark output path"
            ):
                bench.prepare_output_dir(out_dir, [{"id": "tools"}])

            self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep solver")

    def test_first_run_refuses_unowned_aggregate_file_without_deleting_it(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            summary = out_dir / "summary.json"
            summary.write_text("keep unrelated summary", encoding="utf-8")

            with self.assertRaisesRegex(
                ValueError, "refusing to overwrite unowned benchmark output path"
            ):
                bench.prepare_output_dir(out_dir, [{"id": "holdem_flop"}])

            self.assertEqual(
                summary.read_text(encoding="utf-8"), "keep unrelated summary"
            )


class BrSampleContractTests(unittest.TestCase):
    def _result(self, *, requested: int, reported: int) -> dict[str, object]:
        metrics = {
            "actual_iterations": 64,
            "requested_iterations": 64,
            "stop_cause": "max_iterations",
            "solve_elapsed_seconds": 0.1,
            "infosets": 1,
            "metrics": {
                "guarantee": "empirical",
                "exploitability_raw": 1.0,
                "exploitability_mbb_per_game": 2.0,
                "requested_br_samples": requested,
                "br_samples": reported,
            },
            "memory": {"descriptions_capped": True},
            "report": {"emitted_rows": 1, "completed": True},
            "per_street": {},
        }
        return {
            "process": {
                "returncode": 0,
                "solver_report": "case/run-1/solver-report.json",
                "solver_report_error": None,
            },
            "native_solver_report": {"schema": bench.NATIVE_REPORT_SCHEMA},
            "case": {"expect_streets": []},
            "benchmark": metrics,
        }

    def test_reported_br_samples_must_match_requested_budget(self) -> None:
        failures = bench.validate_result(self._result(requested=16, reported=8))
        self.assertIn("br_samples 8 != requested 16", failures)

    def test_matching_br_samples_satisfy_br_contract(self) -> None:
        failures = bench.validate_result(self._result(requested=16, reported=16))
        self.assertNotIn("missing br_samples telemetry", failures)
        self.assertNotIn("missing requested br_samples configuration", failures)
        self.assertFalse(any(failure.startswith("br_samples ") for failure in failures))


    def test_unspecified_guarantee_is_rejected(self) -> None:
        result = self._result(requested=16, reported=16)
        result["benchmark"]["metrics"]["guarantee"] = "unspecified"

        failures = bench.validate_result(result)

        self.assertIn("unspecified convergence guarantee telemetry", failures)

    def test_reproducibility_view_records_requested_and_reported_br_samples(self) -> None:
        result = self._result(requested=16, reported=16)
        benchmark = result["benchmark"]
        benchmark.update(
            {
                "description_infosets": 1,
                "report": {
                    "strategy_fingerprint_sha256": "abc",
                },
                "per_street": {
                    street: {
                        "strategy_rows": 0,
                        "uniform_rows": 0,
                        "non_uniform_rows": 0,
                        "unique_nodes_with_rows": 0,
                    }
                    for street in bench.STREETS
                },
            }
        )

        view = bench.stable_reproducibility_view(result)

        self.assertEqual(view["requested_br_samples"], 16)
        self.assertEqual(view["br_samples"], 16)


if __name__ == "__main__":
    unittest.main()
