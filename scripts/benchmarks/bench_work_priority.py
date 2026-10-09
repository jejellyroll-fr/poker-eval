#!/usr/bin/env python3
"""Solve-level uncertainty-aware work-priority benchmark (issue #258).

Issue #258 asks for the work-priority layer to be benchmarked at solve level on
four representative workloads -- Hold'em heads-up, PLO4 heads-up, PLO5
heads-up and a multiway Omaha solve -- and for the comparison to show either
faster convergence to the same quality target or better quality for the same
compute budget. The layer landed in #270 and its call site in #271; this is the
measurement.

What the call site can and cannot move is fixed by where it sits. The priority
only decides how many draws the *best-response measurement* spends per decision
(`external_best_response.c`, the per-decision cap); it never touches training.
So the runs below solve the *same* strategy and differ only in how they measure
it, and every claim here is about the measurement. The script enforces that
rather than assuming it -- see `assert_same_training` -- and refuses a run that
stopped before its iteration cap, since a partial solve would otherwise be
published as a complete one -- see `assert_completed`. Neither of the issue's
two demonstrations follows from it, and the guide states both verdicts: the runs
converge identically by construction, so nothing here shows faster convergence,
and no spot detects an effect on the reported number, so nothing here shows
better quality at the same budget.

Each spot is measured twice, at a fixed cap of `--br-max-samples`:

  fifo              the default: every decision may spend the full cap. The
                    historical behaviour, and the baseline.
  uncertainty-aware the cap is interpolated down the decision's priority
                    bucket, so a settled decision is capped near
                    `--br-min-samples` and a fragile one keeps the maximum.

Reported per spot: the solve-and-measurement time (machine-dependent, never a
property of the feature, and deliberately excluding the report phase -- see
`drain_stream`), the measurement's terminal evaluations and draws per decision
(both deterministic for a fixed seed), the measured exploitability, and the
priority layer's own bucket histogram -- the work distribution across infosets
the issue asks for. The comparison against the baseline is *paired*: both arms of
a spot see the same seed and therefore the same solved strategy, so the per-seed
difference removes the strategy and leaves the measurement.

What that comparison may and may not be read as is printed with it, and this is
deliberate. With five seeds, failing to reject "the difference is zero" at the
5% level is *not* evidence that the two arms are equivalent -- a small sample is
too weak to reject anything, and a modestly biased estimator would pass the same
test. So no significance verdict is printed. Two numbers are: the 95%
confidence interval of the paired difference, which bounds the effect the policy
can be having, and each arm's own run-to-run spread across seeds, because a
policy that widens the spread is a real regression even when its mean is
unchanged. The whole interval is then compared against the *baseline* arm's
spread -- the variation a reader already lives with when they change the seed --
and the verdict is stated. The yardstick is the baseline's spread alone and not
the larger of the two, so a policy cannot widen the bound it is judged against;
it is still estimated from the same seeds, so it is a yardstick and not a
pre-specified equivalence margin. Both the interval and the yardstick are built
from a spread, so fewer than two seeds is refused rather than tabulated.

Two diagnostics are printed under each spot, because the size of the saving is
not a constant:

  revisit curve  the saving at a tenth, a quarter and the full BR budget. Only
                 a *repeat* visit to a decision is capped: a decision below the
                 coverage floor ranks in the top bucket and keeps the full cap,
                 so a budget that visits most decisions once has almost nothing
                 to reallocate. This is why `--br-samples` defaults here to
                 20000 rather than the CLI's 2000, and the curve shows both.
  cap curve      the uncertainty-aware cost as the cap is raised. How far it
                 climbs is per workload, and only `holdem-hu` saturates: the
                 confidence rule, not the cap, is what stops most decisions
                 there, so the saving has nowhere to go back to. The other
                 three keep climbing -- `plo5-hu` crosses the FIFO baseline at
                 2x and `plo4-3way` at 4x, where the cap does bind and the saved
                 evaluations are reallocatable.

`--reference-samples` adds a third block: each arm scored against a run at ten
times the budget, which is what says how far an arm's *estimate* moves when the
budget grows rather than only whether the policy moved it. It is off by default
because it costs about ten times as much as the rest. The reference has to share
the arm's seed -- the seed drives the solve, so a disjoint seed scores a
different strategy -- but a shared seed is not neutral either, so each arm is
scored against both policies' higher-budget runs and the two are printed side by
side. No winner is named: see `print_reference_accuracy` for why the block
reports the scale instead. The reference must exceed `--br-samples`, or it is
not a reference at all.

    scripts/benchmarks/bench_work_priority.py --build /tmp/pe-bench
    scripts/benchmarks/bench_work_priority.py --build /tmp/pe-bench \
        --reference-samples 200000
"""

import argparse
import json
import math
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

# The report's row cap must not bind, because `strategy_fingerprint` is the
# guard and a capped report turns it into a guard over a subset. The CLI's
# default of 2,000 is enough at the published 5,000 iterations -- the trained
# rows all fit -- but not once the solve is longer: on PLO4 heads-up at 50,000
# iterations it emits 1,998 of 11,167 trained decisions. A large finite value is
# used rather than `--report-rows 0`, which does not mean "no cap": with 0 the
# two-sweep filter is disabled as well, so every row is printed twice.
REPORT_ROWS = 10_000_000

# Printed after the solve and the BR measurement, before the report: the point
# at which the timed quantity ends. See `drain_stream`.
SOLVE_PHASE_COMPLETE = "solver_phase=complete"

# The accuracy comparison scores each arm against a higher-budget reference, and
# the reference has to share the arm's seed: the seed drives the *solve*, so a
# disjoint seed scores a different strategy rather than the same one measured
# better. Measured on `holdem-hu`, a disjoint-seed reference reported mean|err|
# 1308.7 where a same-seed one reported 101.3 -- a factor of thirteen, all of it
# strategy variation. A shared seed is not neutral either: the tool seeds its
# best-response RNG once from `config->seed` and consumes trajectories in order
# (`external_best_response.c:1206-1215`), so a run at N trajectories is a
# *prefix* of the same policy's run at ten times N. FIFO's error can therefore
# cancel against FIFO's own stream while the aware arm -- a different policy,
# hence a different stream -- gets no such cancellation. Both references are
# printed for every arm, so the asymmetry is measured rather than assumed, and
# the symmetric reading (each arm against its own policy's run) is kept apart
# from the crossed one (against the other policy's): the single table this
# replaces mixed them -- FIFO's symmetric error beside the aware arm's crossed
# one -- and on `plo4-3way` that reversed the verdict.

SPOTS = [
    # name, game, players (full private ranges)
    ("holdem-hu", "holdem", 2),
    ("plo4-hu", "plo4", 2),
    ("plo5-hu", "plo5", 2),
    ("plo4-3way", "plo4", 3),
]

# The solver refuses a sampling cap above this: `BR_MAX_SAMPLES_CEILING` in
# `src/solver/domain/br_sampling.c`, checked in `pe_br_sampling_resolve`. Both
# `--br-max-samples` and the cap curve's multipliers have to respect it, so the
# number lives here once. A test pins it against the C source rather than
# trusting the copy.
BR_MAX_SAMPLES_CEILING = 1 << 20

# The solver resolves `--br-min-samples` before it checks `min <= max`: an unset
# (0) minimum becomes its own default of 4, and anything below 2 is lifted to 2
# (`BR_DEFAULT_MIN` and the floor in `pe_br_sampling_resolve`). A test pins both
# numbers against the C source.
BR_MIN_SAMPLES_DEFAULT = 4
BR_MIN_SAMPLES_FLOOR = 2

