#!/usr/bin/env python3
"""Regression coverage for the work-priority benchmark's parsing and guards.

The published table is only worth reading if the script refuses to produce it
from two runs that did not solve the same strategy, so the guard is the part
that most needs a test. The parsers are pinned against the lines the shipped
CLI actually prints.
"""

from __future__ import annotations

import contextlib
import io
import re
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bench_work_priority as bench

ROOT = Path(__file__).resolve().parents[2]

# The guide publishes the numbers this script prints, so a number that moves
# here has to move there. It carried one that was wrong: the interval's
# half-width, quoted as the largest shift the experiment admits.
GUIDE = ROOT / "docs" / "cfr" / "guides" / "work_priority_scheduling.md"

# The solver's sampling ceiling lives in C; the runner copies it. See
# CapCurveTests, which pins the copy against this file.
BR_SAMPLING_C = ROOT / "src" / "solver" / "domain" / "br_sampling.c"

# The solver's `--br-samples` bound lives here too -- it rejects 0 and anything
# above `UINT32_MAX`. The reference budget is spent through that flag, so the
# runner copies the bound; see ReferenceBudgetTests, which pins it against this
# file.
PREFLOP_SOLVE_C = ROOT / "tools" / "pe_preflop_solve.c"

# The stop-reason names the runner has to recognise live here. See
# StopReasonTests, which pins the one it accepts -- and the memory-budget name
# it refuses -- against this file rather than trusting the copy.
SOLVER_C = ROOT / "src" / "solver" / "domain" / "solver.c"


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


class FingerprintResolutionTests(unittest.TestCase):
    """The fingerprint's resolution is the report's, so the guide states it.

    `pe_preflop_solve.c` prints the frequency column with `%.1f%%`, and
    `strategy_fingerprint` takes the printed token verbatim, so the guard cannot
    resolve what the report did not print: one bin is 0.1 percentage points. The
    guide claimed the check misses "half a percentage point" -- five bins -- and
    the contradiction was internal, since the same paragraph had just stated the
    0.1-point granularity. A reader can falsify the wrong claim from the report
    itself, where 49.5% and 49.7% sit side by side.
    """

    STEP = 1e-5   # in probability: 0.001 percentage points

    @staticmethod
    def printed(probability):
        return "%.1f" % (probability * 100.0)

    def test_the_report_prints_the_column_to_a_tenth(self) -> None:
        # The measurement below is about this format, so pin it at the source: a
        # `%.2f` column would move the bin, and the guide's threshold with it.
        source = PREFLOP_SOLVE_C.read_text(encoding="utf-8")
        self.assertIn('printf("%s%s=%.1f%%", a ? "," : "",', source)

    def test_one_bin_is_a_tenth_of_a_point(self) -> None:
        # Derived from the format rather than asserted: the widest run of
        # probabilities that print identically is one bin wide.
        widest, start, previous = 0.0, 0.0, None
        p = 0.0
        while p <= 1.0 + self.STEP / 2.0:
            current = self.printed(p)
            if previous is None:
                start = p
            elif current != previous:
                widest = max(widest, (p - start) * 100.0)
                start = p
            previous = current
            p += self.STEP
        self.assertGreater(widest, 0.09)
        self.assertLessEqual(widest, 0.1 + 2.0 * self.STEP * 100.0)

    def test_four_tenths_of_a_point_always_shows(self) -> None:
        # The example the wrong claim turned on: 0.4 points is four bins wide,
        # so no probability in the range can hide it.
        hidden, p = [], 0.0
        while p + 0.004 <= 1.0:
            if self.printed(p) == self.printed(p + 0.004):
                hidden.append(p)
            p += self.STEP
        self.assertEqual(hidden, [])

    def test_a_fifth_of_a_point_shows(self) -> None:
        # Two frequencies the published report actually prints.
        self.assertNotEqual(self.printed(0.495), self.printed(0.497))

    def test_the_guide_states_the_bin_and_not_a_half_point(self) -> None:
        # Normalised first: the paragraph is wrapped, and a literal substring
        # search over a wrapped line cannot see the phrase it guards -- the old
        # claim read "half a\npercentage point", which the raw text does not
        # contain.
        text = " ".join(GUIDE.read_text(encoding="utf-8").split())
        self.assertNotIn("half a percentage point", text)
        self.assertIn("smaller than a tenth of a percentage point on every "
                      "decision at once", text)


class FingerprintBindingTests(unittest.TestCase):
    """The fingerprint has to bind a frequency vector to a decision.

    `(hand, node, actor)` does not do it: these runs pass no `--tree`, so `node`
    is the field's zero-initialised value for every state but the root, and
    measured on `plo4-3way` `distinct(hand, node, actor)` equals
    `distinct(hand, actor)` on all 17,424 rows of a 5,000-iteration report --
    the node never separates two rows the hand and actor do not already
    separate. Two *distinct* decisions sharing a hand and an actor therefore
    collapse onto one key, and exchanging their frequency vectors leaves the
    multiset unchanged.
    """

    SPOT = ("plo4-3way", "plo4", 3)
    COLLIDING = (
        "7cTc7d8h\t0\tP1\tfold=75.0%,all-in=25.0%\tfold=pending,all-in=pending\t-",
        "7cTc7d8h\t0\tP1\tfold=25.0%,call=75.0%\tfold=pending,call=pending\t-",
    )

    def assertRefused(self, rows) -> str:
        with self.assertRaises(SystemExit) as caught:
            bench.assert_fingerprint_binds(self.SPOT, "fifo", rows)
        return str(caught.exception)

    def test_two_trained_rows_sharing_a_key_with_different_vectors_are_refused(self) -> None:
        rows = bench.strategy_fingerprint(hand_table(*self.COLLIDING))
        self.assertEqual(len(rows), 2)
        message = self.assertRefused(rows)
        self.assertIn("7cTc7d8h", message)
        self.assertIn("fold=75.0%,all-in=25.0%", message)
        self.assertIn("fold=25.0%,call=75.0%", message)

    def test_the_swap_is_invisible_without_the_guard(self) -> None:
        # Why the guard exists rather than a stronger fingerprint: the two
        # colliding rows are interchangeable in the multiset, so a report whose
        # vectors were exchanged fingerprints identically. Measured on the real
        # 20,000-iteration `plo4-3way` report, swapping the two vectors of
        # `5c2hQhAs`/`P1` leaves `strategy_fingerprint` unchanged.
        rows = bench.strategy_fingerprint(hand_table(*self.COLLIDING))
        self.assertEqual(sorted(rows), sorted(reversed(rows)))

    def test_two_rows_sharing_a_key_with_the_same_vector_are_accepted(self) -> None:
        # Two copies of one tuple are unambiguous: changing either row moves the
        # multiset, since it then holds two different tuples. Refusing this
        # would be a guard that fires on a comparison it should accept.
        rows = bench.strategy_fingerprint(
            hand_table(self.COLLIDING[0], self.COLLIDING[0]))
        self.assertEqual(len(rows), 2)
        self.assertIsNone(
            bench.assert_fingerprint_binds(self.SPOT, "fifo", rows))

    def test_a_report_with_distinct_keys_is_accepted(self) -> None:
        rows = bench.strategy_fingerprint(HAND_TABLE)
        self.assertTrue(rows)
        self.assertIsNone(
            bench.assert_fingerprint_binds(("holdem-hu", "holdem", 2), "fifo",
                                           rows))

    def test_an_uniform_row_does_not_make_the_fingerprint_ambiguous(self) -> None:
        # Uniform rows never reach the fingerprint, so a uniform row sharing a
        # key with a trained one cannot make the comparison ambiguous.
        table = hand_table(
            self.COLLIDING[0],
            self.COLLIDING[1].replace("fold=25.0%,call=75.0%",
                                      "fold=50.0%,call=50.0%"))
        rows = bench.strategy_fingerprint(table)
        self.assertEqual(len(rows), 1)
        self.assertIsNone(
            bench.assert_fingerprint_binds(self.SPOT, "fifo", rows))

    def test_the_guard_is_called_from_measure(self) -> None:
        # Pinned on the call: the check is only load-bearing if a run whose
        # report cannot bind its vectors is refused before it is tabulated.
        source = Path(bench.__file__).read_text()
        self.assertIn("assert_fingerprint_binds(spot, policy, strategy)",
                      source)


