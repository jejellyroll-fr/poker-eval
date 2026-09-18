#!/usr/bin/env python3
"""Focused tests for the tier-scale comparison report (issue #247).

`tier_scale_report.py` is a pure transformation over the benchmark
summary: it groups the storage twins of a scale run into families, checks
that each family carries exactly one `full` baseline and both compact
variants, and derives the ratios the committed artifact reports. These
tests pin that contract with synthetic summaries so a regression in the
grouping or the ratio math fails without needing a solver run.
"""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tier_scale_report as scale


def make_summary_case(
    case_id: str,
    *,
    solve_seconds: float = 10.0,
    peak_bytes: int = 1_000_000,
    storage_bytes: int = 400_000,
    fingerprint: str = "deadbeef",
    validation_failures: list[str] | None = None,
    accounting: bool = True,
    iterations: int = 20000,
    infosets: int = 1000,
    stop_cause: str = "max_iterations",
    metrics_available: bool | None = True,
) -> dict:
    benchmark = {
        "infosets": infosets,
        "actual_iterations": iterations,
        "solve_elapsed_seconds": solve_seconds,
        "stop_cause": stop_cause,
        "memory": {
            "peak_measured_bytes": peak_bytes,
            "final_bytes": peak_bytes,
            "storage_bytes": storage_bytes,
            "adapter_bytes": 16777216,
        },
        "metrics": {
            "exploitability_mbb_per_game": 1125.0,
            "nash_conv_mbb_per_game": 1125.0,
            "metrics_available": metrics_available,
        },
        "report": {"strategy_fingerprint_sha256": fingerprint},
    }
    if accounting:
        benchmark["memory"]["solver_accounting"] = {
            "memory_policy": "full",
            "storage_bytes": storage_bytes,
            "retained_bytes": 1000,
            "recomputable_bytes": 2000,
            "recompute_calls": 7,
            "bytes_saved_vs_full": 123,
            "bytes_per_infoset": 118.2,
            "bytes_per_strategy_slot": 116.44,
        }
    return {
        "case": case_id,
        "returncode": 0,
        "validation_failures": validation_failures or [],
        "benchmark": benchmark,
    }


def make_summary(*cases: dict) -> dict:
    return {
        "schema": scale.BENCHMARK_SCHEMA,
        "results": list(cases),
        "reproducibility": {},
    }


class SplitFamilyCaseTests(unittest.TestCase):
    def test_accepts_every_known_suffix(self) -> None:
        for suffix in ("full", "compact", "deep"):
            self.assertEqual(
                scale.split_family_case(f"holdem_scale_{suffix}"),
                ("holdem", suffix),
            )

    def test_rejects_ids_without_marker_or_unknown_suffix(self) -> None:
        for case_id in ("holdem_tier_full", "holdem_scale", "holdem_scale_bogus", ""):
            self.assertIsNone(scale.split_family_case(case_id))

    def test_rpartition_keeps_multi_token_families(self) -> None:
        self.assertEqual(
            scale.split_family_case("plo4_big_scale_deep"),
            ("plo4_big", "deep"),
        )