# The solver reads `--br-samples` as a uint32: `pe_preflop_solve.c` rejects 0
# and anything above `UINT32_MAX` before the solve starts (the
# `options->br_samples > UINT32_MAX` clause). `--reference-samples` reaches the
# solver through that same flag, so it shares the bound -- and because the
# reference runs come *after* every main FIFO and aware run, an out-of-range
# reference pays for the whole spot and then aborts. A test pins the number
# against the C source rather than trusting the copy.
BR_SAMPLES_MAX = (1 << 32) - 1

# The cap curve is the aware arm's cost as the cap is raised by these factors.
# A point whose product exceeds the ceiling cannot be run, so it is dropped --
# see `cap_curve_multiples`.
CAP_CURVE_MULTIPLES = (1, 2, 4, 8, 16)

# The report fields the best-response measurement owns. Everything else has to
# agree across the policies of a spot: the priority reallocates draws inside a
# measurement, it does not change the strategy that measurement is taken on.
# If a future change lets it reach the training, the paired comparison below
# silently stops comparing like with like, so the script refuses to publish.
MEASUREMENT_FIELDS = frozenset((
    "metrics.exploitability_raw",
    "metrics.exploitability_mbb_per_game",
    "metrics.nash_conv",
    "metrics.nash_conv_mbb_per_game",
    "metrics.max_br_gap",
    "metrics.mean_br_gap",
))

# The storage counters the measurement grows, and the only report fields a
# policy change is allowed to move besides its own numbers. A measurement
# reaches infosets the training never did, and `sampled_action_probability`
# (solver.c) resolves them with create=1 -- its own comment says so: "a miss
# here CREATES the infoset". So the measurement's footprint outside itself is
# the storage it grows, and how far it explores depends on the cap, hence on
# the policy. This is not a training change and it predates the priority:
# `--br-samples 4000` moves the same counters under FIFO alone.
#
# Two halves. The counts and the two ratios move with every extra infoset. The
# byte totals move only when a count crosses a capacity doubling: storage_v2.c
# reports `hash_index_bytes = slot_capacity * sizeof(uint32_t)` (line 810),
# `metadata_bytes += meta_capacity * sizeof(pe_infoset_meta_t)` (811), sums the
# lot into `storage_bytes` (835), and grows both capacities by doubling. They
# answer to the storage's capacities, not to the training, so a pair that
# straddles a doubling refuses the comparison on a difference that is purely
# measurement growth. Reproduced: plo4 heads-up, 500 iterations, seed 1,
# `--br-samples 9000`, the aware arm given a bucket ratio large enough to pin
# every item to bucket 0 (cap 4 against FIFO's 64) -- 22993 infosets on 65536
# hash slots against 22936 on 32768, and `storage_bytes`, `hash_index_bytes`
# and `retained_strategy_bytes` all differ. It is listed rather than ignored so
# that the exemption stays visible and narrow. `recompute_calls` and
# `bytes_saved_vs_full` are deliberately absent: they answer to the memory
# policy, which the report already states outright in `memory_policy`.
GROWTH_FIELDS = frozenset((
    "infosets",
    "memory.total_infosets",
    "memory.adapter_bytes",
    "memory.bytes_per_infoset",
    "memory.bytes_per_strategy_slot",
    # Capacity-derived byte totals -- see above.
    "memory.storage_bytes",
    "memory.hash_index_bytes",
    "memory.metadata_bytes",
    "memory.regret_bytes",
    "memory.average_bytes",
    "memory.other_values_bytes",
    "memory.staging_bytes",
    "memory.allocator_overhead_bytes",
    "memory.retained_strategy_bytes",
    "memory.recomputable_strategy_bytes",
))

EVALS_RE = re.compile(
    r"br_sampling (?:estimator=(\S+) )?terminal_evaluations=(\d+)")
DECISIONS_RE = re.compile(
    r"br_decisions decisions=(\d+) samples=(\d+) avg_samples=([\d.]+) "
    r"early_stop_pct=([\d.]+).*?max_budget_hits=(\d+)", re.S)
PRIORITY_RE = re.compile(
    r"work_priority items=(\d+) buckets=(\d+) coverage_promotions=(\d+) "
    r"aging_promotions=(\d+) unresolved=(\d+) mean_score=(\S+) "
    r"mean_delay=(\S+) p50_bucket=(\d+) p90_bucket=(\d+) depth=([\d,]+)")
# Visits, updates and chance draws are what the training *did*. They are
# counted before the measurement runs and are not touched by the infosets it
# creates, which is exactly the evidence that both arms trained the same way.
EFFORT_RE = re.compile(
    r"street_stats street=(\w+) policy=(\w+) visits=(\d+) updates=(\d+) "
    r"chance_samples=(\d+)")
# The solver names why it stopped on the same line as the phase marker
# (`pe_preflop_solve.c`, right after the solve and the measurement). The marker
# is printed unconditionally and the reason is one of the names in
# `pe_stop_cause_name` (`solver.c`), of which `max_iterations` is the only one
# this benchmark asks for. `SOLVE_PHASE_COMPLETE` is the *prefix* of this line,
# which is why the reason has to be read out of the full line rather than
# inferred from the marker's presence -- see `assert_completed`.
SOLVE_STOP_RE = re.compile(r"solver_phase=complete stop_reason=(\S+)")


def training_effort(text):
    """The training's own per-street counters, as a comparable tuple."""
    return EFFORT_RE.findall(text)


# The hand table, which is emitted for every game -- unlike the range grid.
HAND_TABLE_HEADER = "hand\tnode\tactor\tfrequencies"
HAND_TABLE_END = ("RANGE GRID", "report_phase", "board_query_rows=",
                  "... report capped")


def trained(row):
    """Does this row carry a strategy the training actually produced?

    The report is emitted in two sweeps: rows that carry a strategy first, the
    untouched ones after, and every untouched row holds the uniform vector regret
    matching starts from. Which of them fill the leftover per-node quota depends
    on storage-id order, and the measurement grows the storage, so that tail
    churns between two arms that trained identically -- 135 of 1,998 rows on PLO4
    heads-up at 500 iterations, all of them uniform. Keeping them in the
    fingerprint makes it refuse a comparison it should accept. Dropping them
    costs nothing: a decision that is uniform in both arms is a decision that did
    not change, and a decision that leaves the uniform start enters the set and
    is seen.
    """
    values = [part.split("=")[1] for part in row[3].split(",")]
    return len(set(values)) > 1