class StatisticsTests(unittest.TestCase):
    """The published claim is now an interval rather than a verdict, so the
    interval is load-bearing and gets its own tests."""

    def test_the_t_quantile_is_students_not_the_normal(self) -> None:
        # At five seeds the normal quantile would understate the interval by a
        # third, which is exactly the reading the reviewer objected to.
        self.assertAlmostEqual(bench.t95(4), 2.776, places=3)
        self.assertAlmostEqual(bench.t95(1), 12.706, places=3)
        self.assertAlmostEqual(bench.t95(30), 2.042, places=3)

    def test_the_quantile_keeps_its_correction_past_the_table(self) -> None:
        # The regression: beyond df=30 the function returned the flat normal
        # 1.96. At 31 degrees of freedom the quantile is 2.0395, so a 32-seed
        # run printed an interval 4% too narrow -- and the verdict is decided by
        # whether that interval fits inside a bound, so it could flip.
        self.assertAlmostEqual(bench.t95(31), 2.0395, places=3)
        self.assertAlmostEqual(bench.t95(40), 2.0211, places=3)
        self.assertAlmostEqual(bench.t95(120), 1.9800, places=3)
        self.assertGreater(bench.t95(31), 1.96 + 0.07)
        # Still monotone in the degrees of freedom, and still converges.
        self.assertLess(bench.t95(31), bench.t95(30))
        self.assertAlmostEqual(bench.t95(10 ** 6), 1.96, places=3)

    def test_the_expansion_agrees_with_the_table_where_they_overlap(self) -> None:
        # The table is the authority for the samples in use; the expansion takes
        # over past it, so the two must not disagree in between.
        for df in range(10, len(bench.T95) + 1):
            self.assertAlmostEqual(bench.t95_expansion(df), bench.T95[df - 1],
                                   places=3)

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

    def test_one_seed_is_refused_rather_than_tabulated(self) -> None:
        # The degenerate interval above must never reach the printer. With one
        # pair the margin collapses to zero as well, so the line reads
        # "95% CI [+261.8, +261.8] ... margin 0.0 = +0.00%" -- a point estimate
        # labelled a confidence interval, judged against nothing.
        with self.assertRaises(SystemExit) as caught:
            bench.require_paired_seeds(1)
        self.assertIn("zero-width interval", str(caught.exception))

    def test_no_seeds_are_refused_too(self) -> None:
        with self.assertRaises(SystemExit):
            bench.require_paired_seeds(0)

    def test_two_seeds_are_enough_to_run(self) -> None:
        self.assertIsNone(bench.require_paired_seeds(2))
        self.assertIsNone(bench.require_paired_seeds(5))

    def test_a_wide_spread_cannot_establish_equivalence(self) -> None:
        # The point of printing the interval: four seeds and a large spread
        # leave a half-width of ~4, so an effect of 3 is not excluded even
        # though a t-test would find nothing at the 5% level.
        _, _, half = bench.paired_interval([-10.0, 5.0, 0.0, 8.0])
        self.assertGreater(half, 3.0)

    def test_the_verdict_uses_both_endpoints_not_the_half_width(self) -> None:
        # The regression: comparing the half-width alone labels an interval that
        # reaches past the bound as "inside". These are the published PLO4 and
        # three-way rows, both of which the half-width test passed wrongly.
        self.assertFalse(bench.interval_within(119.9, 255.1, 273.1))
        self.assertFalse(bench.interval_within(-221.4, 604.6, 801.5))
        # And the one that does hold, judged against its baseline's own spread.
        self.assertTrue(bench.interval_within(-92.2, 189.3, 660.9))
        self.assertFalse(bench.interval_within(-0.3, 329.6, 160.7))
        # A zero mean degenerates to the half-width test, which is correct there.
        self.assertTrue(bench.interval_within(0.0, 200.0, 250.0))
        self.assertFalse(bench.interval_within(0.0, 300.0, 250.0))

    def test_the_yardstick_is_the_baseline_arm_alone(self) -> None:
        # The regression: the bound was max(sd_fifo, sd_aware), so a policy that
        # increased run-to-run variance raised the bar it had to clear. Measured
        # on the published run, the aware arm set the margin on two of the four
        # spots -- holdem-hu 700.8 against the baseline's 660.9, plo5-hu 250.3
        # against 160.7, an inflation of 56%. Neither flipped a verdict, but a
        # spot whose effect sits between the two spreads would.
        baseline = [{"nash_conv": v} for v in (0.0, 10.0, 20.0)]   # sd 10.0
        aware = [{"nash_conv": v} for v in (0.0, 100.0, 200.0)]    # sd 100.0
        self.assertAlmostEqual(bench.sd_of(aware, "nash_conv"), 100.0)
        self.assertAlmostEqual(bench.paired_yardstick(baseline, aware), 10.0)

    def test_sd_of_is_zero_for_a_single_run(self) -> None:
        self.assertEqual(bench.sd_of([{"v": 1.0}], "v"), 0.0)
        self.assertAlmostEqual(bench.sd_of([{"v": 1.0}, {"v": 3.0}], "v"),
                               1.4142135623730951)


class SolveTimingTests(unittest.TestCase):
    """The timed quantity stops at the solve, not at the end of the report.

    The report is generated after the solve and its cost is proportional to the
    infosets the run materialized -- a count the two arms do not share, since
    the measurement resolves infosets with `create=1`. Measured on PLO5
    heads-up the report phase is 1.0-1.3 s, 44% of the invocation.
    """

    # The line the shipped CLI prints, verbatim.
    MARKER = "solver_phase=complete stop_reason=max_iterations report=starting\n"

    def test_the_reading_is_taken_at_the_marker_not_earlier(self) -> None:
        # The marker is not the first line -- the solver prints its
        # configuration before it -- so a reader keyed on "the first line" or
        # "the first line after the start" would take the reading in the wrong
        # place and still look correct. The clock records how much of the
        # stream had been pulled when it was called, which pins the position.
        pulled = []

        def lines():
            for i, line in enumerate(["preflop_solver=lane-b\n",
                                      "sampling_policy=standard\n",
                                      self.MARKER, "HAND TABLE\n",
                                      "report_phase=complete rows=30134\n"]):
                pulled.append(i)
                yield line

        reads = []

        def clock():
            reads.append(list(pulled))
            return 11.0

        text, marked = bench.drain_stream(lines(), clock)
        self.assertEqual(marked, 11.0)
        # Read once, with lines 0, 1 and 2 consumed: line 2 is the marker.
        self.assertEqual(reads, [[0, 1, 2]])
        # The whole stream is still returned: the guard and the fingerprint
        # read the report that follows the marker.
        self.assertIn("report_phase=complete", text)

    def test_lines_after_the_marker_do_not_move_the_reading(self) -> None:
        # The point of the change: with an uncapped report the tail is
        # proportional to the infosets materialized, so a reading that moved
        # with it would make the two arms' times incomparable.
        def reading(tail):
            calls = []

            def clock():
                calls.append(len(calls))
                return 42.0 + len(calls)

            marked = bench.drain_stream([self.MARKER] + ["row\n"] * tail,
                                        clock)[1]
            return marked, len(calls)

        # A one-line tail and a ten-thousand-line tail read the same value, and
        # each reads the clock exactly once.
        self.assertEqual(reading(1), (43.0, 1))
        self.assertEqual(reading(10000), (43.0, 1))

    def test_a_run_without_the_marker_reports_no_reading(self) -> None:
        # An older solver that never prints the marker must not lose the column:
        # the caller falls back to its own final reading.
        text, marked = bench.drain_stream(["no marker here\n"], lambda: 5.0)
        self.assertIsNone(marked)
        self.assertIn("no marker here", text)