class BuildScaleTests(unittest.TestCase):
    def test_derives_ratios_and_fingerprint_match(self) -> None:
        summary = make_summary(
            make_summary_case(
                "holdem_scale_full", solve_seconds=80.0, peak_bytes=23_000_000,
                storage_bytes=6_000_000, fingerprint="aaa",
            ),
            make_summary_case(
                "holdem_scale_compact", solve_seconds=40.0, peak_bytes=21_000_000,
                storage_bytes=4_500_000, fingerprint="aaa",
            ),
            make_summary_case(
                "holdem_scale_deep", solve_seconds=20.0, peak_bytes=20_000_000,
                storage_bytes=3_900_000, fingerprint="bbb",
            ),
        )

        document = scale.build_scale(summary, None)

        self.assertEqual(document["schema"], scale.SCALE_SCHEMA)
        cases = document["families"]["holdem"]["cases"]
        self.assertEqual([c["policy"] for c in cases], ["full", "compact", "deep"])

        baseline, compact, deep = cases
        self.assertNotIn("solve_ratio_vs_baseline", baseline)
        self.assertAlmostEqual(compact["solve_ratio_vs_baseline"], 0.5)
        self.assertAlmostEqual(compact["peak_ratio_vs_baseline"], 21 / 23)
        self.assertAlmostEqual(compact["storage_ratio_vs_baseline"], 0.75)
        self.assertTrue(compact["strategy_matches_baseline"])
        self.assertAlmostEqual(deep["solve_ratio_vs_baseline"], 0.25)
        self.assertFalse(deep["strategy_matches_baseline"])

    def test_copies_memory_accounting_when_present(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep", accounting=False),
        )
        document = scale.build_scale(summary, None)
        cases = {c["policy"]: c for c in document["families"]["holdem"]["cases"]}
        self.assertIn("memory_accounting", cases["full"])
        self.assertNotIn("memory_accounting", cases["deep"])

    def test_provenance_is_copied_from_selection(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        selection = {"solver": "/tmp/pe-preflop-solve", "manifest": "/tmp/cases.json"}
        document = scale.build_scale(summary, selection)
        self.assertEqual(document["solver"], "/tmp/pe-preflop-solve")
        self.assertEqual(document["manifest"], "/tmp/cases.json")

    def test_missing_variant_is_rejected(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
        )
        with self.assertRaises(SystemExit):
            scale.build_scale(summary, None)

    def test_missing_baseline_is_rejected(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        with self.assertRaises(SystemExit):
            scale.build_scale(summary, None)

    def test_duplicate_baseline_is_rejected(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        with self.assertRaises(SystemExit):
            scale.build_scale(summary, None)

    def test_malformed_marked_case_is_rejected(self) -> None:
        # A case that carries the marker but an unknown policy must fail,
        # not be silently dropped.
        summary = make_summary(make_summary_case("holdem_scale_bogus"))
        with self.assertRaises(SystemExit):
            scale.build_scale(summary, None)

    def test_cases_without_any_marker_are_ignored(self) -> None:
        # A merged summary may carry unrelated suites; they must not abort
        # the scale artifact.
        summary = make_summary(
            make_summary_case("holdem_tier_full"),
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        document = scale.build_scale(summary, None)
        self.assertEqual(list(document["families"]), ["holdem"])
        self.assertEqual(document["budget_families"], {})

    def test_wrong_summary_schema_is_rejected(self) -> None:
        summary = make_summary(make_summary_case("holdem_scale_full"))
        summary["schema"] = "pe-solver-benchmark-summary/v2"
        with self.assertRaises(SystemExit):
            scale.build_scale(summary, None)

    def test_families_are_sorted_and_independent(self) -> None:
        summary = make_summary(
            make_summary_case("plo4_scale_full"),
            make_summary_case("plo4_scale_compact"),
            make_summary_case("plo4_scale_deep"),
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        document = scale.build_scale(summary, None)
        self.assertEqual(list(document["families"]), ["holdem", "plo4"])

    def test_validation_failures_surface_on_the_case(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case(
                "holdem_scale_compact", validation_failures=["stop_cause='memory_budget'"]
            ),
            make_summary_case("holdem_scale_deep"),
        )
        document = scale.build_scale(summary, None)
        cases = {c["policy"]: c for c in document["families"]["holdem"]["cases"]}
        self.assertFalse(cases["compact"]["valid"])
        self.assertEqual(
            cases["compact"]["validation"], ["stop_cause='memory_budget'"]
        )
        self.assertTrue(cases["full"]["valid"])


class BudgetFamilyTests(unittest.TestCase):
    def _budget_summary(self) -> dict:
        return make_summary(
            make_summary_case(
                "holdem_budget_full", iterations=9000, infosets=25000,
                peak_bytes=4_000_000, storage_bytes=3_000_000, stop_cause="memory_budget",
            ),
            make_summary_case(
                "holdem_budget_compact", iterations=12300, infosets=34000,
                peak_bytes=4_000_000, storage_bytes=3_000_000, stop_cause="memory_budget",
            ),
            make_summary_case(
                "holdem_budget_deep", iterations=14400, infosets=40000,
                peak_bytes=4_000_000, storage_bytes=3_000_000, stop_cause="memory_budget",
            ),
        )

    def test_budget_family_reports_iterations_and_infosets_reached(self) -> None:
        document = scale.build_scale(self._budget_summary(), None)

        self.assertEqual(document["families"], {})
        cases = document["budget_families"]["holdem"]["cases"]
        self.assertEqual([c["policy"] for c in cases], ["full", "compact", "deep"])

        full, compact, deep = cases
        self.assertEqual(full["actual_iterations"], 9000)
        self.assertEqual(full["stop_cause"], "memory_budget")
        self.assertNotIn("actual_iterations_ratio_vs_baseline", full)
        self.assertAlmostEqual(compact["actual_iterations_ratio_vs_baseline"], 12300 / 9000)
        self.assertAlmostEqual(deep["actual_iterations_ratio_vs_baseline"], 14400 / 9000)
        self.assertAlmostEqual(compact["infosets_ratio_vs_baseline"], 34000 / 25000)
        self.assertAlmostEqual(deep["infosets_ratio_vs_baseline"], 40000 / 25000)
        # A binding budget makes the tiers stop at different iterations, so
        # fingerprint matching is not part of the budget comparison.
        self.assertNotIn("strategy_matches_baseline", compact)

    def test_budget_family_marks_unmeasured_convergence(self) -> None:
        # Issue #249: on a budget stop the convergence block was never
        # measured, so exploitability is 0.0 as an absence. The artifact has
        # to say which it is, or a reader cannot tell it from a converged
        # solve that reached zero.
        summary = make_summary(
            make_summary_case(
                "holdem_budget_full",
                iterations=9000,
                stop_cause="memory_budget",
                metrics_available=False,
            ),
            make_summary_case(
                "holdem_budget_compact",
                iterations=12300,
                stop_cause="memory_budget",
                metrics_available=False,
            ),
            make_summary_case(
                "holdem_budget_deep",
                iterations=14400,
                stop_cause="memory_budget",
                metrics_available=False,
            ),
        )
        document = scale.build_scale(summary, None)
        cases = document["budget_families"]["holdem"]["cases"]
        self.assertEqual([c["metrics_available"] for c in cases], [False] * 3)

    def test_measured_convergence_is_marked_measured(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        document = scale.build_scale(summary, None)
        cases = document["families"]["holdem"]["cases"]
        self.assertEqual([c["metrics_available"] for c in cases], [True] * 3)

    def test_scale_and_budget_families_coexist(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        summary["results"].extend(self._budget_summary()["results"])
        document = scale.build_scale(summary, None)
        self.assertEqual(list(document["families"]), ["holdem"])
        self.assertEqual(list(document["budget_families"]), ["holdem"])

    def test_merge_summaries_concatenates_results(self) -> None:
        left = make_summary(make_summary_case("holdem_scale_full"))
        right = make_summary(
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        merged = scale.merge_summaries([left, right])
        self.assertEqual(len(merged["results"]), 3)
        document = scale.build_scale(merged, None)
        self.assertEqual(len(document["families"]["holdem"]["cases"]), 3)

    def test_merge_rejects_foreign_schema(self) -> None:
        with self.assertRaises(SystemExit):
            scale.merge_summaries([{"schema": "nope", "results": []}])


class MainTests(unittest.TestCase):
    def _write(self, directory: Path, name: str, payload: dict) -> Path:
        path = directory / name
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def test_main_writes_document_and_exits_zero(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            summary_path = self._write(directory, "summary.json", summary)
            output_path = directory / "artifact.json"
            argv = [
                "tier_scale_report.py",
                "--summary", str(summary_path),
                "--output", str(output_path),
            ]
            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(scale.main(), 0)
            written = json.loads(output_path.read_text(encoding="utf-8"))
            self.assertEqual(written["schema"], scale.SCALE_SCHEMA)

    def test_main_merges_repeated_summary_flags(self) -> None:
        scale_summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact"),
            make_summary_case("holdem_scale_deep"),
        )
        budget_summary = make_summary(
            make_summary_case(
                "holdem_budget_full", stop_cause="memory_budget", iterations=9000
            ),
            make_summary_case(
                "holdem_budget_compact", stop_cause="memory_budget", iterations=12000
            ),
            make_summary_case(
                "holdem_budget_deep", stop_cause="memory_budget", iterations=14000
            ),
        )
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            first = self._write(directory, "scale.json", scale_summary)
            second = self._write(directory, "budget.json", budget_summary)
            output_path = directory / "artifact.json"
            argv = [
                "tier_scale_report.py",
                "--summary", str(first),
                "--summary", str(second),
                "--output", str(output_path),
            ]
            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(scale.main(), 0)
            written = json.loads(output_path.read_text(encoding="utf-8"))
            self.assertEqual(list(written["families"]), ["holdem"])
            self.assertEqual(list(written["budget_families"]), ["holdem"])

    def test_main_returns_one_when_a_case_is_invalid(self) -> None:
        summary = make_summary(
            make_summary_case("holdem_scale_full"),
            make_summary_case("holdem_scale_compact", validation_failures=["boom"]),
            make_summary_case("holdem_scale_deep"),
        )
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            summary_path = self._write(directory, "summary.json", summary)
            output_path = directory / "artifact.json"
            argv = [
                "tier_scale_report.py",
                "--summary", str(summary_path),
                "--output", str(output_path),
            ]
            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(scale.main(), 1)


if __name__ == "__main__":
    unittest.main()