def strategy_fingerprint(text):
    """The trained strategy, as a multiset of
    (hand, node, actor, frequencies), over the decisions it was trained on.

    `RANGE GRID` is the obvious fingerprint and the wrong one: the tool emits
    it only for Hold'em (`pe_preflop_solve.c` gates it on `strcmp(options->game,
    "holdem") == 0`), so on the three PLO spots it is absent and a guard that
    reads it is vacuous there. The hand table is emitted for every game.

    The hand is kept, and it is what binds a frequency vector to a decision.
    Without it the projection is a multiset of (node, actor, frequencies), and
    two hands at the same node and actor that exchange their frequency vectors
    leave it unchanged -- a per-decision change the guard would wave through.
    The hand string is a canonical representative over the 24 loose suit
    permutations of hole and board (`preflop_op_infoset_key`), so it names a
    class of decisions rather than one, but it is a *deterministic function of
    the infoset*: measured stable for the same decision across the two policies
    and across two BR budgets, on 24 spot/seed pairs including `plo4-3way`,
    where only 1,867-1,881 of 1,997 rows are shared between two runs that
    solved the same strategy.

    Uniform rows are excluded, which is what lets the hand be kept: they are
    exactly the rows whose presence churns (see `trained`), and including them
    turns the churn into a false refusal. Measured: with every row the
    projection differs between the two policies on three of the four spots at
    500 iterations, and on `plo4-3way` at the published regime; restricted to
    the trained rows it is identical in every regime tried, while still
    separating a 500-iteration strategy from a 5,000-iteration one.

    Two columns are still unusable: the EV column is the measurement's own
    sampled view, and the board column is the sampled deal's runout. The
    frequency column is the report's own `%.1f%%`, so the fingerprint resolves
    0.1 percentage points -- a divergence is caught as soon as one decision
    moves by that much, and the smallest divergence measured (500 iterations
    against 5,000) moves 1% of the shared decisions on the least sensitive spot
    and 99% on the most sensitive.

    A multiset, not the sequence: the row cap fills per-node quotas in
    storage-id order, so which rows make the cut depends on a storage the
    measurement grows. The trained rows *can* be truncated by that quota, and
    the measurement therefore asks for a cap that cannot bind and refuses a
    report that was capped anyway -- see `REPORT_ROWS` and
    `assert_report_uncapped`. Measured on PLO4 heads-up: the CLI's default
    2,000-row report loses none of the 249 trained decisions at the published
    5,000 iterations, but emits 1,998 of 11,167 of them at 50,000 and 1,998 of
    28,496 at 200,000. An earlier version of this docstring asserted that the
    trained rows were never truncated; that was true only of the published
    regime, and it had not been measured.

    The projection is injective only while no two trained rows share a
    `(hand, node, actor)` key, and nothing in the report guarantees that: `node`
    carries no decision identity on these runs. `assert_fingerprint_binds`
    checks the condition and refuses rather than let a swap through -- read the
    two together.
    """
    rows, inside = [], False
    for line in text.splitlines():
        if line.startswith(HAND_TABLE_HEADER):
            inside = True
            continue
        if not inside:
            continue
        if line.startswith(HAND_TABLE_END):
            inside = False
            continue
        cells = line.split("\t")
        # `ev_update` rows carry the same tab layout and are not strategy.
        if len(cells) >= 4 and cells[0] != "ev_update" and trained(cells):
            rows.append((cells[0], cells[1], cells[2], cells[3]))
    return sorted(rows)


def assert_fingerprint_binds(spot, arm, rows):
    """Refuse a fingerprint that cannot bind a frequency vector to a decision.

    The projection is `(hand, node, actor, frequencies)` and `node` is the
    solver's `tree_node_index`. These runs pass no `--tree`, so the column
    carries no decision identity: measured on `plo4-3way` at 5,000 iterations it
    takes only two values -- `-1` (the root, set explicitly in
    `preflop_allin_game.c`) and `0` (every other state, the field's
    zero-initialised value, which the no-tree path never writes) -- and
    `distinct(hand, node, actor)` equals `distinct(hand, actor)` on all 17,424
    rows of the report. The node never separates two rows the hand and actor do
    not already separate, so it is not doing the work it was assumed to do.

    Two *distinct* decisions that share a hand and an actor therefore collapse
    onto one key. Measured on the same report: 3,159 keys carry more than one
    row and 1,514 of those carry *different* frequency vectors -- e.g.
    `TcQc8h3s`/`P1` holds both `fold=25.0%,call=75.0%` and
    `fold=50.0%,all-in=50.0%`. Exchanging two such vectors leaves the multiset
    unchanged, so `assert_same_training` accepts a per-decision change it exists
    to catch; measured directly by swapping the two vectors of
    `5c2hQhAs`/`P1` in a 20,000-iteration report, which leaves the fingerprint
    identical. The published regime happens to have no such key among the
    trained rows of any of the four spots -- checked at 5,000 iterations with
    `--br-samples 20000`, and again at the revisit curve's 2,000 and 5,000 --
    but a guard must not depend on that luck: at 20,000 iterations `plo4-3way`
    has two.

    Nothing in the report closes this. The tool computes the infoset key and a
    betting context (pot, to-call, current bet, raises) for every row
    (`preflop_desc_format`), but prints neither in the hand table, and every
    column it does print is already in the projection. Emitting one of them
    would change the format `poker_eval_studio.c` parses, so it is not this
    script's change to make. Refuse rather than compare a fingerprint that
    cannot tell two decisions apart: accepting one is exactly the silent pass
    this guard exists to prevent.

    A key whose rows all carry the *same* vector is fine and is accepted -- a
    change to either row then moves the multiset, since it holds two copies of
    one tuple and would hold two different ones.
    """
    by_key = {}
    for hand, node, actor, frequencies in rows:
        key = (hand, node, actor)
        first = by_key.setdefault(key, frequencies)
        if first != frequencies:
            sys.exit("%s %s: two trained decisions share (hand=%s node=%s "
                     "actor=%s) with different frequency vectors (%s against "
                     "%s), so the strategy fingerprint cannot tell them apart "
                     "and a swap between them would pass unnoticed. The report "
                     "prints no infoset identity that would separate them; the "
                     "trained rows have to be unambiguous for the comparison "
                     "to mean what it says."
                     % (spot[0], arm, hand, node, actor, first, frequencies))


def assert_report_uncapped(spot, arm, text):
    """Refuse a report the row cap truncated.

    The cap is enforced as a per-node quota filled in storage-id order, so a
    node holding more infosets than its share loses whichever come last. The
    trained rows are printed in the first of the two sweeps, so a long enough
    solve loses *trained* decisions -- exactly the ones the fingerprint exists
    to compare. Measured on PLO4 heads-up with the cap in force: 1,998 of
    11,167 trained decisions emitted at 50,000 iterations, 1,998 of 28,496 at
    200,000.

    The tool prints "... report capped at N visible rows" exactly when its
    budget ran out, and the quota loop can only truncate a node once the budget
    is exhausted, so the absence of that line is the proof that nothing was
    dropped. It is checked rather than assumed: the flag is requested above and
    a workload that outgrows it must fail loudly, not compare a subset.
    """
    if "... report capped at" in text:
        sys.exit("%s %s: the report was capped, so the strategy fingerprint "
                 "would cover only part of the strategy -- raise REPORT_ROWS"
                 % (spot[0], arm))


def assert_completed(spot, arm, text):
    """Refuse a solve that stopped before the iteration cap.

    `--iterations` is the whole point of the run and the header quotes it, so a
    solve that stopped early must not be published as one that ran it. The
    solver stops for reasons other than the cap, and the one that is reachable
    without anyone asking for it is the memory budget: the CLI arms a default of
    70% of physical RAM (`default_ram_budget_bytes`, `pe_preflop_solve.c`) and
    this runner never passes `--max-ram`, so a long enough solve trips
    `PE_STOP_MEMORY_BUDGET` (`solver.c`), writes its report and exits 0 -- a
    success as far as `returncode` is concerned, at whatever iteration it
    reached. The marker names the cause (`stop_reason=%s`), and the reason is
    what separates the two cases.

    This is not caught downstream. Both arms of a spot share a seed and
    therefore the same solve, so the same memory trajectory, so they stop at
    the *same* earlier iteration: every field `assert_same_training` compares
    agrees, the fingerprint agrees, and `config_summary()` still quotes the
    requested `--iterations`. The timing read at the marker is then the time to
    a partial solve, and the exploitability is that of a partial solve. Parse
    the reason and refuse anything that is not `max_iterations`.

    An absent marker is refused too, rather than trusted: `drain_stream`
    tolerates one for the *reading* (it falls back to the caller's final
    reading), but a run that does not state why it stopped cannot be shown to
    have reached the cap, and the solver prints the line unconditionally, so its
    absence is a version mismatch worth failing on rather than a reason to
    guess.
    """
    match = SOLVE_STOP_RE.search(text)
    if match is None:
        sys.exit("%s %s: no `solver_phase=complete stop_reason=` line in the "
                 "solver output, so the run cannot be shown to have reached "
                 "--iterations. The solver prints it unconditionally; a build "
                 "that omits it is too old for this benchmark."
                 % (spot[0], arm))
    reason = match.group(1)
    if reason != "max_iterations":
        sys.exit("%s %s: the solve stopped on %r, not on the iteration cap, so "
                 "it did not run the requested --iterations -- a memory-budget "
                 "stop exits 0 with a partial solve. Raise --max-ram (or cap "
                 "the iterations to what fits) and re-run." % (spot[0], arm,
                                                               reason))