class StopReasonTests(unittest.TestCase):
    """A zero exit status is not proof the solve reached the iteration cap.

    The CLI stops early on its own memory budget (70% of physical RAM, and the
    runner passes no `--max-ram`), writes its report and exits 0. It names the
    cause on the same line as the phase marker the timing is read at, and the
    timing keys on that line's *prefix* -- so reading only its presence, as the
    timing does, would publish a partial solve under a header quoting the
    requested `--iterations`.
    """

    def test_a_run_that_reached_the_cap_is_accepted(self) -> None:
        self.assertIsNone(bench.assert_completed(
            ("holdem-hu", "holdem", 2), "fifo",
            "iterations=5000 complete=1 infosets=338\n"
            "solver_phase=complete stop_reason=max_iterations "
            "report=starting\n"))

    def test_a_memory_budget_stop_is_refused(self) -> None:
        # The reachable early stop: the solve is intact and the exit is clean,
        # so the reason is the only thing that separates it from a completed
        # run. Both arms of a spot share a seed, hence the same memory
        # trajectory, hence the same earlier iteration -- which is why no
        # downstream comparison catches it.
        with self.assertRaises(SystemExit) as caught:
            bench.assert_completed(
                ("plo4-3way", "plo4", 3), "aware",
                "solver_phase=complete stop_reason=memory_budget "
                "report=starting\n")
        self.assertIn("memory-budget", str(caught.exception))

    def test_a_target_stop_is_refused(self) -> None:
        # Nothing in the runner asks for a target, so any reason other than the
        # cap means the run stopped on something the benchmark did not request.
        with self.assertRaises(SystemExit) as caught:
            bench.assert_completed(
                ("holdem-hu", "holdem", 2), "fifo",
                "solver_phase=complete stop_reason=target report=starting\n")
        self.assertIn("'target'", str(caught.exception))

    def test_a_missing_marker_is_refused(self) -> None:
        # drain_stream tolerates a missing marker for the *reading*; the guard
        # does not, because a run that does not state its stop reason cannot be
        # shown to have reached the cap.
        with self.assertRaises(SystemExit) as caught:
            bench.assert_completed(("holdem-hu", "holdem", 2), "fifo",
                                   "report_phase=complete rows=338\n")
        self.assertIn("unconditionally", str(caught.exception))

    def test_the_reason_is_read_from_the_full_marker_line(self) -> None:
        # The regex has to key on the reason, not on the prefix the timing uses:
        # `SOLVE_PHASE_COMPLETE` alone matches a completed and a truncated run
        # alike.
        for reason in ("max_iterations", "memory_budget", "target"):
            line = ("solver_phase=complete stop_reason=%s report=starting\n"
                    % reason)
            self.assertEqual(bench.SOLVE_STOP_RE.search(line).group(1), reason)

    def test_the_marker_format_matches_the_shipped_cli(self) -> None:
        # The parse is only as good as the line it reads, and the timing and the
        # guard share that line. Pin the format string against the C source so a
        # change there fails here rather than silently disarming the guard (the
        # empty-PLO fingerprint lesson).
        source = PREFLOP_SOLVE_C.read_text(encoding="utf-8")
        self.assertIn(
            '"solver_phase=complete stop_reason=%s report=starting\\n"', source)

    def test_the_accepted_reason_matches_the_shipped_solver(self) -> None:
        # The one reason the benchmark accepts has to be the solver's own name
        # for the iteration cap, not a copy that drifted.
        source = SOLVER_C.read_text(encoding="utf-8")
        self.assertIn('case PE_STOP_ITERATIONS:     return "max_iterations";',
                      source)
        self.assertIn('case PE_STOP_MEMORY_BUDGET:  return "memory_budget";',
                      source)

    def test_the_guard_is_called_from_measure(self) -> None:
        # Pinned on the call, like `assert_fingerprint_binds`: the check is only
        # load-bearing if a partial solve is refused before it is tabulated.
        source = Path(bench.__file__).read_text()
        self.assertIn("assert_completed(spot, policy, out)", source)


class ReferenceAccuracyTests(unittest.TestCase):
    """The accuracy block reports the scale, and never names a winner.

    The tool seeds its best-response RNG once from `config->seed` and consumes
    trajectories in order (`external_best_response.c:1206-1215`), so a run at N
    is a *prefix* of the same policy's run at ten times N under the same seed.
    Both arms' errors therefore carry a prefix covariance with their own stream,
    and nothing in the block shows the two covariances are comparable: an arm
    whose stream happened to be the more stable one could win without being the
    closer to the true NashConv. The reference cannot be taken at a disjoint
    seed instead either -- the seed drives the *solve*, so a disjoint seed scores
    a different strategy (measured on `holdem-hu`, 1308.7 against 101.3). So the
    block prints the distances, the spread between the two references, and how
    far apart the arms' own-policy errors are, and stops there -- the spread is
    not compared with the gap, because it is not a null distribution for it. See
    `print_reference_accuracy` for the two measured reasons.
    """

    ARMS = (("fifo@20,000", [{"nash_conv": 100.0}, {"nash_conv": 200.0}]),
            ("aware@20,000", [{"nash_conv": 120.0}, {"nash_conv": 180.0}]))
    # fifo's own-policy error 0.0; aware's |120-140|, |180-160| -> 20.0.
    # The two references are |100-140|, |200-160| -> 40.0 apart.
    REFS = (("fifo@200,000", [{"nash_conv": 100.0}, {"nash_conv": 200.0}]),
            ("aware@200,000", [{"nash_conv": 140.0}, {"nash_conv": 160.0}]))

    def render(self, arms, references) -> str:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            bench.print_reference_accuracy(("holdem-hu", "holdem", 2),
                                           arms, references)
        return buf.getvalue()

    def test_every_arm_is_scored_against_every_reference(self) -> None:
        text = self.render(self.ARMS, self.REFS)
        # One row per reference, each carrying both arms: the point of printing
        # the pair is that the reader can see whether the reference's policy
        # decides the answer.
        self.assertEqual(text.count("vs fifo@200,000"), 1)
        self.assertEqual(text.count("vs aware@200,000"), 1)
        for line in text.splitlines()[:2]:
            self.assertIn("fifo@20,000", line)
            self.assertIn("aware@20,000", line)

    def test_the_line_reports_the_mean_absolute_error_of_each_arm(self) -> None:
        # fifo: errors 0, 0 -> mean|err| 0.0. aware: +20, -20 -> 20.0, mean 0.0.
        text = self.render(self.ARMS, self.REFS[:1])
        self.assertRegex(text, r"fifo@20,000\s+0\.0 \(mean\s+\+0\.0\)")
        self.assertRegex(text, r"aware@20,000\s+20\.0 \(mean\s+\+0\.0\)")

    def test_no_winner_is_named(self) -> None:
        # Neither reference is neutral, so the block reports the scale rather
        # than a ranking it cannot support.
        self.assertNotIn("closer", self.render(self.ARMS, self.REFS))

    def test_the_gap_between_the_arms_is_printed_beside_the_spread(
            self) -> None:
        # Own-policy errors 0.0 and 20.0 differ by 20.0; the two references are
        # 40.0 apart. Both numbers are reported; neither is compared with the
        # other, and the line says so rather than leaving it to the reader.
        text = self.render(self.ARMS, self.REFS)
        self.assertIn("own-policy errors differ by 20.0", text)
        self.assertIn("refs differ by 40.0", text)
        self.assertIn("scale, not a threshold", text)

    def test_the_runner_docstring_states_why_the_two_are_not_compared(
            self) -> None:
        # The measured reason, not just the correction: the docstring carries
        # the two budgets so the claim can be re-derived rather than trusted.
        source = Path(bench.__file__).read_text()
        self.assertIn("is not a null distribution for", source)
        self.assertRegex(source,
                         r"119\.7\s+mBB at 20,000 trajectories against\s+36\.4")

    def test_no_verdict_is_printed_about_the_comparison(self) -> None:
        # The line used to read "clears the floor" or "within the floor, no
        # ranking", which read the reference spread as a threshold for the gap.
        # It is not one -- see the last test in this class.
        wide = (("fifo@20,000", [{"nash_conv": 100.0}, {"nash_conv": 200.0}]),
                ("aware@20,000", [{"nash_conv": 300.0}, {"nash_conv": 100.0}]))
        for arms in (self.ARMS, wide):
            text = self.render(arms, self.REFS)
            self.assertNotIn("clears", text)
            self.assertNotIn("no ranking", text)
            self.assertNotIn("floor", text)

    def test_the_spread_is_the_distance_between_the_two_references(
            self) -> None:
        text = self.render(self.ARMS, self.REFS)
        self.assertIn("refs differ by 40.0 (|fifo@200,000 - aware@200,000|)",
                      text)

    def test_a_lone_reference_prints_no_spread(self) -> None:
        # With one reference there is nothing to take a distance from, and a
        # spread of 0.0 would read as agreement rather than as an absence.
        text = self.render(self.ARMS, self.REFS[:1])
        self.assertNotIn("refs differ by", text)

    def test_which_number_is_larger_is_decided_by_the_refs_not_the_arms(
            self) -> None:
        # The reason the line carries no verdict. Both cases have a gap of 0.2
        # between the two arms' own-policy errors, and both arms sit about 100
        # from the truth (T = 0, never printed). Only the aware reference moves,
        # taking the spread from 0.0 to 5.0 -- and the aware arm with it, so its
        # own-policy error stays 0.3. The gap is the larger number in the first
        # case and the smaller one in the second, so a verdict would be a
        # function of the references' mutual agreement, not of the arms.
        agree_arms = (("fifo@20,000", [{"nash_conv": 100.1},
                                       {"nash_conv": 99.9}]),
                      ("aware@20,000", [{"nash_conv": 100.3},
                                        {"nash_conv": 99.7}]))
        agree_refs = (("fifo@200,000", [{"nash_conv": 100.0},
                                        {"nash_conv": 100.0}]),
                      ("aware@200,000", [{"nash_conv": 100.0},
                                         {"nash_conv": 100.0}]))
        shift_arms = (("fifo@20,000", [{"nash_conv": 100.1},
                                       {"nash_conv": 99.9}]),
                      ("aware@20,000", [{"nash_conv": 105.3},
                                        {"nash_conv": 94.7}]))
        shift_refs = (("fifo@200,000", [{"nash_conv": 100.0},
                                        {"nash_conv": 100.0}]),
                      ("aware@200,000", [{"nash_conv": 105.0},
                                         {"nash_conv": 95.0}]))
        first = self.render(agree_arms, agree_refs)
        second = self.render(shift_arms, shift_refs)
        self.assertIn("own-policy errors differ by 0.2", first)
        self.assertIn("own-policy errors differ by 0.2", second)
        self.assertIn("refs differ by 0.0", first)
        self.assertIn("refs differ by 5.0", second)

    def test_the_reference_shares_the_arm_seed(self) -> None:
        # A disjoint seed would score a different strategy: the seed drives the
        # solve, so it changes the hand table, not just the sampling error.
        source = (Path(bench.__file__)).read_text()
        self.assertNotIn("REFERENCE_SEED_OFFSET", source)
        self.assertIn("the seed drives the *solve*", source)


