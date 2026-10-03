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

# A hand table as the CLI prints one. Copied from a real `plo4` run, which is
# the point: `RANGE GRID` is emitted only for Hold'em, so on PLO this is the
# only strategy evidence the report carries.
HAND_TABLE_HEADER = "hand\tnode\tactor\tfrequencies\tEV by action"
HAND_TABLE_ROWS = (
    "7cTc7d8h\t-1\tP1\tfold=75.0%,all-in=25.0%\tfold=pending,all-in=pending\t-",
    # Same tab layout, not strategy: the fingerprint has to skip it.
    "ev_update\t7cTc7d8h\t-1\tP1\tfold=-0.50,all-in=1.00",
    "4cJc8hTh\t-1\tP1\tfold=60.0%,all-in=40.0%\tfold=pending,all-in=pending\t-",
    # Uniform: a decision the training never reached. Its presence churns with
    # the --report-rows quota, so it must not reach the fingerprint.
    "KsQsJsTs\t0\tP2\tfold=50.0%,call=50.0%\tfold=pending,call=pending\t-",
)
HAND_TABLE_TAIL = "report_phase=complete rows=3"


def hand_table(*rows):
    return "\n".join(("HAND TABLE", HAND_TABLE_HEADER, *rows, HAND_TABLE_TAIL))


HAND_TABLE = hand_table(*HAND_TABLE_ROWS)


def memory(**overrides):
    """A `memory` block shaped like the CLI's."""
    base = {
        "total_infosets": 338,
        "storage_bytes": 983040,
        "hash_index_bytes": 65536,
        "metadata_bytes": 393216,
        "regret_bytes": 262144,
        "average_bytes": 262144,
        "retained_strategy_bytes": 983040,
        "bytes_per_infoset": 113.4,
        "bytes_per_strategy_slot": 56.7,
        "recompute_calls": 0,
        "bytes_saved_vs_full": 0,
    }
    base.update(overrides)
    return base