def flatten(report, prefix=""):
    """Every scalar leaf of a report, keyed by its dotted path. Lists are
    reduced to their length: the comparison is about the training, and a list
    that changed length is caught by its own scalar siblings."""
    flat = {}
    for key, value in report.items():
        path = prefix + key
        if isinstance(value, dict):
            flat.update(flatten(value, path + "."))
        elif isinstance(value, list):
            flat[path] = "<list %d>" % len(value)
        else:
            flat[path] = value
    return flat


def parse_priority(text):
    """The priority layer's telemetry line, or None when the policy emitted
    none (the FIFO baseline emits none by design)."""
    match = PRIORITY_RE.search(text)
    if not match:
        return None
    return {
        "items": int(match.group(1)),
        "buckets": int(match.group(2)),
        "coverage_promotions": int(match.group(3)),
        "aging_promotions": int(match.group(4)),
        "unresolved": int(match.group(5)),
        "mean_score": float(match.group(6)),
        "mean_delay": float(match.group(7)),
        "p50_bucket": int(match.group(8)),
        "p90_bucket": int(match.group(9)),
        "depth": [int(v) for v in match.group(10).split(",")],
    }


def drain_stream(lines, clock):
    """Read every line, noting the clock when the solve declares itself done.

    The tool prints `solver_phase=complete` after the solve *and* the BR
    measurement and before the report, so a reading taken there is the solve and
    the measurement and nothing else. Timing the whole invocation instead would
    fold in the report, which with an uncapped report is 1.0-1.3 s on PLO5
    heads-up and PLO4 three-way -- 44% of the invocation -- and which is
    proportional to the infosets the run materialized, a count the two arms do
    not share. Measured: the marker reads 1.447/1.433/1.385 s across report
    sizes of 1, 2,000 and 10,000,000 rows, while the totals read 1.527/1.614/
    2.408 s, so the reading is stable where the total is not.

    Falls back to the caller's final reading if the marker never appears, which
    is the old behaviour rather than a missing column. That tolerance is about
    the *reading* only: the benchmark refuses a run whose marker is absent or
    names a reason other than `max_iterations` -- see `assert_completed`.
    """
    text, marked = [], None
    for line in lines:
        if marked is None and line.startswith(SOLVE_PHASE_COMPLETE):
            marked = clock()
        text.append(line)
    return "".join(text), marked


def measure(binary, spot, args, seed, policy, cap, br_samples, out_json):
    _, game, players = spot
    cmd = [binary, "--game", game, "--players", str(players),
           "--algorithm", "external-mccfr",
           "--iterations", str(args.iterations),
           "--samples", str(args.samples),
           "--br-samples", str(br_samples),
           "--br-min-samples", str(args.br_min_samples),
           "--br-max-samples", str(cap),
           "--exploitability-interval", str(args.iterations),
           "--seed", str(seed), "--output", out_json,
           "--report-rows", str(REPORT_ROWS)]
    if policy is not None:
        cmd += ["--br-priority-policy", policy]
    start = time.perf_counter()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, bufsize=1)
    out, marked = drain_stream(proc.stdout, time.perf_counter)
    proc.wait()
    finished = time.perf_counter()
    # Not "wall": the report phase is deliberately excluded, so this is not
    # comparable to the same-named column in the sibling benchmarks.
    solve = (marked - start) if marked is not None else (finished - start)
    if proc.returncode != 0:
        sys.exit("solve failed: %s\n%s" % (" ".join(cmd), out[-2000:]))
    # A zero exit status is not proof the solve reached the cap: the memory
    # budget stops it early and still exits 0 -- see assert_completed.
    assert_completed(spot, policy, out)
    assert_report_uncapped(spot, policy, out)
    with open(out_json) as handle:
        report = json.load(handle)
    evals = EVALS_RE.findall(out)
    decisions = DECISIONS_RE.search(out)
    if not evals or not decisions:
        sys.exit("no br_sampling/br_decisions line in: %s\n%s"
                 % (" ".join(cmd), out[-2000:]))
    # The comparison is only as strong as the fingerprint's ability to tell two
    # decisions apart, and the report does not guarantee that -- see
    # assert_fingerprint_binds.
    strategy = strategy_fingerprint(out)
    assert_fingerprint_binds(spot, policy, strategy)
    return {
        "solve": solve,
        "report": report,
        # Issue #274: the sampled BR defaults to the confidence-guided
        # estimator, so a row that silently measured the historical
        # one-rollout one would be mislabelled. The caller checks this.
        "estimator": evals[-1][0],
        "evals": int(evals[-1][1]),
        "decisions": int(decisions.group(1)),
        "avg_draws": float(decisions.group(3)),
        "early": float(decisions.group(4)),
        "max_budget_hits": int(decisions.group(5)),
        "nash_conv": float(report["metrics"]["nash_conv_mbb_per_game"]),
        "priority": parse_priority(out),
        "effort": training_effort(out),
        "strategy": strategy,
    }


def assert_same_training(spot, arm, run, baseline_run):
    """Refuse to compare two arms that did not solve the same strategy.

    Three checks, weakest to strongest evidence: the report's fields agree
    outside the measurement's own numbers and the storage it grows; the
    training's visits, updates and chance draws agree street by street; and
    the trained strategy's frequencies agree decision by decision. A policy
    that reached the training would have to break all three at once to get
    past this. The third check refuses an empty fingerprint rather than
    passing it: an empty list compares equal to another empty list, which is
    how a game whose report format dropped the hand table would quietly lose
    the guard instead of failing it."""
    a, b = flatten(run["report"]), flatten(baseline_run["report"])
    if set(a) != set(b):
        sys.exit("%s %s: the report's shape changed across policies (%s)"
                 % (spot[0], arm, sorted(set(a) ^ set(b))))
    allowed = MEASUREMENT_FIELDS | GROWTH_FIELDS
    differing = sorted(k for k in a if a[k] != b[k] and k not in allowed)
    if differing:
        sys.exit("%s %s: the priority policy reached fields it must not "
                 "touch, so this is not the same solved strategy: %s"
                 % (spot[0], arm, differing))
    if run["effort"] != baseline_run["effort"]:
        sys.exit("%s %s: the training did different work under this policy: "
                 "%s vs %s" % (spot[0], arm, run["effort"],
                               baseline_run["effort"]))
    if not run["strategy"]:
        sys.exit("%s %s: no decision in the report carries a trained strategy, "
                 "so the arms cannot be shown to have solved the same one -- "
                 "either the report has no hand table, or --iterations is too "
                 "low for the strategy to have left the uniform start"
                 % (spot[0], arm))
    if run["strategy"] != baseline_run["strategy"]:
        sys.exit("%s %s: the trained strategy differs under this policy, so "
                 "the arms are not measuring the same thing" % (spot[0], arm))


def check_estimator(spot, label, runs):
    """The row label is a claim about which estimator produced the numbers;
    refuse to publish it when the solver says otherwise."""
    for run in runs:
        if run["estimator"] != "confidence-guided":
            sys.exit("%s %s: expected the confidence-guided estimator, the "
                     "solver reported %r" % (spot[0], label,
                                             run["estimator"]))