class ReferenceBudgetTests(unittest.TestCase):
    """The reference has to be a higher budget than the arm it judges.

    Nothing checked it, so `--reference-samples 1000` against the default
    `--br-samples 20000` was accepted and printed in exactly the format a real
    reference uses -- measured on `holdem-hu`, errors of 871.6 and 732.3 against
    a 1,000-trajectory "reference" whose own two policies disagree by 417.4.

    It also has to be one the *solver* will accept: the budget is spent through
    `--br-samples`, a uint32 there, so a value above `UINT32_MAX` passes the
    ordering check and then dies on the first reference invocation -- after the
    main runs have already been paid for.
    """

    def assertRefused(self, br_samples, reference_samples) -> None:
        with self.assertRaises(SystemExit) as caught:
            bench.require_higher_reference(br_samples, reference_samples)
        self.assertIn("--reference-samples", str(caught.exception))

    def test_a_reference_below_the_arm_budget_is_refused(self) -> None:
        self.assertRefused(20000, 1000)

    def test_a_reference_equal_to_the_arm_budget_is_refused(self) -> None:
        # Equal is not higher: the arm would be scored against an estimate no
        # more precise than itself.
        self.assertRefused(20000, 20000)

    def test_a_reference_above_the_arm_budget_is_accepted(self) -> None:
        bench.require_higher_reference(20000, 200000)
        bench.require_higher_reference(2000, 2001)

    def test_a_reference_above_the_solver_limit_is_refused(self) -> None:
        # The ordering check cannot see it: an oversized value trivially exceeds
        # --br-samples, so it passed every guard and died on the *first*
        # reference invocation -- after the main FIFO and aware runs had been
        # paid for, and without a reference block.
        self.assertRefused(100, bench.BR_SAMPLES_MAX + 1)
        self.assertRefused(20000, 4294967296)

    def test_the_solver_limit_itself_is_accepted(self) -> None:
        # The boundary, so the guard cannot pass by refusing everything. Nothing
        # here runs a solve; the guard is what is under test.
        bench.require_higher_reference(20000, bench.BR_SAMPLES_MAX)
        bench.require_higher_reference(bench.BR_SAMPLES_MAX - 1,
                                       bench.BR_SAMPLES_MAX)

    def test_the_solver_limit_refusal_wins_over_the_ordering_message(self) -> None:
        # Both branches apply only when the reference *and* the arm budget are
        # above the limit: then the ordering check would read "higher" and hide
        # the real reason. The solver-limit branch has to come first.
        with self.assertRaises(SystemExit) as caught:
            bench.require_higher_reference(bench.BR_SAMPLES_MAX + 1,
                                           bench.BR_SAMPLES_MAX + 1)
        message = str(caught.exception)
        self.assertIn(str(bench.BR_SAMPLES_MAX), message)
        self.assertIn("cannot start", message)
        self.assertNotIn("higher", message)

    def test_the_solver_limit_matches_the_solver(self) -> None:
        # A uint32 ceiling enforced in C, so the copy is pinned to its source: if
        # the solver ever widened it, the runner would start refusing budgets the
        # solver would take.
        source = PREFLOP_SOLVE_C.read_text(encoding="utf-8")
        self.assertIn("options->br_samples > UINT32_MAX", source)
        self.assertEqual(bench.BR_SAMPLES_MAX, (1 << 32) - 1)

    def test_a_negative_reference_is_refused(self) -> None:
        # Both call sites guarded on `> 0`, so a negative value made the validity
        # check *and* the execution guard false: the block was skipped in
        # silence and the run exited 0. Measured with `--reference-samples -1`:
        # no reference block printed, command reported success.
        self.assertRefused(20000, -1)
        self.assertRefused(20000, -100)

    def test_the_refusal_says_zero_skips_and_negatives_do_not(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            bench.require_higher_reference(20000, -1)
        message = str(caught.exception)
        self.assertIn("negative", message)
        self.assertIn("0 skips", message)

    def test_the_reference_guard_is_called_for_every_non_zero_value(self) -> None:
        # Pinned on `!= 0`: guarding on `> 0` is what let a negative budget
        # through both this call and the execution guard below.
        source = Path(bench.__file__).read_text()
        self.assertIn("if args.reference_samples != 0:", source)


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

    def test_a_capped_report_is_refused(self) -> None:
        # The cap is a per-node quota filled in storage-id order, so a node over
        # quota loses the trained rows that come last -- the fingerprint would
        # then cover part of the strategy and still compare equal. Measured:
        # the CLI's default 2,000-row cap emits 1,998 of 11,167 trained
        # decisions on PLO4 heads-up at 50,000 iterations.
        with self.assertRaises(SystemExit) as caught:
            bench.assert_report_uncapped(
                ("plo4-hu", "plo4", 2), "fifo",
                "... report capped at 2000 visible rows (--report-rows); the "
                "solve storage still contains all 30134 infosets.")
        self.assertIn("was capped", str(caught.exception))

    def test_an_uncapped_report_is_accepted(self) -> None:
        # The marker is the tool's own statement that its budget ran out, and
        # the quota loop can only truncate once the budget is exhausted, so its
        # absence is the proof that nothing was dropped.
        self.assertIsNone(bench.assert_report_uncapped(
            ("plo4-hu", "plo4", 2), "fifo",
            "report_phase=complete rows=30134\n"))


class PublishedBoundTests(unittest.TestCase):
    """The figure the guide states on the reported number, and where it comes
    from. Quoting the interval's half-width understates it, because the paired
    mean is nonzero and the interval is off-centre -- and the largest of four
    individual endpoints is a descriptive maximum, not a simultaneous bound."""

    # The four published rows: spot, paired mean, half-width, reference.
    PUBLISHED = (("holdem-hu", -92.2, 189.3, 12459.8),
                 ("plo4-hu", 119.9, 255.1, 20861.9),
                 ("plo5-hu", -0.3, 329.6, 19667.0),
                 ("plo4-3way", -221.4, 604.6, 51339.2))

    def test_the_bound_is_the_farthest_endpoint_not_the_half_width(self) -> None:
        bounds = [100.0 * max(abs(mean - half), abs(mean + half)) / reference
                  for _, mean, half, reference in self.PUBLISHED]
        self.assertAlmostEqual(bounds[0], 2.26, places=2)
        self.assertAlmostEqual(bounds[1], 1.80, places=2)
        self.assertAlmostEqual(bounds[2], 1.68, places=2)
        self.assertAlmostEqual(bounds[3], 1.61, places=2)
        # The half-widths would have said 1.2-1.7%, understating the first by a
        # third; the maximum over the four is what the guide states -- as a
        # descriptive maximum, not as a simultaneous bound over the four spots.
        half_widths = [100.0 * half / reference
                       for _, _, half, reference in self.PUBLISHED]
        self.assertAlmostEqual(half_widths[0], 1.52, places=2)
        self.assertLess(max(half_widths), max(bounds))

    def test_the_guide_states_the_corrected_bound(self) -> None:
        # Pinned on the bound as stated, not on the bare figures: the guide
        # still quotes the half-widths, correctly, as the thing the bound is
        # *not* taken from. The old wording -- "at most 2.3% of the reported
        # value" -- read as a bound on the true effect; the largest of four
        # individual 95% endpoints is a descriptive maximum with no joint
        # coverage, so the guide says which of the two it states.
        text = GUIDE.read_text(encoding="utf-8")
        self.assertNotIn("at most 2.3% of the", text)
        self.assertNotIn("at most 1.2-1.7%", text)
        self.assertIn("farthest of the four individual", text)
        self.assertIn("not a simultaneous 95% bound", text)

    def test_the_guide_states_the_yardstick_is_the_baseline_alone(self) -> None:
        # The regression: the bound was max(sd_fifo, sd_aware), so a policy that
        # increased run-to-run variance raised its own bar. The guide called it
        # "the larger of the two arms' spreads" and read the result as an
        # equivalence test rather than as a yardstick.
        text = GUIDE.read_text(encoding="utf-8")
        self.assertIn("baseline arm's own", text)
        self.assertNotIn("the larger of the two arms", text)
        self.assertIn("yardstick, not a pre-specified equivalence margin", text)

    def test_the_savings_range_covers_the_smallest_saving(self) -> None:
        # The four workloads save 14.4/14.6/2.7/20.9%, so a range that starts
        # at 14.4% omits plo5-hu and overstates the cheapest case fivefold.
        # Asserted on the range alone, not on the words around it: pinning the
        # sentence would make the test fail on a rewrap that changes nothing.
        text = GUIDE.read_text(encoding="utf-8")
        self.assertIn("2.7-20.9%", text)
        self.assertNotIn("14.4-20.9%", text)


class ReferenceTableTests(unittest.TestCase):
    """The guide's accuracy tables must not name a winner.

    They originally scored both arms against FIFO at ten times the budget. That
    reference shares the seed -- and it has to, because the seed drives the
    solve -- but the tool then seeds its best-response RNG once and consumes
    trajectories in order, so a run at N is a *prefix* of the same policy's run
    at ten times N. The old table put FIFO's *symmetric* error beside the aware
    arm's *crossed* one, and on `plo4-3way` that decided the answer. Worse, even
    the symmetric reading is not neutral -- both arms carry a prefix covariance
    with their own stream -- so no reading can rank the arms at all, and the
    guide now reports the distances, the gap and the reference spread instead.
    The last two are stated side by side and not compared: the spread is taken
    at ten times the arm's budget, where the estimator has converged further,
    and a spread between two estimates is not a difference between two mean
    absolute errors. See `print_reference_accuracy`.
    """

    # spot, fifo@20k vs FIFO@200k and aware@20k vs aware@200k (symmetric),
    #       fifo@20k vs aware@200k and aware@20k vs FIFO@200k (crossed), and the
    #       spread between the two references -- which is not a floor.
    PUBLISHED = (("holdem-hu", 80.4, 136.7, 78.4, 137.1, 36.4),
                 ("plo4-hu", 253.5, 187.2, 234.0, 183.1, 93.6),
                 ("plo5-hu", 178.9, 193.5, 154.7, 272.0, 90.4),
                 ("plo4-3way", 492.7, 615.4, 836.8, 272.6, 613.5))

    # The policy spread at the two budgets, measured on `holdem-hu` at the
    # published regime (5,000 iterations, 20,000 / 200,000 trajectories, five
    # seeds) by a harness that drives `measure` directly -- the runner prints
    # neither number. The gap (56.3) comes from the 20,000-trajectory runs and
    # sits *between* the two, so which side of the spread it falls on is decided
    # by the budget the spread is taken at.
    SPREAD_AT_ARM_BUDGET = 119.7
    SPREAD_AT_REF_BUDGET = 36.4

    def test_the_gap_is_smaller_than_the_spread_at_its_own_budget(self) -> None:
        # The reason the spread is not a threshold for the gap: it is taken ten
        # times further along, where the estimator has converged, so it
        # understates the spread at the budget the gap is derived from.
        self.assertLess(self.SPREAD_AT_REF_BUDGET, 56.3)
        self.assertGreater(self.SPREAD_AT_ARM_BUDGET, 56.3)

    def test_the_measured_holdem_hu_row_reproduces_the_published_one(
            self) -> None:
        # The harness that measured the two spreads above reproduced this row
        # exactly, which is what makes the comparison between them meaningful.
        _, fifo_sym, aware_sym, _, _, spread = self.PUBLISHED[0]
        self.assertAlmostEqual(fifo_sym, 80.4, places=1)
        self.assertAlmostEqual(aware_sym, 136.7, places=1)
        self.assertAlmostEqual(abs(fifo_sym - aware_sym), 56.3, places=1)
        self.assertAlmostEqual(spread, self.SPREAD_AT_REF_BUDGET, places=1)

    def test_the_spot_a_ranking_would_have_named_is_inside_its_spread(
            self) -> None:
        # plo4-hu: a gap of 66.3 against a spread of 93.6. The old guide named
        # the aware arm there, and the withdrawn claim is pinned below. The
        # spread is not the rebuttal -- it is not a threshold -- so the guide
        # rests on the non-neutral reference instead.
        _, fifo_sym, aware_sym, _, _, spread = self.PUBLISHED[1]
        self.assertAlmostEqual(abs(fifo_sym - aware_sym), 66.3, places=1)
        self.assertLess(abs(fifo_sym - aware_sym), spread)

    def test_the_published_plo4_3way_row_mixed_the_two_readings(self) -> None:
        # It printed FIFO's symmetric error (492.7) beside the aware arm's
        # crossed one (272.6) -- a 220 mBB margin for the aware arm that neither
        # reading supports: symmetric gives FIFO a 123 mBB margin instead.
        _, fifo_sym, aware_sym, _, aware_cross, _ = self.PUBLISHED[3]
        self.assertAlmostEqual(fifo_sym - aware_cross, 220.1, places=1)
        self.assertAlmostEqual(aware_sym - fifo_sym, 122.7, places=1)

    def test_the_guide_names_no_winner(self) -> None:
        text = GUIDE.read_text(encoding="utf-8")
        start = text.index("The interval bounds the *effect*")
        section = text[start:text.index("Criterion (1)")]
        # The verdict column and the arrow the runner used to print are gone.
        # ("closer" itself stays: the prose still says an arm is not the closer
        # to the truth, which is the opposite of naming a winner.)
        self.assertNotIn("| closer |", section)
        self.assertNotIn("-> ", section)
        self.assertIn("names no winner", section)
        self.assertIn("own-policy gap", section)
        self.assertIn("refs differ by", section)
        # The comparison that read the spread as a threshold for the gap is
        # gone, from the table and from the prose.
        self.assertNotIn("gap vs floor", section)
        self.assertNotIn("clears the floor", text)
        self.assertNotIn("within the floor", text)
        # The withdrawn claims, and the framing that carried them, are gone.
        self.assertNotIn("overestimates the reference by 223.3", text)
        self.assertNotIn("the more accurate estimator on `plo4-hu` alone", text)
        self.assertNotIn("the one spot where the aware arm is the more accurate",
                         text)

    def test_the_guide_prints_the_gaps_and_the_spreads(self) -> None:
        text = GUIDE.read_text(encoding="utf-8")
        for value in ("56.3", "66.3", "14.6", "122.7", "36.4", "93.6", "90.4",
                      "613.5", "119.7"):
            self.assertIn(value, text)

    def test_the_guide_states_why_a_disjoint_seed_is_not_the_fix(self) -> None:
        # The obvious remedy -- an independent reference seed -- does not work
        # here, and the guide records the measurement rather than the guess.
        text = GUIDE.read_text(encoding="utf-8")
        self.assertIn("the seed drives the *solve*", text)
        self.assertIn("1308.7", text)

    def test_the_guide_withdraws_the_uniform_accuracy_conclusion(self) -> None:
        # The guide used to read the mixed errors as "no *uniform* accuracy
        # loss". That is a claim about the same ordering the block refuses to
        # read: if the two prefix covariances are not comparable, the order that
        # makes the errors mixed is unreadable too, and a loss on *every*
        # workload is not excluded. The conclusion is withdrawn, not reworded.
        text = GUIDE.read_text(encoding="utf-8")
        self.assertNotIn("no *uniform* accuracy loss", text)
        self.assertIn("not the absence of a uniform loss", text)
        self.assertIn("an accuracy loss on *every* workload is not", text)


class CriterionConclusionTests(unittest.TestCase):
    """The guide's two verdicts must rest on the evidence they name.

    Issue #258 asks the feature to show *either* faster convergence to the same
    quality target *or* better quality for the same compute budget. The cost
    half is measured; the quality half is not, on any spot. The guide used to
    imply the opposite for `plo4-3way`, where the cap curve crosses the baseline
    at 4x (and `plo5-hu` at 2x): that crossing is a statement about terminal
    evaluations, and the curve carries no NashConv at a cap tuned to the same
    budget, so it cannot show that spending the saved work improves the answer
    -- and the interval table already bounds these spots' reported movement at
    1.61% and 1.68% with no effect detected.
    """

    def test_the_guide_does_not_rank_quality_on_plo4_3way(self) -> None:
        text = GUIDE.read_text(encoding="utf-8")
        self.assertNotIn("the saving *is* reinvestable", text)
        self.assertNotIn("not** delivered\non the three heads-up spots", text)
        self.assertIn("established on any of the four spots", text)

    def test_the_cap_curve_is_reported_as_a_cost_fact(self) -> None:
        # Crossing the baseline shows the saved evaluations are reallocatable
        # there; it does not show the answer improves. The guide has to say
        # which of the two it measured.
        text = GUIDE.read_text(encoding="utf-8")
        self.assertIn("reports no NashConv at a cap tuned to the same budget",
                      text)
        self.assertIn("a quality comparison at a matched budget", text)
        self.assertIn("this benchmark does not\nrun one", text)

    def test_criterion_one_is_stated_as_unestablished(self) -> None:
        # The verdict once read "Criterion (1) is therefore supported", then
        # "is supported by the interval table above". Neither holds: the feature
        # changes the *measurement*, not convergence -- the guide's own Scope
        # section says the training is untouched -- so nothing here shows a
        # solver converging faster, and the interval bounds the reported value's
        # shift rather than showing either policy reached a quality target.
        text = GUIDE.read_text(encoding="utf-8")
        self.assertNotIn("Criterion (1) is therefore supported", text)
        self.assertNotIn("is supported by\nthe interval table above", text)
        self.assertIn("Criterion (1) — faster convergence to the same quality "
                      "target — is **not**\nestablished", text)
        self.assertIn("shows neither policy reaching a quality", text)
        self.assertIn("no solver converging faster", text)
        self.assertIn("cost half of the issue and nothing more", text)

    def test_the_runner_states_the_two_verdicts(self) -> None:
        # The runner's docstring quotes the issue's ask, so it has to say that
        # neither demonstration follows from what it measures.
        source = Path(bench.__file__).read_text()
        self.assertIn("Neither of the issue's\ntwo demonstrations follows "
                      "from it", source)


class CapCurveSaturationTests(unittest.TestCase):
    """How far the cap curve climbs is per workload, and the docs must say so.

    The runner's docstring claimed the curve saturates and that the saving
    "cannot be spent back on a larger cap". The published curve contradicts
    that: only `holdem-hu` saturates (+2.6% over 16x), while `plo5-hu` crosses
    the FIFO baseline at 2x and `plo4-3way` at 4x, so on those two the cap does
    bind and the saved evaluations are reallocatable. Both curves were
    reproduced byte-identically against the shipped binary.
    """

    # The published cap curve: 1x..16x terminal evaluations, and FIFO at 1x.
    PUBLISHED = (("holdem-hu", (383006, 385646, 386910, 389690, 392856),
                  452678),
                 ("plo4-hu", (541937, 560369, 584595, 599083, 619203), 633843),
                 ("plo5-hu", (626925, 655613, 698269, 727981, 754677), 643357),
                 ("plo4-3way", (1242466, 1466196, 1774180, 2197318, 2647162),
                  1573060))

    def test_only_holdem_hu_saturates(self) -> None:
        growth = {spot: 100.0 * (vals[-1] - vals[0]) / vals[0]
                  for spot, vals, _ in self.PUBLISHED}
        self.assertAlmostEqual(growth["holdem-hu"], 2.57, places=1)
        self.assertGreater(growth["plo4-hu"], 14.0)
        self.assertGreater(growth["plo5-hu"], 20.0)
        self.assertGreater(growth["plo4-3way"], 100.0)

    def test_two_spots_cross_the_baseline(self) -> None:
        multiples = (1, 2, 4, 8, 16)
        crosses = {spot: next((multiples[i] for i, value in enumerate(vals)
                               if value > fifo), None)
                   for spot, vals, fifo in self.PUBLISHED}
        self.assertEqual(crosses["plo5-hu"], 2)
        self.assertEqual(crosses["plo4-3way"], 4)
        self.assertIsNone(crosses["holdem-hu"])
        self.assertIsNone(crosses["plo4-hu"])

    def test_the_docs_no_longer_claim_universal_saturation(self) -> None:
        source = Path(bench.__file__).read_text()
        self.assertNotIn("It saturates:", source)
        self.assertNotIn("the saving cannot be spent back", source)
        text = GUIDE.read_text(encoding="utf-8")
        self.assertIn("Only that spot saturates", text)
        self.assertIn("`plo5-hu` at 2x", text)


class RevisitBudgetTests(unittest.TestCase):
    """The revisit curve must never ask the solver for a zero budget.

    Integer division produced one: `--br-samples 5` gives a tenth of zero, and
    the solver refuses `--br-samples 0` (`tools/pe_preflop_solve.c:1250-1253`).
    The curve is drawn *after* the spot's main measurements, so the abort landed
    once the expensive work was done -- reproduced with `--br-samples 5`, which
    printed its four rows and then died on `solve failed: ... --br-samples 0`.
    """

    def test_the_nominal_budget_keeps_the_published_three_points(self) -> None:
        self.assertEqual(bench.revisit_budgets(20000), [2000, 5000, 20000])

    def test_a_tenth_that_rounds_to_zero_is_dropped(self) -> None:
        # 5 // 10 == 0, which the solver refuses; 5 // 4 == 1 is kept.
        self.assertEqual(bench.revisit_budgets(5), [1, 5])

    def test_every_budget_is_positive_across_small_inputs(self) -> None:
        for br_samples in range(1, 40):
            budgets = bench.revisit_budgets(br_samples)
            self.assertTrue(budgets, br_samples)
            self.assertTrue(all(b > 0 for b in budgets), (br_samples, budgets))
            # The full budget is always drawn, so the curve always reaches the
            # published configuration.
            self.assertIn(br_samples, budgets)

    def test_duplicate_budgets_are_collapsed(self) -> None:
        # 1 // 10 and 1 // 4 are both zero, so only the full budget survives.
        self.assertEqual(bench.revisit_budgets(1), [1])
        self.assertEqual(bench.revisit_budgets(3), [3])

    def test_the_curve_is_built_from_the_helper(self) -> None:
        # Pinned on the call, not on the helper alone: an inline tuple could
        # come back and the helper would still pass its own tests.
        source = Path(bench.__file__).read_text()
        self.assertIn("for budget in revisit_budgets(args.br_samples):", source)


class WorkflowTriggerTests(unittest.TestCase):
    """The workflow running these tests must trigger on what they read.

    The assertions above read the guide, so a PR that changes only the guide has
    to run them; otherwise the guide and the benchmark evidence drift apart with
    no signal at all.
    """

    WORKFLOW = ROOT / ".github" / "workflows" / "solver-benchmark-smoke.yml"

    def test_the_workflow_triggers_on_the_path_the_tests_read(self) -> None:
        # Derived from GUIDE rather than named again, so moving the guide cannot
        # leave the filter pointing at the old path.
        text = self.WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("'%s'" % GUIDE.relative_to(ROOT).as_posix(), text)

    def test_the_workflow_still_runs_the_work_priority_tests(self) -> None:
        text = self.WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("unittest discover -s scripts/benchmarks", text)


class SpotSelectorTests(unittest.TestCase):
    """`--spot` must refuse a name that is not a spot, not drop it in silence.

    The selection is `[s for s in SPOTS if s[0] in selected]`, so an unknown
    name vanished without a word. Measured before the guard existed:
    `--spot bogus` printed the header and exited **0**, and `--spot bogus
    --spot holdem-hu` published `holdem-hu` alone. A benchmark whose whole
    output is a table cannot let a typo look like a deliberately short run.
    """

    def assertRefused(self, selected, needle) -> None:
        with self.assertRaises(SystemExit) as caught:
            bench.require_known_spots(selected)
        self.assertIn(needle, str(caught.exception))

    def test_an_unknown_spot_is_refused(self) -> None:
        self.assertRefused(["bogus"], "--spot bogus")

    def test_the_refusal_names_the_spots_that_exist(self) -> None:
        # A refusal that does not say what it accepts sends the reader back to
        # the source; list every spot, derived from SPOTS rather than restated.
        with self.assertRaises(SystemExit) as caught:
            bench.require_known_spots(["bogus"])
        message = str(caught.exception)
        for name, _, _ in bench.SPOTS:
            self.assertIn(name, message)

    def test_a_mixed_selection_is_refused_rather_than_trimmed(self) -> None:
        # The partial-benchmark case: one good name must not buy silence for a
        # bad one.
        self.assertRefused(["holdem-hu", "bogus"], "bogus")

    def test_every_unknown_name_is_reported_not_just_the_first(self) -> None:
        self.assertRefused(["bogus", "holdem-hu", "nope"], "bogus, nope")

    def test_every_published_spot_is_accepted(self) -> None:
        # The guard must not reject a workload it exists to allow, and the
        # selection must keep exactly the name that was asked for. Counted per
        # spot rather than trusted: an empty selection would pass a loop that
        # only asserted the guard returns.
        for name, _, _ in bench.SPOTS:
            self.assertIsNone(bench.require_known_spots([name]))
            kept = [s for s in bench.SPOTS if s[0] in [name]]
            self.assertEqual([s[0] for s in kept], [name])

    def test_no_selection_is_accepted(self) -> None:
        # No `--spot` means every spot, which is not an unknown name.
        self.assertIsNone(bench.require_known_spots(None))
        self.assertIsNone(bench.require_known_spots([]))

    def test_the_guard_is_called_from_main(self) -> None:
        # Pinned on the call site: the helper alone would pass every test above
        # and the run would still drop unknown names.
        source = Path(bench.__file__).read_text()
        self.assertIn("require_known_spots(args.spot)", source)


class CapGuardTests(unittest.TestCase):
    """The sampling caps have to be ones the benchmark can actually run with.

    Three ways to be unrunnable, all once discovered only when the solver
    refused mid-sweep. The curve scales `--br-max-samples` by a fixed ladder and
    the solver caps sampling at `BR_MAX_SAMPLES_CEILING` (`1u << 20`); measured
    before the fix, `--br-max-samples 100000` printed the main rows and the
    revisit curve, then the 16x point asked for 1,600,000 and the run died on
    `preflop solve failed: status=5`, losing the cap curve and every later spot.
    At `0` the solver accepts the cap -- it means "sampling off" -- but the
    report then carries no `br_sampling`/`br_decisions` line, so the parser
    rejects results the benchmark cannot exist without. And below the resolved
    `--br-min-samples` the ordering check fails, which is why the guard takes the
    pair rather than the maximum alone.
    """

    def test_the_ceiling_matches_the_solver(self) -> None:
        # The runner copies a C constant, so the copy is pinned to its source: if
        # `BR_MAX_SAMPLES_CEILING` moves, the curve would start asking for caps
        # the solver rejects and nothing else in this file would notice.
        source = BR_SAMPLING_C.read_text(encoding="utf-8")
        found = re.search(
            r"#define\s+BR_MAX_SAMPLES_CEILING\s+\(1u\s*<<\s*(\d+)\)", source)
        self.assertIsNotNone(found, "BR_MAX_SAMPLES_CEILING not found")
        self.assertEqual(bench.BR_MAX_SAMPLES_CEILING, 1 << int(found.group(1)))

    def test_the_default_cap_keeps_the_published_five_points(self) -> None:
        self.assertEqual(bench.cap_curve_multiples(64), (1, 2, 4, 8, 16))

    def test_a_multiplier_that_exceeds_the_ceiling_is_dropped(self) -> None:
        # The reproduced case: 100000 * 16 = 1,600,000, over the ceiling; 8x is
        # 800,000 and stays.
        self.assertEqual(bench.cap_curve_multiples(100000), (1, 2, 4, 8))

    def test_the_largest_accepted_cap_keeps_only_the_first_point(self) -> None:
        self.assertEqual(bench.cap_curve_multiples(bench.BR_MAX_SAMPLES_CEILING),
                         (1,))

    def test_no_point_ever_exceeds_the_ceiling(self) -> None:
        # Every cap the guard accepts, plus the ones just above it, so the curve
        # cannot ask for a rejected cap for any input the guard lets through.
        for cap in list(range(1, 4096)) + [
                65535, 65536, 65537, 100000, 1 << 20]:
            for multiple in bench.cap_curve_multiples(cap):
                self.assertLessEqual(cap * multiple,
                                     bench.BR_MAX_SAMPLES_CEILING,
                                     (cap, multiple))

    def test_the_first_point_is_always_kept(self) -> None:
        # A curve with no points prints an empty diagnostic; the 1x point is
        # always runnable, since the guard refuses a cap above the ceiling.
        for cap in range(1, 4096):
            self.assertIn(1, bench.cap_curve_multiples(cap))

    def test_the_curve_is_built_from_the_helper(self) -> None:
        # Pinned on the call: an inline tuple could come back and every test
        # above would still pass.
        source = Path(bench.__file__).read_text()
        self.assertIn("for multiple in cap_curve_multiples(cap):", source)

    # Measured against the shipped solver with `--spot holdem-hu --iterations
    # 500 --samples 4 --br-samples 100`: does the run complete, or die on
    # `preflop solve failed: status=5`? Replayed here so the guard's model of the
    # solver stays tied to the measurement rather than to a reading of it.
    MEASURED = [
        (2, 1, False), (4, 1, False), (1, 1, False),
        (2, 2, True), (4, 2, False), (1, 2, True), (0, 2, False),
        (2, 3, True), (4, 3, False), (0, 3, False),
        # min 3 is the one the resolution model was documented wrong about: it
        # is *not* floored to 2, so cap 3 runs and cap 2 does not.
        (3, 3, True), (3, 2, False),
        (2, 4, True), (4, 4, True), (0, 4, True),
        (0, 5, True),
    ]

    def test_the_guard_replays_the_measured_matrix(self) -> None:
        # Both verdicts have to appear, or the table proves nothing: a list of
        # only-failing cases is passed by a guard that refuses everything.
        self.assertTrue(any(runs for _, _, runs in self.MEASURED))
        self.assertTrue(any(not runs for _, _, runs in self.MEASURED))
        for br_min, br_max, runs in self.MEASURED:
            if runs:
                self.assertIsNone(
                    bench.require_supported_caps(br_min, br_max),
                    (br_min, br_max))
            else:
                with self.assertRaises(SystemExit, msg=(br_min, br_max)):
                    bench.require_supported_caps(br_min, br_max)

    def test_the_min_constants_match_the_solver(self) -> None:
        # Copied from C, so pinned to it: `BR_DEFAULT_MIN` replaces an unset
        # minimum and the floor lifts anything below it, both before the
        # ordering check.
        source = BR_SAMPLING_C.read_text(encoding="utf-8")
        default = re.search(r"#define\s+BR_DEFAULT_MIN\s+(\d+)u", source)
        self.assertIsNotNone(default, "BR_DEFAULT_MIN not found")
        self.assertEqual(bench.BR_MIN_SAMPLES_DEFAULT, int(default.group(1)))
        floor = re.search(
            r"if \(r\.min_samples < (\d+)u\)\s*r\.min_samples = (\d+)u;",
            source)
        self.assertIsNotNone(floor, "the min_samples floor was not found")
        self.assertEqual(floor.group(1), floor.group(2))
        self.assertEqual(bench.BR_MIN_SAMPLES_FLOOR, int(floor.group(1)))

    def test_the_effective_minimum_resolves_zero_to_the_solver_default(self) -> None:
        self.assertEqual(bench.effective_min_samples(0),
                         bench.BR_MIN_SAMPLES_DEFAULT)
        self.assertEqual(bench.effective_min_samples(1),
                         bench.BR_MIN_SAMPLES_FLOOR)
        # 3 is above the floor and is *not* lifted: measured, cap 3 completes
        # and cap 2 dies, so the resolved minimum is 3. The docstring used to
        # say 2 for 3 as well, and nothing measured it.
        self.assertEqual(bench.effective_min_samples(3), 3)
        self.assertEqual(bench.effective_min_samples(4), 4)
        self.assertEqual(bench.effective_min_samples(64), 64)

    def test_one_is_never_usable_because_the_floor_lifts_min_to_two(self) -> None:
        # "Require the cap to be at least 1" is not enough: the solver lifts a
        # minimum of 1 up to 2 and then requires min <= max, so cap 1 fails even
        # with `--br-min-samples 1`. Measured: exit 1 for (1, 1) and (2, 1).
        self.assertEqual(bench.effective_min_samples(1), 2)
        with self.assertRaises(SystemExit):
            bench.require_supported_caps(1, 1)

    def test_a_cap_below_the_resolved_minimum_is_refused(self) -> None:
        # The shipped default is --br-min-samples 4, so cap 3 cannot run.
        with self.assertRaises(SystemExit) as caught:
            bench.require_supported_caps(4, 3)
        message = str(caught.exception)
        self.assertIn("--br-max-samples 3", message)
        self.assertIn("min_samples <= max_samples", message)

    def test_the_resolved_minimum_itself_is_accepted(self) -> None:
        self.assertIsNone(bench.require_supported_caps(4, 4))
        self.assertIsNone(bench.require_supported_caps(2, 2))
        self.assertIsNone(bench.require_supported_caps(0, 4))

    def test_a_cap_above_the_ceiling_is_refused(self) -> None:
        # This one cannot be degraded to something runnable: the main
        # measurements use it directly.
        with self.assertRaises(SystemExit) as caught:
            bench.require_supported_caps(4, bench.BR_MAX_SAMPLES_CEILING + 1)
        self.assertIn("--br-max-samples", str(caught.exception))
        self.assertIn(str(bench.BR_MAX_SAMPLES_CEILING), str(caught.exception))

    def test_the_ceiling_itself_is_accepted(self) -> None:
        self.assertIsNone(
            bench.require_supported_caps(4, bench.BR_MAX_SAMPLES_CEILING))
        self.assertIsNone(bench.require_supported_caps(4, 64))

    def test_a_zero_cap_is_refused(self) -> None:
        # The solver *accepts* 0 -- it means "sampling off" -- but then emits no
        # br_sampling/br_decisions line, so the benchmark cannot read its
        # metrics. Measured: one solve ran, then `no br_sampling/br_decisions
        # line in: ...`, exit 1.
        with self.assertRaises(SystemExit) as caught:
            bench.require_supported_caps(4, 0)
        message = str(caught.exception)
        self.assertIn("--br-max-samples 0", message)
        self.assertIn("at least 1", message)

    def test_a_negative_cap_is_refused(self) -> None:
        with self.assertRaises(SystemExit):
            bench.require_supported_caps(4, -1)

    def test_a_negative_minimum_is_refused(self) -> None:
        # A negative minimum is not a small minimum: the solver's option parser
        # refuses the token before `pe_br_sampling_resolve` ever floors it, so
        # no cap makes the pair runnable. Measured with
        # `--br-min-samples -1 --br-max-samples 64`: `missing value for
        # --br-min-samples` and a usage dump. The guard used to accept it and
        # the failure surfaced at the first measurement instead.
        for cap in (2, 64, bench.BR_MAX_SAMPLES_CEILING):
            with self.assertRaises(SystemExit) as caught:
                bench.require_supported_caps(-1, cap)
            self.assertIn("--br-min-samples -1", str(caught.exception))

    def test_the_negative_minimum_is_refused_even_where_the_cap_would_pass(
            self) -> None:
        # Pins the branch, not the cap: 64 is a cap the guard accepts with a
        # sane minimum, so a refusal here can only come from the minimum.
        self.assertIsNone(bench.require_supported_caps(4, 64))
        with self.assertRaises(SystemExit):
            bench.require_supported_caps(-1, 64)

    def test_the_cap_branch_wins_when_both_arguments_are_invalid(self) -> None:
        # Both refusal branches apply to (-1, 0): the cap is 0 *and* the minimum
        # is negative. The cap branch is checked first, so its message is the
        # one printed. Only a fixture where both apply can see the order -- with
        # any other cap the negative-minimum branch is the only one that fires,
        # and swapping the two would go unnoticed.
        with self.assertRaises(SystemExit) as caught:
            bench.require_supported_caps(-1, 0)
        message = str(caught.exception)
        self.assertIn("--br-max-samples 0", message)
        self.assertNotIn("--br-min-samples -1", message)

    def test_the_cap_guard_is_called_from_main(self) -> None:
        # Both caps, not just the maximum: the solver validates the pair.
        source = Path(bench.__file__).read_text()
        self.assertIn(
            "require_supported_caps(args.br_min_samples, args.br_max_samples)",
            source)


class ConfigSummaryTests(unittest.TestCase):
    """The header is the only configuration summary attached to the output."""

    @staticmethod
    def args(br_min_samples: int):
        return SimpleNamespace(iterations=5000, samples=4, br_samples=20000,
                               br_min_samples=br_min_samples, seeds=5)

    def test_the_summary_prints_the_resolved_minimum_not_the_raw_one(self) -> None:
        # Measured against the shipped solver: 0 runs at a minimum of 4, 1 at 2,
        # 3 at 3. A header quoting the raw argument names a cap the run never
        # used, and the saved output cannot be reproduced from it.
        self.assertIn("cap 4..64",
                      bench.config_summary(self.args(0), 64))
        self.assertIn("cap 2..64",
                      bench.config_summary(self.args(1), 64))
        self.assertIn("cap 3..64",
                      bench.config_summary(self.args(3), 64))

    def test_the_default_minimum_is_printed_unchanged(self) -> None:
        # The published regime uses --br-min-samples 4, so its header does not
        # move: no number in the guide depends on this change.
        self.assertIn("cap 4..64", bench.config_summary(self.args(4), 64))

    def test_the_summary_keeps_the_rest_of_the_configuration(self) -> None:
        text = bench.config_summary(self.args(4), 64)
        for field in ("5000 iterations", "4 showdown boards",
                      "20000 BR trajectories per player", "5 seeds"):
            self.assertIn(field, text)

    def test_the_summary_is_printed_from_main(self) -> None:
        source = Path(bench.__file__).read_text()
        self.assertIn("print(config_summary(args, cap))", source)


if __name__ == "__main__":
    unittest.main()