def report(**overrides):
    """A report shaped like the CLI's, with the fields the guard compares."""
    base = {
        "schema": "pe-preflop-solve/v1",
        "game": "holdem",
        "iterations": 5000,
        "infosets": 338,
        "memory": memory(),
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
        "strategy": bench.strategy_fingerprint(HAND_TABLE),
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


class StrategyFingerprintTests(unittest.TestCase):
    def test_a_hand_table_without_a_range_grid_still_fingerprints(self) -> None:
        # The regression this covers: `pe_preflop_solve.c` gates `RANGE GRID`
        # on `strcmp(options->game, "holdem") == 0`, so a fingerprint read from
        # it is empty on all three PLO spots and a guard comparing it is
        # vacuous there -- both arms agree on the empty list whatever they
        # trained. The hand table is emitted for every game.
        self.assertNotIn("RANGE GRID", HAND_TABLE)
        self.assertTrue(bench.strategy_fingerprint(HAND_TABLE))

    def test_the_fingerprint_is_hand_node_actor_and_frequencies(self) -> None:
        self.assertEqual(bench.strategy_fingerprint(HAND_TABLE),
                         [("4cJc8hTh", "-1", "P1", "fold=60.0%,all-in=40.0%"),
                          ("7cTc7d8h", "-1", "P1", "fold=75.0%,all-in=25.0%")])

    def test_a_uniform_decision_is_not_a_trained_strategy(self) -> None:
        # The rows that churn with the --report-rows quota are exactly the ones
        # the training never reached, and they all carry the uniform vector.
        # Excluding them is what lets the hand be kept without the guard
        # refusing a comparison it should accept.
        self.assertFalse(bench.trained(HAND_TABLE_ROWS[3].split("\t")))
        self.assertTrue(bench.trained(HAND_TABLE_ROWS[0].split("\t")))
        self.assertNotIn("KsQsJsTs", "".join(
            r[0] for r in bench.strategy_fingerprint(HAND_TABLE)))

    def test_a_decision_leaving_the_uniform_start_is_seen(self) -> None:
        # Excluding uniform rows must not make them permanently invisible: the
        # moment one is trained it enters the fingerprint.
        started = HAND_TABLE.replace("KsQsJsTs\t0\tP2\tfold=50.0%,call=50.0%",
                                     "KsQsJsTs\t0\tP2\tfold=40.0%,call=60.0%")
        self.assertNotEqual(started, HAND_TABLE)
        self.assertEqual(len(bench.strategy_fingerprint(HAND_TABLE)), 2)
        self.assertEqual(len(bench.strategy_fingerprint(started)), 3)

    def test_two_hands_exchanging_their_frequencies_is_detected(self) -> None:
        # The hand is what binds a frequency vector to a decision. Without it
        # the projection is a multiset of (node, actor, frequencies), and two
        # hands at the same node and actor that swap their vectors leave it
        # unchanged -- a per-decision change the guard would wave through.
        swapped = hand_table(
            HAND_TABLE_ROWS[0].replace("fold=75.0%,all-in=25.0%",
                                       "fold=60.0%,all-in=40.0%"),
            HAND_TABLE_ROWS[1],
            HAND_TABLE_ROWS[2].replace("fold=60.0%,all-in=40.0%",
                                       "fold=75.0%,all-in=25.0%"),
            HAND_TABLE_ROWS[3])
        without_hand = sorted((r[1], r[2], r[3])
                              for r in bench.strategy_fingerprint(HAND_TABLE))
        without_hand_swapped = sorted(
            (r[1], r[2], r[3]) for r in bench.strategy_fingerprint(swapped))
        self.assertEqual(without_hand, without_hand_swapped,
                         "the fixture must be a genuine exchange")
        self.assertNotEqual(bench.strategy_fingerprint(HAND_TABLE),
                            bench.strategy_fingerprint(swapped))

    def test_ev_update_rows_are_not_strategy(self) -> None:
        # They share the tab layout, so a parser that only splits on tabs
        # would read the sampled EV line as a second opinion on the strategy.
        self.assertEqual(len(bench.strategy_fingerprint(HAND_TABLE)), 2)

    def test_the_sampled_columns_do_not_enter_the_fingerprint(self) -> None:
        # Column 4 is the measurement's own sampled EV view and column 5 is
        # the sampled deal's board: both belong to the measurement, not to the
        # strategy, and neither may be compared between the arms.
        moved = (HAND_TABLE
                 .replace("fold=pending,all-in=pending", "fold=9.99,all-in=-9.99")
                 .replace("\t-\n", "\tAsKdQc\n"))
        self.assertNotEqual(moved, HAND_TABLE)
        self.assertEqual(bench.strategy_fingerprint(HAND_TABLE),
                         bench.strategy_fingerprint(moved))

    def test_the_row_order_does_not_enter_the_fingerprint(self) -> None:
        # `--report-rows` fills per-node quotas in storage-id order, so which
        # rows make the cut churns between two arms that trained identically.
        shuffled = hand_table(HAND_TABLE_ROWS[2], HAND_TABLE_ROWS[1],
                              HAND_TABLE_ROWS[0])
        self.assertNotEqual(shuffled, HAND_TABLE)
        self.assertEqual(bench.strategy_fingerprint(HAND_TABLE),
                         bench.strategy_fingerprint(shuffled))

    def test_a_frequency_change_is_read(self) -> None:
        changed = HAND_TABLE.replace("fold=75.0%,all-in=25.0%",
                                     "fold=80.0%,all-in=20.0%")
        self.assertNotEqual(bench.strategy_fingerprint(HAND_TABLE),
                            bench.strategy_fingerprint(changed))

    def test_a_report_without_a_hand_table_fingerprints_empty(self) -> None:
        self.assertEqual(bench.strategy_fingerprint("no table here"), [])


class StatisticsTests(unittest.TestCase):
    """The published claim is now an interval rather than a verdict, so the
    interval is load-bearing and gets its own tests."""

    def test_the_t_quantile_is_students_not_the_normal(self) -> None:
        # At five seeds the normal quantile would understate the interval by a
        # third, which is exactly the reading the reviewer objected to.
        self.assertAlmostEqual(bench.t95(4), 2.776, places=3)
        self.assertAlmostEqual(bench.t95(1), 12.706, places=3)
        self.assertAlmostEqual(bench.t95(30), 2.042, places=3)
        self.assertAlmostEqual(bench.t95(1000), 1.96, places=6)

    def test_the_paired_interval_matches_a_hand_computed_case(self) -> None:
        mean, sd, half = bench.paired_interval([1.0, 2.0, 3.0, 4.0, 5.0])
        self.assertAlmostEqual(mean, 3.0)
        self.assertAlmostEqual(sd, (2.5) ** 0.5)
        # half = t(0.975, 4) * sd / sqrt(n), with t pinned by the test above.
        self.assertAlmostEqual(half, 2.776 * sd / (5 ** 0.5), places=6)

    def test_a_single_seed_yields_a_degenerate_interval(self) -> None:
        # No spread can be estimated from one pair, so the interval is reported
        # as zero-width rather than invented.
        self.assertEqual(bench.paired_interval([7.0]), (7.0, 0.0, 0.0))

    def test_a_wide_spread_cannot_establish_equivalence(self) -> None:
        # The point of printing the interval: four seeds and a large spread
        # leave a half-width of ~4, so an effect of 3 is not excluded even
        # though a t-test would find nothing at the 5% level.
        _, _, half = bench.paired_interval([-10.0, 5.0, 0.0, 8.0])
        self.assertGreater(half, 3.0)

    def test_sd_of_is_zero_for_a_single_run(self) -> None:
        self.assertEqual(bench.sd_of([{"v": 1.0}], "v"), 0.0)
        self.assertAlmostEqual(bench.sd_of([{"v": 1.0}, {"v": 3.0}], "v"),
                               1.4142135623730951)


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
        # The byte totals move with them whenever a count crosses a capacity
        # doubling -- storage_v2.c derives them from slot_capacity and
        # meta_capacity, both of which double. Reproduced on plo4 heads-up at
        # seed 1, 500 iterations and --br-samples 9000: 22993 infosets on
        # 65536 hash slots against 22936 on 32768.
        base = run()
        grown = run(report=report(
            infosets=22993,
            memory=memory(total_infosets=22993, storage_bytes=2097576,
                          hash_index_bytes=262144, retained_strategy_bytes=2097152,
                          bytes_per_infoset=91.2, bytes_per_strategy_slot=45.6)))
        bench.assert_same_training(("plo4-hu", "plo4", 2), "aware", grown, base)

    def test_a_memory_policy_counter_is_still_refused(self) -> None:
        # The other side of that exemption: `recompute_calls` answers to the
        # memory policy, not to the measurement, so exempting the byte totals
        # must not have swallowed it.
        base = run()
        recomputed = run(report=report(memory=memory(recompute_calls=7)))
        self.assertRefused(("plo4-hu", "plo4", 2), "aware", recomputed, base,
                           "must not touch")

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
        other = run(strategy=bench.strategy_fingerprint(
            HAND_TABLE.replace("fold=75.0%,all-in=25.0%",
                               "fold=80.0%,all-in=20.0%")))
        self.assertRefused(("plo4-hu", "plo4", 2), "aware", other, base,
                           "strategy differs")

    def test_an_empty_fingerprint_is_refused(self) -> None:
        # An empty list compares equal to another empty list, so a report
        # format that lost the hand table would silently disarm the guard
        # rather than fail it. This is the failure mode the RANGE GRID
        # fingerprint had on PLO without anyone noticing.
        base = run()
        blind = run(strategy=[])
        self.assertRefused(("plo4-hu", "plo4", 2), "aware", blind, base,
                           "no hand table")


if __name__ == "__main__":
    unittest.main()