def fmt(value, spec=".1f"):
    return "-" if value is None else format(value, spec)


def mean_of(runs, key):
    return statistics.mean(run[key] for run in runs)


def sd_of(runs, key):
    """The arm's own run-to-run spread across seeds. Printed next to the paired
    difference because "no significant difference" says nothing about whether
    the arm got *noisier*, and a policy that widens the spread is a real
    regression even when its mean is unchanged."""
    values = [run[key] for run in runs]
    return statistics.stdev(values) if len(values) > 1 else 0.0


# Two-sided 95% Student-t quantiles, df 1..30. The sample is five seeds, so the
# normal quantile would understate the interval by a third at df=4 (1.96
# against 2.776).
T95 = (12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
       2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
       2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042)


def t95_expansion(df):
    """The Cornish-Fisher Student-t quantile, accurate from df=10 upwards.

    Agrees with `T95` to within 0.0004 over the whole overlap (df 10..30), and
    with the published tables above it -- 2.0395 at df=31, 2.0211 at df=40,
    1.9800 at df=120 -- before converging on the normal 1.95996.
    """
    z = 1.959963984540054  # the normal 97.5% quantile
    z3, z5, z7 = z ** 3, z ** 5, z ** 7
    return (z + (z3 + z) / (4.0 * df)
            + (5.0 * z5 + 16.0 * z3 + 3.0 * z) / (96.0 * df * df)
            + (3.0 * z7 + 19.0 * z5 + 17.0 * z3 - 15.0 * z) / (384.0 * df ** 3))


def t95(df):
    """Two-sided 95% Student-t quantile.

    The table is the authority for the small samples this benchmark actually
    uses. Past it the flat normal quantile is not good enough: at 31 degrees of
    freedom the quantile is 2.0395, not 1.96, so a 32-seed run would print an
    interval 4% too narrow -- and the margin verdict is decided by whether that
    interval fits inside a bound, so the error is not cosmetic.
    """
    if 1 <= df <= len(T95):
        return T95[df - 1]
    return t95_expansion(df)


def paired_interval(diffs):
    """The paired mean difference and its 95% confidence interval.

    Reported *instead of* a significance verdict, and this is the whole point of
    the line: with five seeds, failing to reject "the difference is zero" at the
    5% level is not evidence that the two arms are equivalent -- a small sample
    is simply too weak to reject anything. The interval is what the data
    supports, and it bounds the effect the policy can be having. Its *farthest
    endpoint*, not its half-width, is the bound, because a nonzero mean leaves
    the interval off-centre; both endpoints are then judged against the
    measurement's own run-to-run spread, which is the variation a reader already
    lives with when they change the seed: an effect smaller than that is one
    they could not have noticed.

    A single observation has no spread, so the interval degenerates to a point.
    That is returned as-is -- the function does not invent a width -- and the
    caller refuses to print it as a confidence interval: see
    `require_paired_seeds`.
    """
    n = len(diffs)
    mean = statistics.mean(diffs)
    if n < 2:
        return mean, 0.0, 0.0
    sd = statistics.stdev(diffs)
    return mean, sd, t95(n - 1) * sd / math.sqrt(n)


def require_paired_seeds(seeds):
    """At least two seeds, because one cannot estimate a spread.

    With a single pair the interval is zero-width *and* the yardstick -- the
    baseline arm's cross-seed spread -- collapses to zero, so the printed line
    reads "95% CI [+261.8, +261.8] ... baseline sd 0.0 = +0.00%": a point
    estimate labelled a confidence interval, and a bound of zero that turns the
    verdict into a coin flip on the sign of the mean. Neither is a property of
    the measurement, so the run is refused rather than tabulated.
    """
    if seeds < 2:
        sys.exit("--seeds %d: the paired interval, the cross-seed spread and "
                 "the margin it is judged against all need two seeds at least; "
                 "one seed prints a zero-width interval labelled a 95%% CI. "
                 "Use --seeds 2 for a quick run." % seeds)


def require_higher_reference(br_samples, reference_samples):
    """The accuracy reference must be a *higher* budget than the arm it judges.

    Nothing checked this, so `--reference-samples 1000` against the default
    `--br-samples 20000` was accepted and printed in exactly the format a real
    reference uses. Measured on `holdem-hu`: errors of 871.6 and 732.3 against a
    1,000-trajectory "reference", whose two policies disagree by 417.4. A run at
    or below the arm's budget is not a reference -- it scores the arm against an
    estimate no more precise than itself, and the numbers cannot carry the
    accuracy reading the flag promises. Refused before any solve starts, rather
    than tabulated.

    The budget also has to be one the *solver* will accept. `--reference-samples`
    reaches the solver as `--br-samples`, which is a uint32 there
    (`tools/pe_preflop_solve.c:1252`), so anything above `UINT32_MAX` is refused
    -- and the ordering check above cannot see it, because an oversized value
    trivially exceeds `--br-samples`. The cost of missing this is not symmetric
    with the ordering defect: the reference runs come *after* every main FIFO and
    aware run, so the typo would pay for the whole spot and then abort on the
    first reference invocation without a block. Measured with
    `--reference-samples 4294967296` before the fix: the four main rows printed,
    then `solve failed: ... --br-samples 4294967296 ...`, exit 1.
    """
    if reference_samples < 0:
        # A *message* branch, not a refusal branch: `reference_samples <=
        # br_samples` below would refuse a negative anyway, since a negative is
        # below every budget. What it adds is the reason -- "a negative budget is
        # not a budget" rather than "score it against something higher". The
        # refusal itself is pinned by the call site guarding on `!= 0`, without
        # which neither branch ran at all. Measured with `--reference-samples -1`
        # before the fix: the reference block was absent and the command exited 0.
        sys.exit("--reference-samples %d: a negative budget is not a budget. "
                 "0 skips the comparison; any other value has to be a *higher* "
                 "budget than --br-samples %d."
                 % (reference_samples, br_samples))
    if reference_samples > BR_SAMPLES_MAX:
        # Placed before the ordering check, which an oversized value passes
        # trivially -- so this is the only branch that can explain it. It also
        # catches an oversized `--br-samples` when a reference is asked for, since
        # the reference would then have to exceed the ceiling as well.
        sys.exit("--reference-samples %d: the solver takes --br-samples up to "
                 "%d and rejects anything above it, so the reference run cannot "
                 "start. The reference runs come after every main FIFO and aware "
                 "run, so this would pay for the whole spot and then abort "
                 "without a reference block."
                 % (reference_samples, BR_SAMPLES_MAX))
    if reference_samples <= br_samples:
        sys.exit("--reference-samples %d: the accuracy reference has to be a "
                 "*higher* budget than --br-samples %d, otherwise the arm is "
                 "scored against an estimate no more precise than itself and "
                 "the block cannot say what it claims to. The published table "
                 "uses ten times the arm's budget."
                 % (reference_samples, br_samples))


def require_known_spots(selected):
    """Every `--spot` name has to name a spot, or the run is refused.

    The selection is `[s for s in SPOTS if s[0] in selected]`, so a name that
    matches nothing is dropped in silence. Measured: `--spot bogus` printed the
    header and exited **0** -- a typo read as a successful run -- and
    `--spot bogus --spot holdem-hu` published `holdem-hu` alone without ever
    mentioning the name it had discarded. Both are worse here than in an
    ordinary tool, because the point of the script is to publish a number: a
    partial benchmark and a complete one are the same output, and a reader who
    sees one row where the issue names four cannot tell a typo from a decision.
    The two neighbouring entry points already refuse rather than tabulate, and
    this one has to as well.
    """
    if not selected:
        return
    known = [s[0] for s in SPOTS]
    unknown = [name for name in selected if name not in known]
    if unknown:
        sys.exit("--spot %s: not a spot. The spots are %s."
                 % (", ".join(unknown), ", ".join(known)))


