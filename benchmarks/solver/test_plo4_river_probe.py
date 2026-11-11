#!/usr/bin/env python3
"""Validation coverage for the PLO4 river probe (issue #280)."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import plo4_river_probe as probe


def good_report() -> dict:
    return {
        "schema": probe.BENCH_SCHEMA,
        "exact": True,
        "sampled": False,
        "board": "Ks9d4c2hTs",
        "pot": 63,
        "seed": 20260906,
        "repetitions": 3,
        "results": [
            {
                "id": "small",
                "exact": True,
                "hero_combos": 16,
                "opp_combos": 16,
                "zero_reach_opp_combos": 1,
                "evaluated_pairs": 200,
                "blocked_pairs": 56,
                "showdown_path": "pairwise",
                "setup_seconds": 0.001,
                "eval_seconds": {
                    "min": 0.002,
                    "median": 0.003,
                    "max": 0.004,
                    "total": 0.009,
                },
                "eval_seconds_per_call": 0.003,
                "pair_evaluations_per_second": 66666.0,
                "values_fingerprint": "0123456789abcdef",
                "deterministic_across_repetitions": True,
                "peak_rss_bytes": 12345678,
                "allocations": {
                    "available": True,
                    "counter": "mallinfo2.uordblks",
                    "unit": "bytes",
                    "before": 100,
                    "after": 100,
                    "net": 0,
                    "per_evaluation": 0.0,
                },
            }
        ],
    }


class ValidateBenchReportTests(unittest.TestCase):
    def test_accepts_exact_pairwise_report(self) -> None:
        probe.validate_bench_report(good_report())

    def test_rejects_sampled_report(self) -> None:
        report = good_report()
        report["sampled"] = True
        with self.assertRaises(probe.BenchValidationError):
            probe.validate_bench_report(report)

    def test_rejects_sorted_path_for_four_card_hands(self) -> None:
        report = good_report()
        report["results"][0]["showdown_path"] = "sorted"
        with self.assertRaises(probe.BenchValidationError):
            probe.validate_bench_report(report)

    def test_rejects_inconsistent_blocker_accounting(self) -> None:
        report = good_report()
        report["results"][0]["blocked_pairs"] = 1
        with self.assertRaises(probe.BenchValidationError):
            probe.validate_bench_report(report)

    def test_rejects_missing_setup_timing(self) -> None:
        report = good_report()
        del report["results"][0]["setup_seconds"]
        with self.assertRaises(probe.BenchValidationError):
            probe.validate_bench_report(report)

    def test_rejects_unsupported_allocation_count_without_reason(self) -> None:
        report = good_report()
        report["results"][0]["allocations"] = {"available": False}
        with self.assertRaises(probe.BenchValidationError):
            probe.validate_bench_report(report)


class ReproducibilityTests(unittest.TestCase):
    def test_identical_deterministic_fields_match(self) -> None:
        self.assertEqual(probe.deterministic_mismatches(good_report(), good_report()), [])

    def test_timing_changes_are_not_mismatches(self) -> None:
        first = good_report()
        second = copy.deepcopy(first)
        second["results"][0]["setup_seconds"] = 9.0
        second["results"][0]["eval_seconds"] = {
            "min": 7.0,
            "median": 8.0,
            "max": 9.0,
            "total": 24.0,
        }
        second["results"][0]["eval_seconds_per_call"] = 8.0
        second["results"][0]["peak_rss_bytes"] = 999
        self.assertEqual(probe.deterministic_mismatches(first, second), [])

    def test_fingerprint_change_is_a_mismatch(self) -> None:
        second = good_report()
        second["results"][0]["values_fingerprint"] = "fedcba9876543210"
        problems = probe.deterministic_mismatches(good_report(), second)
        self.assertTrue(any("values_fingerprint" in problem for problem in problems))


class BaselineTests(unittest.TestCase):
    def test_baseline_keeps_exact_fields_and_marks_timings_machine_specific(self) -> None:
        baseline = probe.build_baseline([good_report(), good_report()], bench_binary="build/bench")
        self.assertEqual(baseline["schema"], probe.BASELINE_SCHEMA)
        self.assertTrue(baseline["exact"])
        self.assertTrue(baseline["reproducibility"]["fingerprints_match"])
        self.assertTrue(baseline["timings_are_machine_specific"])
        result = baseline["results"][0]
        self.assertEqual(result["evaluated_pairs"], 200)
        self.assertEqual(result["values_fingerprint"], "0123456789abcdef")
        self.assertEqual(result["showdown_path"], "pairwise")

    def test_baseline_records_fingerprint_mismatches(self) -> None:
        second = good_report()
        second["results"][0]["values_fingerprint"] = "fedcba9876543210"
        baseline = probe.build_baseline([good_report(), second], bench_binary="build/bench")
        self.assertFalse(baseline["reproducibility"]["fingerprints_match"])
        self.assertTrue(baseline["reproducibility"]["mismatches"])

    def test_round_trips_through_json(self) -> None:
        baseline = probe.build_baseline([good_report()], bench_binary="build/bench")
        self.assertEqual(json.loads(json.dumps(baseline)), baseline)


if __name__ == "__main__":
    unittest.main()
