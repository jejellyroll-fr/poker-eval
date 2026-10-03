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
rather than assuming it -- see `assert_same_training`.

Each spot is measured twice, at a fixed cap of `--br-max-samples`:

  fifo              the default: every decision may spend the full cap. The
                    historical behaviour, and the baseline.
  uncertainty-aware the cap is interpolated down the decision's priority
                    bucket, so a settled decision is capped near
                    `--br-min-samples` and a fragile one keeps the maximum.

Reported per spot: wall clock (machine-dependent, never a property of the
feature), the measurement's terminal evaluations and draws per decision (both
deterministic for a fixed seed), the measured exploitability, and the priority
layer's own bucket histogram -- the work distribution across infosets the issue
asks for. The comparison against the baseline is *paired*: both arms of a spot
see the same seed and therefore the same solved strategy, so the mean and
spread of the per-seed difference separate a systematic shift from the
measurement's own sampling noise.

Two diagnostics are printed under each spot, because the size of the saving is
not a constant:

  revisit curve  the saving at a tenth, a quarter and the full BR budget. Only
                 a *repeat* visit to a decision is capped: a decision below the
                 coverage floor ranks in the top bucket and keeps the full cap,
                 so a budget that visits most decisions once has almost nothing
                 to reallocate. This is why `--br-samples` defaults here to
                 20000 rather than the CLI's 2000, and the curve shows both.
  cap curve      the uncertainty-aware cost as the cap is raised. It saturates:
                 the confidence rule, not the cap, is what stops most
                 decisions, so the saving cannot be spent back on a larger cap.

    scripts/benchmarks/bench_work_priority.py --build /tmp/pe-bench
"""

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

SPOTS = [
    # name, game, players (full private ranges)
    ("holdem-hu", "holdem", 2),
    ("plo4-hu", "plo4", 2),
    ("plo5-hu", "plo5", 2),
    ("plo4-3way", "plo4", 3),
]

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
# `--br-samples 4000` moves the same counters under FIFO alone. It is listed
# rather than ignored so that the exemption stays visible and narrow.
GROWTH_FIELDS = frozenset((
    "infosets",
    "memory.total_infosets",
    "memory.adapter_bytes",
    "memory.bytes_per_infoset",
    "memory.bytes_per_strategy_slot",
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


def training_effort(text):
    """The training's own per-street counters, as a comparable tuple."""
    return EFFORT_RE.findall(text)


def strategy_grid(text):
    """The report's range grid -- the highest-frequency action per hand, which
    is the trained strategy's argmax. Coarser than the full table, but unlike
    the hand table it does not depend on the order of a storage the measurement
    grows, so it can be compared between two arms."""
    start = text.find("RANGE GRID")
    return text[start:].splitlines()[:48] if start >= 0 else []


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
           "--seed", str(seed), "--output", out_json]
    if policy is not None:
        cmd += ["--br-priority-policy", policy]
    start = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    wall = time.perf_counter() - start
    out = proc.stdout + proc.stderr
    if proc.returncode != 0:
        sys.exit("solve failed: %s\n%s" % (" ".join(cmd), out[-2000:]))
    with open(out_json) as handle:
        report = json.load(handle)
    evals = EVALS_RE.findall(out)
    decisions = DECISIONS_RE.search(out)
    if not evals or not decisions:
        sys.exit("no br_sampling/br_decisions line in: %s\n%s"
                 % (" ".join(cmd), out[-2000:]))
    return {
        "wall": wall,
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
        "grid": strategy_grid(out),
    }


def assert_same_training(spot, arm, run, baseline_run):
    """Refuse to compare two arms that did not solve the same strategy.

    Three checks, weakest to strongest evidence: the report's fields agree
    outside the measurement's own numbers and the storage it grows; the
    training's visits, updates and chance draws agree street by street; and
    the trained strategy's argmax agrees. A policy that reached the training
    would have to break all three at once to get past this."""
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
    if run["grid"] != baseline_run["grid"]:
        sys.exit("%s %s: the trained strategy's argmax differs under this "
                 "policy, so the arms are not measuring the same thing"
                 % (spot[0], arm))


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


def print_row(spot, label, runs, baseline):
    diffs = [r["nash_conv"] - b["nash_conv"] for r, b in zip(runs, baseline)]
    spread = statistics.stdev(diffs) if len(diffs) > 1 else 0.0
    print("%-11s %-17s %8.2f %12s %10s %8s %9s %14.1f %+10.1f +- %5.1f"
          % (spot[0], label, mean_of(runs, "wall"),
             "{:,}".format(int(mean_of(runs, "evals"))),
             fmt(mean_of(runs, "avg_draws")), fmt(mean_of(runs, "early")),
             "{:,}".format(int(mean_of(runs, "max_budget_hits"))),
             mean_of(runs, "nash_conv"), statistics.mean(diffs), spread))
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
                    help="draws per action for a settled decision")
    ap.add_argument("--br-max-samples", type=int, default=64,
                    help="draws per action for a fragile decision")
    ap.add_argument("--seeds", type=int, default=5)
    ap.add_argument("--spot", action="append", help="only these spots")
    args = ap.parse_args()

    binary = os.path.join(args.build, "tools", "pe-preflop-solve")
    if not os.path.exists(binary):
        sys.exit("no solver at %s" % binary)
    cap = args.br_max_samples
    spots = [s for s in SPOTS if not args.spot or s[0] in args.spot]

    print("pe-preflop-solve, external MCCFR, %d iterations, %d showdown "
          "boards, %d BR trajectories per player, cap %d..%d, %d seeds"
          % (args.iterations, args.samples, args.br_samples,
             args.br_min_samples, cap, args.seeds))
    print("Every arm of a spot solves the same strategy; the training counters "
          "and the trained argmax are checked to agree.")
    print()
    header = ("%-11s %-17s %8s %12s %10s %8s %9s %14s %19s"
              % ("spot", "policy", "wall s", "BR evals", "draws/dec",
                 "early %", "cap hits", "NashConv mBB", "vs fifo (paired)"))
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

            # Only a repeat visit is capped, so the saving tracks how often the
            # budget revisits a decision. One seed is enough to show the shape.
            revisit = []
            for budget in (args.br_samples // 10, args.br_samples // 4,
                           args.br_samples):
                fifo_run = measure(binary, spot, args, seeds[0], "fifo", cap,
                                   budget, out_json)
                aware_run = measure(binary, spot, args, seeds[0],
                                    "uncertainty-aware", cap, budget, out_json)
                revisit.append("%s %.1f%%" % ("{:,}".format(budget),
                                              saving(fifo_run["evals"],
                                                     aware_run["evals"])))
            print("%-11s %-17s %s" % (spot[0], "revisit curve",
                                      "  ".join(revisit)))

            # The saving is not spendable back through the cap: the confidence
            # rule stops most decisions long before it.
            caps = []
            for multiple in (1, 2, 4, 8, 16):
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
