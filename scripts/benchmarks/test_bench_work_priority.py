#!/usr/bin/env python3
"""Regression coverage for the work-priority benchmark's parsing and guards.

The published table is only worth reading if the script refuses to produce it
from two runs that did not solve the same strategy, so the guard is the part
that most needs a test. The parsers are pinned against the lines the shipped
CLI actually prints.
"""

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bench_work_priority as bench


BR_SAMPLING = "br_sampling estimator=confidence-guided terminal_evaluations=44743"
BR_SAMPLING_LEGACY = "br_sampling terminal_evaluations=65122"
# The trap the anchored regex exists for: a solve prints this line too, and an
# unanchored `terminal_evaluations=(\d+)` matches it last, reporting the chance
# sampler's 36 as the best response's 44,743.
SAMPLING_TOTALS = "sampling_totals policy=standard terminal_evaluations=36"
BR_DECISIONS = (
    "br_decisions decisions=3071 samples=61172 avg_samples=19.919 "
    "early_stop_pct=99.97 separated=3070 tolerance_stops=0 max_budget_hits=754 "
    "single_action=0 eliminated=3070 mean_gap=30.7956 "
    "mean_gap_half_width=11.1299 mean_selection_value=15.2878 "
    "histogram=1749,517,586,214,5,0,0,0")
WORK_PRIORITY = (
    "work_priority items=333 buckets=8 coverage_promotions=0 "
    "aging_promotions=0 unresolved=0 mean_score=0.409534 mean_delay=300.838 "
    "p50_bucket=0 p90_bucket=1 depth=274,57,2,0,0,0,0,0")
STREET_STATS = (
    "street_stats street=preflop policy=standard visits=882 updates=764 "
    "chance_samples=500 unique_infosets=8758 uniform_rows=8757")


def report(**overrides):
    """A report shaped like the CLI's, with the fields the guard compares."""
    base = {
        "schema": "pe-preflop-solve/v1",
        "game": "holdem",
        "iterations": 5000,
        "infosets": 338,
        "memory": {"total_infosets": 338, "storage_bytes": 983040},
        "metrics": {
            "exploitability_mbb_per_game": 23770.8,
            "nash_conv_mbb_per_game": 23770.8,
            "max_br_gap": 10.0,
            "mean_br_gap": 10.0,
            "sample_count": 2000,
            "seed": 100,
        },
    }
    for key, value in overrides.items():
        base[key] = value
    return base


def run(**overrides):
    """A measured run, as the guard sees it."""
    measured = {
        "report": report(),
        "effort": bench.training_effort(STREET_STATS),
        "grid": ["RANGE GRID (highest-frequency action; F=fold C=call R=raise)",
                 "A  F F F"],
    }
    measured.update(overrides)
    return measured


