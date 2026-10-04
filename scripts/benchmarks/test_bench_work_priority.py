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
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bench_work_priority as bench

# The guide publishes the numbers this script prints, so a number that moves
# here has to move there. It carried one that was wrong: the interval's
# half-width, quoted as the largest shift the experiment admits.
GUIDE = (Path(__file__).resolve().parents[2]
         / "docs" / "cfr" / "guides" / "work_priority_scheduling.md")


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
    block prints the distances, the floor between the two references, and how
    far apart the arms' own-policy errors are, and stops there.
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

    def test_the_gap_between_the_arms_is_compared_with_the_floor(self) -> None:
        # Own-policy errors 0.0 and 20.0 differ by 20.0; the two references are
        # 40.0 apart. A difference smaller than the reference's own disagreement
        # is not a ranking, and the line has to say so.
        text = self.render(self.ARMS, self.REFS)
        self.assertIn("own-policy errors differ by 20.0", text)
        self.assertIn("within the floor, no ranking", text)

    def test_a_gap_wider_than_the_floor_is_reported_as_clearing_it(self) -> None:
        arms = (("fifo@20,000", [{"nash_conv": 100.0}, {"nash_conv": 200.0}]),
                ("aware@20,000", [{"nash_conv": 300.0}, {"nash_conv": 100.0}]))
        text = self.render(arms, self.REFS)
        # aware's own-policy error is |300-140|, |100-160| -> 110.0, against
        # fifo's 0.0: a 110.0 gap over a 40.0 floor.
        self.assertIn("own-policy errors differ by 110.0", text)
        self.assertIn("clears the floor", text)

    def test_the_floor_is_the_distance_between_the_two_references(self) -> None:
        text = self.render(self.ARMS, self.REFS)
        self.assertIn("floor |fifo@200,000 - aware@200,000| mean 40.0", text)

    def test_a_lone_reference_prints_no_floor(self) -> None:
        # With one reference there is nothing to take a distance from, and a
        # floor of 0.0 would read as agreement rather than as an absence.
        text = self.render(self.ARMS, self.REFS[:1])
        self.assertNotIn("floor", text)

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
    """The bound the guide states on the reported number, and where it comes
    from. Quoting the interval's half-width understates it, because the paired
    mean is nonzero and the interval is off-centre."""

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
        # third; the maximum over the four is what the guide may state.
        half_widths = [100.0 * half / reference
                       for _, _, half, reference in self.PUBLISHED]
        self.assertAlmostEqual(half_widths[0], 1.52, places=2)
        self.assertLess(max(half_widths), max(bounds))

    def test_the_guide_states_the_corrected_bound(self) -> None:
        # Pinned on the bound as stated, not on the bare figures: the guide
        # still quotes the half-widths, correctly, as the thing the bound is
        # *not* taken from.
        text = GUIDE.read_text(encoding="utf-8")
        self.assertIn("at most 2.3% of the", text)
        self.assertNotIn("at most 1.2-1.7%", text)

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
    guide now reports the distances, the gap and the floor instead.
    """

    # spot, fifo@20k vs FIFO@200k and aware@20k vs aware@200k (symmetric),
    #       fifo@20k vs aware@200k and aware@20k vs FIFO@200k (crossed), floor
    PUBLISHED = (("holdem-hu", 80.4, 136.7, 78.4, 137.1, 36.4),
                 ("plo4-hu", 253.5, 187.2, 234.0, 183.1, 93.6),
                 ("plo5-hu", 178.9, 193.5, 154.7, 272.0, 90.4),
                 ("plo4-3way", 492.7, 615.4, 836.8, 272.6, 613.5))

    def test_only_holdem_hu_has_a_gap_that_clears_the_floor(self) -> None:
        # This is the whole reason no winner is named: on three spots of four the
        # two arms' own-policy errors differ by less than the two references
        # disagree on the *same* strategy, so the reference cannot resolve them.
        clears = [spot for spot, fifo_sym, aware_sym, _, _, floor
                  in self.PUBLISHED if abs(fifo_sym - aware_sym) > floor]
        self.assertEqual(clears, ["holdem-hu"])

    def test_the_spot_a_ranking_would_have_named_is_inside_its_floor(self) -> None:
        # plo4-hu: a gap of 66.3 against a floor of 93.6. The old guide named the
        # aware arm there, and the withdrawn claim is pinned below.
        _, fifo_sym, aware_sym, _, _, floor = self.PUBLISHED[1]
        self.assertAlmostEqual(abs(fifo_sym - aware_sym), 66.3, places=1)
        self.assertLess(abs(fifo_sym - aware_sym), floor)

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
        self.assertIn("gap vs floor", section)
        # The withdrawn claims, and the framing that carried them, are gone.
        self.assertNotIn("overestimates the reference by 223.3", text)
        self.assertNotIn("the more accurate estimator on `plo4-hu` alone", text)
        self.assertNotIn("the one spot where the aware arm is the more accurate",
                         text)

    def test_the_guide_prints_the_gaps_and_the_floors(self) -> None:
        text = GUIDE.read_text(encoding="utf-8")
        for value in ("56.3", "66.3", "14.6", "122.7", "36.4", "93.6", "90.4",
                      "613.5"):
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
    at 4x: that crossing is a statement about terminal evaluations, and the
    curve carries no NashConv at a cap tuned to the same budget, so it cannot
    show that spending the saved work improves the answer -- and the interval
    table already bounds this spot's reported movement at 1.61% with no effect
    detected.
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

    ROOT = Path(__file__).resolve().parents[2]
    WORKFLOW = ROOT / ".github" / "workflows" / "solver-benchmark-smoke.yml"

    def test_the_workflow_triggers_on_the_path_the_tests_read(self) -> None:
        # Derived from GUIDE rather than named again, so moving the guide cannot
        # leave the filter pointing at the old path.
        text = self.WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("'%s'" % GUIDE.relative_to(self.ROOT).as_posix(), text)

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


if __name__ == "__main__":
    unittest.main()