def effective_min_samples(br_min_samples):
    """The minimum the solver actually compares `--br-max-samples` against.

    `pe_br_sampling_resolve` replaces an unset (0) minimum with its own default
    of 4 and lifts anything below 2 up to 2, both *before* the
    `min_samples <= max_samples` check -- so the effective value is neither the
    raw one nor a plain floor of it. Measured against the shipped solver, the
    first cap that runs is 4 for `--br-min-samples 0`, 2 for `1` and `2`, and 3
    for `3` (cap 3 completes, cap 2 dies on `preflop solve failed: status=5`),
    and the model reproduces every combination measured.

    Only defined for a non-negative argument: a negative one never reaches
    `pe_br_sampling_resolve`, because the solver's own option parser refuses it
    ("missing value for --br-min-samples") -- `require_supported_caps` refuses
    it up front instead.
    """
    if br_min_samples == 0:
        return BR_MIN_SAMPLES_DEFAULT
    return max(br_min_samples, BR_MIN_SAMPLES_FLOOR)


def require_supported_caps(br_min_samples, br_max_samples):
    """Both sampling caps have to be ones the solver will actually run.

    The solver validates the *pair*, so there are four ways to be unrunnable
    and all four used to surface only when the solver refused mid-sweep:

    - `--br-max-samples 0` -- the solver *accepts* it (0 means "sampling off",
      `tools/pe_preflop_solve.c:702`) but then emits no
      `br_sampling`/`br_decisions` line, so `check_estimator` has nothing to
      read. Measured: one solve ran and printed
      `report_phase=complete rows=309`, then
      `no br_sampling/br_decisions line in: ...`, exit 1.
    - Above `BR_MAX_SAMPLES_CEILING` the solver rejects the cap outright.
    - Below the effective minimum: the solver floors `min_samples` at 2 and then
      requires `min <= max`, so `--br-max-samples 1` can never run (the floor
      lifts min to 2), and on the shipped default `--br-min-samples 4` the first
      cap that runs is 4. Measured: caps 1, 2 and 3 all die on
      `preflop solve failed: status=5`, cap 4 is the first that completes.
    - A *negative* `--br-min-samples`, which no branch above covers: the
      solver's own option parser refuses the token before `pe_br_sampling_resolve`
      sees it, so it never gets to the floor. Measured with
      `--br-min-samples -1 --br-max-samples 64`: `missing value for
      --br-min-samples` and a usage dump, exit non-zero -- and the pair is
      accepted by this guard, so it surfaced at the first measurement rather
      than up front, which is what this function exists to prevent.

    Refused up front, because the first measurement is the expensive one and
    none of the four can be degraded to something runnable -- the main
    measurements use these caps directly.
    """
    if br_max_samples < 1:
        # A *message* branch, not a refusal branch: the ordering check below
        # would refuse 0 anyway, because the resolved minimum is never below 2.
        # What it adds is the reason -- the metrics have no lines to be read from
        # -- rather than "the smallest cap that runs here is 4", which would send
        # the reader after the wrong argument.
        sys.exit("--br-max-samples %d: the benchmark needs the confidence-guided "
                 "best response on, and 0 turns it off -- the solver then emits "
                 "no br_sampling/br_decisions line for the metrics to be read "
                 "from. Use at least 1 (the default is 64)." % br_max_samples)
    if br_max_samples > BR_MAX_SAMPLES_CEILING:
        sys.exit("--br-max-samples %d: the solver refuses a sampling cap above "
                 "%d (`BR_MAX_SAMPLES_CEILING`, src/solver/domain/"
                 "br_sampling.c), and the main measurements use this cap "
                 "directly. Use --br-max-samples %d or less; the cap curve "
                 "already drops the multipliers that would exceed the ceiling."
                 % (br_max_samples, BR_MAX_SAMPLES_CEILING,
                    BR_MAX_SAMPLES_CEILING))
    if br_min_samples < 0:
        # Placed after the cap branches and before the floor: a negative
        # minimum is not a smaller minimum, it is an argument the solver's
        # parser will not read at all, so there is no resolved value to compare
        # against. `effective_min_samples` is therefore not consulted here --
        # feeding it a negative would answer 2 and turn a parse failure into a
        # plausible-looking cap.
        sys.exit("--br-min-samples %d: the solver's option parser refuses a "
                 "negative value before it is resolved (`missing value for "
                 "--br-min-samples`, then a usage dump), so no run can start. "
                 "Use 0 for the solver's default of 4, or any value from 2 up."
                 % br_min_samples)
    floor = effective_min_samples(br_min_samples)
    if br_max_samples < floor:
        sys.exit("--br-max-samples %d: the solver resolves --br-min-samples %d "
                 "to %d and then requires min_samples <= max_samples, so the "
                 "smallest cap that runs here is %d. Raise --br-max-samples or "
                 "lower --br-min-samples."
                 % (br_max_samples, br_min_samples, floor, floor))


def print_row(spot, label, runs, baseline):
    diffs = [r["nash_conv"] - b["nash_conv"] for r, b in zip(runs, baseline)]
    _, spread, _ = paired_interval(diffs)
    print("%-11s %-17s %8.2f %12s %10s %8s %9s %14.1f %8.1f %+10.1f +- %5.1f"
          % (spot[0], label, mean_of(runs, "solve"),
             "{:,}".format(int(mean_of(runs, "evals"))),
             fmt(mean_of(runs, "avg_draws")), fmt(mean_of(runs, "early")),
             "{:,}".format(int(mean_of(runs, "max_budget_hits"))),
             mean_of(runs, "nash_conv"), sd_of(runs, "nash_conv"),
             statistics.mean(diffs), spread))
    sys.stdout.flush()


def interval_within(mean, half, bound):
    """Is the whole confidence interval inside [-bound, +bound]?

    Both endpoints, not the half-width: a nonzero mean shifts the interval, and
    comparing `half` alone labels an interval that reaches past the bound as
    "inside". The published PLO4 row is the counter-example that caught this --
    mean +119.9, half-width 255.1, bound 273.1, and an upper endpoint of +375.0.
    The condition is `|mean| + half <= bound`, which is the same as requiring
    both endpoints to lie within the bounds.
    """
    return abs(mean) + half <= bound


def paired_yardstick(baseline, aware):
    """The bound the paired interval is judged against: the *baseline* arm's
    cross-seed spread, and deliberately not the larger of the two arms'.

    Taking the maximum lets the judged arm widen its own bound: a policy that
    increases run-to-run variance raises the bar it has to clear. Measured on
    the published run the aware arm set the margin on two of the four spots --
    `holdem-hu` 700.8 against the baseline's 660.9, `plo5-hu` 250.3 against
    160.7, an inflation of 56%. The aware arm's spread is still reported, as a
    diagnostic -- a wider spread is a regression in its own right -- but it does
    not enter the bound.

    The bound is a yardstick, not a pre-specified equivalence margin: it is
    estimated from the same seeds being judged, and its own uncertainty is not
    accounted for. What it supports is "the effect, at its widest, is smaller
    (or larger) than the variation the baseline policy itself shows across
    seeds", which is a practical-significance statement and not a test.
    """
    return sd_of(baseline, "nash_conv")