class ParserTests(unittest.TestCase):
    def test_evals_regex_is_anchored_on_br_sampling(self) -> None:
        text = "\n".join((SAMPLING_TOTALS, BR_SAMPLING, SAMPLING_TOTALS))
        self.assertEqual(bench.EVALS_RE.findall(text),
                         [("confidence-guided", "44743")])

    def test_evals_regex_tolerates_a_solver_without_the_estimator(self) -> None:
        # Issue #274 added the field; an older binary omits it. The optional
        # group then reads as an empty string, which the caller's
        # `!= "confidence-guided"` check rejects -- so a row is never
        # mislabelled with an estimator the solver did not name.
        self.assertEqual(bench.EVALS_RE.findall(BR_SAMPLING_LEGACY),
                         [("", "65122")])
        self.assertNotEqual(bench.EVALS_RE.findall(BR_SAMPLING_LEGACY)[0][0],
                            "confidence-guided")

    def test_decisions_regex_reads_the_totals_it_reports(self) -> None:
        match = bench.DECISIONS_RE.search(BR_DECISIONS)
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1)), 3071)
        self.assertEqual(float(match.group(3)), 19.919)
        self.assertEqual(float(match.group(4)), 99.97)
        self.assertEqual(int(match.group(5)), 754)

    def test_priority_line_parses_into_the_histogram(self) -> None:
        stats = bench.parse_priority(WORK_PRIORITY)
        self.assertIsNotNone(stats)
        self.assertEqual(stats["items"], 333)
        self.assertEqual(stats["buckets"], 8)
        self.assertEqual(stats["p90_bucket"], 1)
        self.assertEqual(stats["depth"], [274, 57, 2, 0, 0, 0, 0, 0])
        self.assertAlmostEqual(stats["mean_score"], 0.409534)

    def test_priority_line_is_absent_under_the_fifo_baseline(self) -> None:
        # FIFO emits no priority line by design, and that has to read as
        # "nothing to report" rather than as a parse failure.
        self.assertIsNone(bench.parse_priority("no telemetry here"))

    def test_training_effort_reads_the_counters_not_the_infoset_counts(self) -> None:
        effort = bench.training_effort(STREET_STATS)
        self.assertEqual(effort, [("preflop", "standard", "882", "764", "500")])
        # unique_infosets and uniform_rows are deliberately not part of it:
        # the measurement grows the storage, so those two move with the policy.
        self.assertNotIn("8758", "".join("".join(row) for row in effort))

    def test_flatten_keys_by_dotted_path_and_reduces_lists(self) -> None:
        flat = bench.flatten({"a": {"b": 1}, "c": [1, 2, 3], "d": 4})
        self.assertEqual(flat, {"a.b": 1, "c": "<list 3>", "d": 4})


class TrainingGuardTests(unittest.TestCase):
    def assertRefused(self, spot, arm, candidate, baseline, needle) -> None:
        with self.assertRaises(SystemExit) as caught:
            bench.assert_same_training(spot, arm, candidate, baseline)
        self.assertIn(needle, str(caught.exception))

    def test_identical_runs_are_accepted(self) -> None:
        base = run()
        bench.assert_same_training(("holdem-hu", "holdem", 2), "aware", run(),
                                   base)

    def test_the_measurements_own_numbers_are_allowed_to_move(self) -> None:
        base = run()
        moved = run(report=report(metrics={
            "exploitability_mbb_per_game": 1.0,
            "nash_conv_mbb_per_game": 1.0,
            "max_br_gap": 2.0,
            "mean_br_gap": 2.0,
            "sample_count": 2000,
            "seed": 100,
        }))
        bench.assert_same_training(("holdem-hu", "holdem", 2), "aware", moved,
                                   base)

    def test_the_storage_the_measurement_grows_is_allowed_to_move(self) -> None:
        # The exemption is narrow and deliberate: the measurement resolves
        # infosets with create=1, so the storage counters move with the policy.
        base = run()
        grown = run(report=report(
            infosets=8810,
            memory={"total_infosets": 8810, "storage_bytes": 983040}))
        bench.assert_same_training(("holdem-hu", "holdem", 2), "aware", grown,
                                   base)

    def test_a_training_field_outside_the_exemption_is_refused(self) -> None:
        base = run()
        touched = run(report=report(iterations=4999))
        self.assertRefused(("holdem-hu", "holdem", 2), "aware", touched, base,
                           "must not touch")

    def test_a_report_that_changed_shape_is_refused(self) -> None:
        base = run()
        reshaped = run(report=report())
        del reshaped["report"]["memory"]
        self.assertRefused(("holdem-hu", "holdem", 2), "aware", reshaped, base,
                           "shape changed")

    def test_different_training_work_is_refused(self) -> None:
        base = run()
        other = run(effort=bench.training_effort(
            "street_stats street=preflop policy=standard visits=883 updates=764 "
            "chance_samples=500 unique_infosets=8758 uniform_rows=8757"))
        self.assertRefused(("holdem-hu", "holdem", 2), "aware", other, base,
                           "different work")

    def test_a_different_trained_strategy_is_refused(self) -> None:
        base = run()
        other = run(grid=["RANGE GRID (highest-frequency action; F=fold C=call)",
                          "A  C C C"])
        self.assertRefused(("holdem-hu", "holdem", 2), "aware", other, base,
                           "argmax differs")


if __name__ == "__main__":
    unittest.main()