def print_paired_bound(spot, baseline, aware):
    """What the paired difference bounds, against the baseline's own spread.

    The point of the line is to refuse the reading "the answer did not move":
    a null result is not equivalence. It prints the 95% confidence interval of
    the paired difference and judges the *whole* interval, both endpoints at
    once, against the baseline arm's own run-to-run spread across seeds -- the
    variation a reader already lives with when they change the seed. See
    `paired_yardstick` for why the bound is that arm's spread alone.
    """
    diffs = [r["nash_conv"] - b["nash_conv"] for r, b in zip(aware, baseline)]
    mean, _, half = paired_interval(diffs)
    reference = mean_of(baseline, "nash_conv")
    sd_fifo = sd_of(baseline, "nash_conv")
    sd_aware = sd_of(aware, "nash_conv")
    yardstick = paired_yardstick(baseline, aware)
    ratio = (sd_aware / sd_fifo) if sd_fifo else float("nan")
    verdict = ("inside the baseline's spread"
               if interval_within(mean, half, yardstick)
               else "wider than the baseline's spread")
    print("%-11s %-17s mean %+.1f mBB, 95%% CI [%+.1f, %+.1f] = [%+.2f%%, "
          "%+.2f%%] of %s; cross-seed sd %.1f (fifo) %.1f (aware) ratio %.2f; "
          "baseline sd %.1f = %+.2f%% -- CI %s"
          % (spot[0], "paired bound", mean, mean - half, mean + half,
             100.0 * (mean - half) / reference, 100.0 * (mean + half) / reference,
             "{:,.1f}".format(reference), sd_fifo, sd_aware, ratio,
             yardstick, 100.0 * yardstick / reference, verdict))
    sys.stdout.flush()


def print_distribution(spot, label, runs):
    """The work distribution across infosets, from the layer's own line. The
    baseline emits none, which is itself the point: with FIFO there is nothing
    to report because every decision is served the same way."""
    prio = [run["priority"] for run in runs if run["priority"]]
    if not prio:
        return
    depth = [int(statistics.mean(p["depth"][b] for p in prio))
             for b in range(prio[0]["buckets"])]
    print("%-11s %-17s buckets %s  mean_score %s  p50 %d  p90 %d  "
          "floor %s  aging %s  unresolved %s"
          % (spot[0], label, ",".join(map(str, depth)),
             fmt(statistics.mean(p["mean_score"] for p in prio), ".4g"),
             statistics.mean(p["p50_bucket"] for p in prio),
             statistics.mean(p["p90_bucket"] for p in prio),
             fmt(statistics.mean(p["coverage_promotions"] for p in prio)),
             fmt(statistics.mean(p["aging_promotions"] for p in prio)),
             fmt(statistics.mean(p["unresolved"] for p in prio))))
    sys.stdout.flush()


def saving(baseline_evals, aware_evals):
    return 100.0 * (baseline_evals - aware_evals) / baseline_evals


def revisit_budgets(br_samples):
    """The budgets the revisit curve is drawn at: a tenth, a quarter, all of it.

    Integer division can produce a *zero* budget -- `--br-samples 5` gives a
    tenth of zero -- and the solver refuses `--br-samples 0` outright
    (`tools/pe_preflop_solve.c:1250-1253`). The curve is drawn *after* the
    spot's main measurements, so the abort used to land once the expensive work
    was already done: measured with `--br-samples 5`, the four rows printed and
    the run then died on `solve failed: ... --br-samples 0`, losing the curve,
    the cap curve and every later spot with it. Only positive budgets are kept,
    and the set drops the duplicates that small budgets collide into. Each
    point is labelled with the budget it used, so a short curve says which of
    the three it had to drop rather than silently drawing two.
    """
    return sorted({br_samples // 10, br_samples // 4, br_samples} - {0})


def cap_curve_multiples(cap):
    """The cap-curve multipliers whose product the solver will accept.

    The curve scales `--br-max-samples` by a fixed ladder, and the solver refuses
    any cap above `BR_MAX_SAMPLES_CEILING`. Measured: with `--br-max-samples
    100000` the main rows and the revisit curve printed, then the 16x point asked
    for 1,600,000 and the run died on `preflop solve failed: status=5`, losing
    the cap curve and every later spot after the expensive work had been paid
    for. Multipliers that would exceed the ceiling are dropped rather than the
    run refused, because the curve is a diagnostic whose reading is its *shape*
    per workload -- saturating on `holdem-hu`, crossing the baseline on
    `plo5-hu` and `plo4-3way`. A shorter curve still shows the shape, and each
    point keeps its multiplier label, so a curve ending at 8x says which point
    it dropped. The
    1x point is never dropped, so the curve always reports something.
    """
    return tuple(m for m in CAP_CURVE_MULTIPLES
                 if cap * m <= BR_MAX_SAMPLES_CEILING)


def print_reference_accuracy(spot, arms, references):
    """Each arm's distance from a higher-budget reference, under every reference.

    `arms` and `references` are in the *same* order: arm i is the one whose own
    policy produced reference i, so the symmetric reading -- each arm against
    its own policy's higher-budget run -- is the diagonal.

    The paired interval says how far the policy moves the reported number; this
    says how far each arm's estimate moves when its budget grows tenfold, which
    is a different question and needs a reference. Every reference is printed
    for every arm, because the reference is not neutral -- the comment above the
    spots records why -- and printing both turns that from a caveat into a
    measurement.

    No winner is named, and that is the point. Neither reading is neutral: the
    reference shares the arm's seed, so *both* arms' errors carry a prefix
    covariance with their own stream, and nothing here shows the two covariances
    are comparable. An arm whose stream happened to be the more stable one could
    win without being the closer to the true NashConv.

    What is printed instead is the scale: how far the two higher-budget runs
    sit apart on the *same* strategy, and how far apart the two arms'
    own-policy errors are. The two numbers are printed side by side and are
    **not** put in a ratio, because the spread is not a null distribution for
    the difference. Two reasons, both measured. They are taken at different
    budgets -- the spread at ten times the arm's, the difference from the
    arm's own -- and the spread shrinks as the estimator converges: on
    `holdem-hu` the policy spread is 119.7 mBB at 20,000 trajectories against
    36.4 at 200,000, so the gap of 56.3 exceeds the spread taken at 200,000
    and is half the spread taken at 20,000, which is the budget the gap is
    derived from. And a spread between two estimates is not a difference
    between two mean absolute errors: holding the arms and their gap fixed at
    0.2 and moving only the references changes which number is larger, with
    the references in exact agreement making the gap the larger one while both
    sit far from the truth. A comparison the references decide on their own is
    not evidence about the arms.
    """
    rows = []
    for ref_label, refs in references:
        cells = []
        for arm_label, runs in arms:
            errors = [r["nash_conv"] - x["nash_conv"] for r, x in zip(runs, refs)]
            cells.append((arm_label, statistics.mean(abs(e) for e in errors),
                          statistics.mean(errors)))
        rows.append(cells)
        print("%-11s vs %-15s mean|err| %s"
              % (spot[0], ref_label,
                 " ".join("%s %6.1f (mean %+7.1f)" % cell for cell in cells)))
    if len(references) == 2 and len(arms) == 2:
        (first_label, first), (second_label, second) = references
        # The two references disagree on the *same* strategy by this much. It
        # is printed because it is the scale of the reference choice; it is
        # not compared with the gap on the same line, for the two measured
        # reasons in the docstring. It used to be printed as a "floor" the gap
        # either "cleared" or sat "within", which read the spread as a
        # threshold.
        spread = statistics.mean(abs(a["nash_conv"] - b["nash_conv"])
                                 for a, b in zip(first, second))
        # The diagonal: arm 0 against reference 0, arm 1 against reference 1.
        own = abs(rows[0][0][1] - rows[1][1][1])
        print("%-11s refs differ by %.1f (|%s - %s|); own-policy errors differ "
              "by %.1f (scale, not a threshold)"
              % (spot[0], spread, first_label, second_label, own))
    sys.stdout.flush()


def config_summary(args, cap):
    """The one-line configuration summary printed above the table.

    Prints the *resolved* minimum, not the raw argument. `pe_br_sampling_resolve`
    replaces an unset (0) minimum with 4 and lifts anything below 2 up to 2 --
    measured against the shipped solver, `--br-min-samples 0` runs with a minimum
    of 4 and `--br-min-samples 1` with 2 -- so a header quoting the raw argument
    would name a cap the run never used. That matters here more than usual
    because this line is the *only* configuration summary attached to the saved
    output: the solver's own stdout is parsed for metrics and the fingerprint and
    never printed, so `cap 0..64` next to a table produced at 4..64 cannot be
    reproduced from what was saved. The resolved value can: passing it back gives
    the identical run.
    """
    return ("pe-preflop-solve, external MCCFR, %d iterations, %d showdown "
            "boards, %d BR trajectories per player, cap %d..%d, %d seeds"
            % (args.iterations, args.samples, args.br_samples,
               effective_min_samples(args.br_min_samples), cap, args.seeds))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", required=True, help="CMake build directory")
    ap.add_argument("--iterations", type=int, default=5000,
                    help="enough that the strategy is actually trained: at "
                         "500 of 338 infosets are still uniform")
    ap.add_argument("--samples", type=int, default=4,
                    help="showdown boards per called terminal")
    ap.add_argument("--br-samples", type=int, default=20000,
                    help="best-response trajectories per player; the CLI "
                         "defaults to 2000, at which most decisions are "
                         "visited once and the revisit curve shows the cost")
    ap.add_argument("--br-min-samples", type=int, default=4,
                    help="draws per action for a settled decision; the solver "
                         "resolves 0 to 4 and floors anything below 2 before it "
                         "is compared with --br-max-samples")
    ap.add_argument("--br-max-samples", type=int, default=64,
                    help="draws per action for a fragile decision; has to be at "
                         "least the resolved --br-min-samples and at most "
                         "1048576, since 0 turns the confidence-guided best "
                         "response off and the benchmark reads its metrics from "
                         "the lines that then go missing")
    ap.add_argument("--seeds", type=int, default=5,
                    help="paired seeds; two at least, since one cannot estimate "
                         "the spread the interval and the yardstick are built "
                         "from")
    ap.add_argument("--reference-samples", type=int, default=0,
                    help="BR trajectories for the higher-budget reference the "
                         "accuracy comparison scores each arm against; 0 skips "
                         "the comparison, and any value at or below --br-samples "
                         "is refused. The solver takes --br-samples as a uint32, "
                         "so a value above 4294967295 is refused too -- the "
                         "reference runs come after the main ones, so a typo "
                         "there would pay for the spot and then abort. The "
                         "published table uses 200000, ten times --br-samples, "
                         "and takes about ten times as long")
    ap.add_argument("--spot", action="append",
                    help="only these spots, by exact name; a name that is not "
                         "a spot is refused rather than dropped, so a typo "
                         "cannot publish a partial benchmark in silence")
    args = ap.parse_args()
    require_paired_seeds(args.seeds)
    require_known_spots(args.spot)
    require_supported_caps(args.br_min_samples, args.br_max_samples)
    # `!= 0`, not `> 0`: the help says 0 skips the comparison, so every other
    # value has to be validated. Guarding on `> 0` let a negative budget through
    # both this check and the execution guard below, and the run then skipped
    # the requested block and exited 0.
    if args.reference_samples != 0:
        require_higher_reference(args.br_samples, args.reference_samples)

    binary = os.path.join(args.build, "tools", "pe-preflop-solve")
    if not os.path.exists(binary):
        sys.exit("no solver at %s" % binary)
    cap = args.br_max_samples
    spots = [s for s in SPOTS if not args.spot or s[0] in args.spot]

    print(config_summary(args, cap))
    print("Every arm of a spot solves the same strategy; the training counters "
          "and the trained strategy's per-decision frequencies are checked to "
          "agree.")
    print()
    header = ("%-11s %-17s %8s %12s %10s %8s %9s %14s %8s %19s"
              % ("spot", "policy", "solve s", "BR evals", "draws/dec",
                 "early %", "cap hits", "NashConv mBB", "sd",
                 "vs fifo (paired)"))
    print(header)
    print("-" * len(header))

    with tempfile.TemporaryDirectory() as tmp:
        out_json = os.path.join(tmp, "run.json")
        for spot in spots:
            seeds = [100 + k for k in range(args.seeds)]
            baseline = [measure(binary, spot, args, seed, "fifo", cap,
                                args.br_samples, out_json) for seed in seeds]
            aware = [measure(binary, spot, args, seed, "uncertainty-aware",
                             cap, args.br_samples, out_json)
                     for seed in seeds]
            check_estimator(spot, "fifo", baseline)
            check_estimator(spot, "uncertainty-aware", aware)
            for run, base in zip(aware, baseline):
                assert_same_training(spot, "uncertainty-aware", run, base)

            print_row(spot, "fifo", baseline, baseline)
            print_row(spot, "uncertainty-aware", aware, baseline)
            print_distribution(spot, "uncertainty-aware", aware)
            print_paired_bound(spot, baseline, aware)

            # The accuracy comparison, when asked for: each arm scored against a
            # higher-budget run of *both* policies, so the reference's own bias
            # is visible instead of assumed. See print_reference_accuracy.
            # `> 0` here is the "is a budget running this block" question, not a
            # validity check: `require_higher_reference` above has already
            # refused every non-zero value it would not accept.
            if args.reference_samples > 0:
                labels = ("fifo@%s" % "{:,}".format(args.br_samples),
                          "aware@%s" % "{:,}".format(args.br_samples))
                ref_labels = ("fifo@%s" % "{:,}".format(args.reference_samples),
                              "aware@%s" % "{:,}".format(args.reference_samples))
                references = []
                for ref_label, policy in zip(ref_labels, ("fifo", "uncertainty-aware")):
                    runs = [measure(binary, spot, args, seed, policy, cap,
                                    args.reference_samples, out_json)
                            for seed in seeds]
                    references.append((ref_label, runs))
                print_reference_accuracy(
                    spot, ((labels[0], baseline), (labels[1], aware)),
                    tuple(references))

            # Only a repeat visit is capped, so the saving tracks how often the
            # budget revisits a decision. One seed is enough to show the shape.
            revisit = []
            for budget in revisit_budgets(args.br_samples):
                fifo_run = measure(binary, spot, args, seeds[0], "fifo", cap,
                                   budget, out_json)
                aware_run = measure(binary, spot, args, seeds[0],
                                    "uncertainty-aware", cap, budget, out_json)
                revisit.append("%s %.1f%%" % ("{:,}".format(budget),
                                              saving(fifo_run["evals"],
                                                     aware_run["evals"])))
            print("%-11s %-17s %s" % (spot[0], "revisit curve",
                                      "  ".join(revisit)))

            # How far the curve climbs is per workload: only `holdem-hu`
            # saturates, while `plo5-hu` and `plo4-3way` cross the FIFO baseline
            # and spend the saving back. Multipliers the solver would refuse are
            # dropped rather than asked for -- see cap_curve_multiples.
            caps = []
            for multiple in cap_curve_multiples(cap):
                run = measure(binary, spot, args, seeds[0],
                              "uncertainty-aware", cap * multiple,
                              args.br_samples, out_json)
                caps.append("%dx %s" % (multiple, "{:,}".format(run["evals"])))
            print("%-11s %-17s %s  (fifo at 1x: %s)"
                  % (spot[0], "cap curve", "  ".join(caps),
                     "{:,}".format(baseline[0]["evals"])))
            print()


if __name__ == "__main__":
    main()
