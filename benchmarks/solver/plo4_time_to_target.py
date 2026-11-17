#!/usr/bin/env python3
"""Time-to-target exploitability probe for the PLO4 heads-up river spots.

Issue #284: iterations per second is not a quality metric. This probe holds the
game, the ranges, the betting tree and the best-response evaluator constant and
measures the wall-clock time each method needs to reach a fixed exploitability
target, or its final exploitability at a fixed horizon.

Runs use the lane A driver `pe-plo4-lane-a`: the v3 vector solver updates the
regrets, and every convergence measurement is the exact best response of the
vector lane (`br_mode=exact`, `guarantee=nash`). Nothing is sampled, so a
target is a measured value and not an estimate.

Two axes are compared, on every selected spot:

* **algorithm** — `cfr`, `cfr+`, `cfr+-chance` and `dcfr` (the v3 presets the
  lane accepts);
* **DCFR schedule** — fixed exponents (the control) and the training-free
  HS-DCFR(15)/HS-DCFR(30) schedules of Zhang et al. (AAAI 2026).

A preset the lane rejects for a spot is recorded as unavailable for that spot,
with the driver's own error, instead of aborting the campaign. The chance
sampling traversal needs a game with direct chance sampling, which a river spot
does not have, so `cfr+-chance` is expected to be unavailable there.

What it records, per (spot, method, target, seed), or per (spot, method, seed)
in fixed-horizon mode:

* wall-clock seconds for the whole process, separately from the solve quality
  fields the report carries (`measurement_iteration`, `br_mode`, `guarantee`);
* the solver checkpoint history with its timing decomposition, and the
  checkpoint bracket around the target;
* the exploitability in mbb/game and whether the target was reached;
* the solver-managed memory (`storage_bytes` and `infosets`), the peak RSS
  of the process where the platform exposes it, the resolved precision and the
  range mode (`sampled` or `enumerated`);
* for DCFR, the exponents the schedule resolved at every checkpoint, so the
  parameter trajectory is recorded rather than only the schedule name;
* the stopping reason, derived from the solver's own progress booleans;
* the seed, so repeated runs are reproducible.

Targets that no seed reached are marked, never interpolated. A campaign whose
seeds changed nothing while the range was sampled is flagged
(`diagnostics.seeds_are_inert`): repeated seeds are then a repetition check, not
a distribution. An enumerated range is seed-independent by construction and is
never flagged. A campaign whose every method measured the same thing is flagged
as `diagnostics.methods_are_inert`, because that means the algorithm axis is
not applied; methods that are identical by construction are listed separately in
`diagnostics.identical_method_groups`.

With --fixed-horizon, the same seeded runs stop at --iterations even when a
target would have stopped them earlier. The report then summarizes final
exploitability and the separate solve/BR timing per method, which makes a
common-iteration comparison possible without target-detection latency.

With --omit-checkpoints the per-run checkpoint histories are left out of the
written document; the summary and every per-run metric stay. The summary and the
diagnostics are still computed from the full history, so a checked-in baseline
can be review-sized without losing the evidence it is quoted for.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

SCHEMA = "pe-plo4-time-to-target/v6"
GAME = "plo4"
BIG_BLIND = 2.0
FIRST_TO_ACT = 1
COMBOS = 512
DEFAULT_SEEDS = (11, 23, 37)
DEFAULT_TARGETS = (1000.0, 500.0, 250.0)
DEFAULT_ITERATIONS = 300
DEFAULT_INTERVAL = 10
PREFLIGHT_ITERATIONS = 2

# Fixed heads-up river spots. Each one declares the whole game: board, pot,
# stacks, the single bet size and how it was chosen.
SPOTS: tuple[dict[str, Any], ...] = (
    {
        "id": "k9_certified",
        "board": "Ks9d4c2hTs",
        "pot": 63.0,
        "stack": 68.5,
        "bet_size": 63.0,
        "note": "the spot certified by the #283 river oracle and BR work",
    },
    {
        "id": "a7_dry",
        "board": "As7c5d3h2s",
        "pot": 60.0,
        "stack": 75.0,
        "bet_size": 45.0,
        "note": "rainbow, no straight or flush possible",
    },
    {
        "id": "qj_wet",
        "board": "QhJhTh9c8d",
        "pot": 72.0,
        "stack": 90.0,
        "bet_size": 72.0,
        "note": "four to a straight, three hearts on board",
    },
    {
        "id": "77_paired",
        "board": "7d7h2c2sKd",
        "pot": 55.0,
        "stack": 60.0,
        "bet_size": 55.0,
        "note": "double-paired board, few nut combinations",
    },
    {
        "id": "t9_coordinated",
        "board": "Ts9s8d7d6h",
        "pot": 80.0,
        "stack": 100.0,
        "bet_size": 60.0,
        "note": "five-card straight on board and two flush suits",
    },
)

# The compared methods. `schedule` is the DCFR schedule flag and only applies to
# the DCFR presets; the other presets ignore it.
METHODS: tuple[dict[str, Any], ...] = (
    {"method": "cfr", "algorithm": "cfr", "schedule": None, "role": "reference"},
    {"method": "cfr_plus", "algorithm": "cfr+", "schedule": None, "role": "reference"},
    {
        "method": "cfr_plus_chance",
        "algorithm": "cfr+-chance",
        "schedule": None,
        "role": "reference",
    },
    {"method": "dcfr", "algorithm": "dcfr", "schedule": "fixed", "role": "control"},
    {
        "method": "dcfr_hs15",
        "algorithm": "dcfr",
        "schedule": "hs15",
        "role": "scheduled",
    },
    {
        "method": "dcfr_hs30",
        "algorithm": "dcfr",
        "schedule": "hs30",
        "role": "scheduled",
    },
)

METHOD_LABELS = tuple(entry["method"] for entry in METHODS)

# Methods the lane cannot run under matched game semantics. They are recorded
# as unsupported so a reader cannot mistake the compared lanes for a survey.
UNSUPPORTED_METHODS = (
    {
        "method": "hs_pcfr_plus",
        "reason": (
            "PCFR+ is not implemented in the lane A solver, so its update and "
            "averaging semantics cannot be matched to the AAAI 2026 "
            "schedules. It is not a parameter-only change and stays "
            "unsupported."
        ),
    },
    {
        "method": "deep_predictive_cfr",
        "reason": (
            "Neural predictive CFR is not implemented in this lane. Its "
            "reported match reward is not a PLO exploitability certificate, "
            "and its sampling, replay-buffer and training costs are not "
            "comparable with a tabular CFR solve."
        ),
    },
    {
        "method": "monker_import",
        "reason": (
            "Imported commercial-solver strategies are excluded: class "
            "mapping, range/board/tree equivalence and quantized action "
            "probabilities have not been validated, so they are not a "
            "reproduced benchmark."
        ),
    },
)


def spot_ids() -> tuple[str, ...]:
    return tuple(spot["id"] for spot in SPOTS)


def spot_by_id(identifier: str) -> dict[str, Any]:
    for spot in SPOTS:
        if spot["id"] == identifier:
            return spot
    raise KeyError(identifier)


def parse_spots(text: str) -> list[str]:
    """Parse a comma-separated spot selection; unknown ids are rejected."""
    if text == "all":
        return list(spot_ids())
    identifiers = [part.strip() for part in text.split(",") if part.strip()]
    if not identifiers:
        raise argparse.ArgumentTypeError("at least one spot id is required")
    for identifier in identifiers:
        if identifier not in spot_ids():
            raise argparse.ArgumentTypeError(
                f"unknown spot {identifier!r}; known: {', '.join(spot_ids())}"
            )
    return identifiers


def build_command(
    solver: Path,
    spot: dict[str, Any],
    method: dict[str, Any],
    seed: int,
    target_mbb: float | None,
    iterations: int,
    interval: int,
    combos: int,
    output: Path,
) -> list[str]:
    command = [
        str(solver),
        "--board", spot["board"],
        "--pot", f"{spot['pot']:.17g}",
        "--stack", f"{spot['stack']:.17g}",
        "--bet-size", f"{spot['bet_size']:.17g}",
        "--big-blind", f"{BIG_BLIND:.17g}",
        "--combos", str(combos),
        "--first-to-act", str(FIRST_TO_ACT),
        "--iterations", str(iterations),
        "--exploitability-interval", str(interval),
    ]
    if target_mbb is not None:
        command += ["--target-mbb", f"{target_mbb:.17g}"]
    command += ["--algorithm", method["algorithm"]]
    if method["schedule"] is not None:
        command += ["--dcfr-schedule", method["schedule"]]
    command += ["--seed", str(seed), "--output", str(output)]
    return command


def stopping_reason(progress: dict[str, Any], target_mbb: float | None) -> str:
    """Classify how the run ended from the solver's own progress booleans."""
    if target_mbb is not None and progress.get("target_reached"):
        return "target_reached"
    if progress.get("complete"):
        return "iteration_budget"
    return "incomplete"


def _stats(values: list[Any]) -> dict[str, float | None]:
    """Median/min/max over the present values; all None when nothing is known."""
    present = [float(value) for value in values if value is not None]
    if not present:
        return {"median": None, "min": None, "max": None}
    return {
        "median": statistics.median(present),
        "min": min(present),
        "max": max(present),
    }


def target_bracket(
    checkpoints: list[dict[str, Any]], target_mbb: float
) -> dict[str, Any]:
    """Bracket one target with the last checkpoint above and first below it.

    The measurement interval means the exact crossing time is unknown; these
    two checkpoints are the honest bounds, not an interpolated time. The solve
    and certification (best-response) components are kept, so the crossing
    time can be read with its certification overhead separated out.
    """
    last_above: dict[str, Any] | None = None
    for checkpoint in checkpoints:
        entry = {
            "iteration": checkpoint.get("iteration"),
            "elapsed_seconds": checkpoint.get("elapsed_seconds"),
            "solve_seconds": checkpoint.get("solve_seconds"),
            "br_seconds": checkpoint.get("br_seconds"),
            "alpha": checkpoint.get("alpha"),
            "beta": checkpoint.get("beta"),
            "gamma": checkpoint.get("gamma"),
            "exploitability_mbb_per_game": checkpoint.get(
                "exploitability_mbb_per_game"
            ),
        }
        if float(entry["exploitability_mbb_per_game"]) <= target_mbb:
            return {
                "target_mbb": target_mbb,
                "last_above": last_above,
                "first_below": entry,
                "bracketed": True,
            }
        last_above = entry
    return {
        "target_mbb": target_mbb,
        "last_above": last_above,
        "first_below": None,
        "bracketed": False,
    }


def preflight(
    solver: Path, spot: dict[str, Any], method: dict[str, Any], workdir: Path
) -> str | None:
    """Return the driver's error when a method cannot run this spot at all.

    A method is a preset, and a preset can require a game capability the spot
    does not have — chance sampling needs chance nodes, which a river spot has
    none of. The check runs the driver for a couple of iterations so a
    structurally unsupported preset is reported instead of aborting the whole
    campaign halfway through.
    """
    output = workdir / f"preflight-{spot['id']}-{method['method']}.json"
    command = build_command(
        solver, spot, method, seed=1, target_mbb=None,
        iterations=PREFLIGHT_ITERATIONS, interval=PREFLIGHT_ITERATIONS,
        combos=32, output=output,
    )
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode == 0:
        return None
    return (
        f"pe-plo4-lane-a exited with {completed.returncode}: "
        f"{completed.stderr.strip() or 'no diagnostic'}"
    )


def run_once(
    solver: Path,
    spot: dict[str, Any],
    method: dict[str, Any],
    seed: int,
    target_mbb: float | None,
    iterations: int,
    interval: int,
    combos: int,
    workdir: Path,
) -> dict[str, Any]:
    label = "fixed" if target_mbb is None else f"{int(target_mbb)}"
    output = workdir / (
        f"report-{spot['id']}-{method['method']}-{label}-{seed}.json"
    )
    command = build_command(
        solver, spot, method, seed, target_mbb, iterations, interval, combos,
        output,
    )
    started = time.perf_counter()
    completed = subprocess.run(
        command, capture_output=True, text=True, check=False
    )
    elapsed = time.perf_counter() - started
    if completed.returncode != 0:
        raise RuntimeError(
            f"pe-plo4-lane-a exited with {completed.returncode} for "
            f"{spot['id']}/{method['method']} target {target_mbb}: "
            f"{completed.stderr.strip()}"
        )
    with open(output, encoding="utf-8") as report_file:
        report = json.load(report_file)

    metrics = report.get("metrics", {})
    exploitability = metrics.get("exploitability_mbb_per_game")
    if exploitability is None:
        raise RuntimeError(
            f"the {spot['id']}/{method['method']} run reported no exploitability"
        )
    progress = report.get("progress", {})
    checkpoints = report.get("checkpoints", [])
    dcfr = method["algorithm"] == "dcfr"
    return {
        "spot": spot["id"],
        "board": spot["board"],
        "method": method["method"],
        "algorithm": report.get("algorithm") or method["algorithm"],
        "schedule": method["schedule"],
        "role": method["role"],
        "seed": seed,
        "target_mbb": target_mbb,
        "requested_schedule": report.get("dcfr_schedule") if dcfr else None,
        "horizon": report.get("dcfr_horizon") if dcfr else None,
        "dcfr_params": report.get("dcfr_params") if dcfr else None,
        "precision": report.get("precision"),
        "range_mode": report.get("range_mode"),
        "seconds": elapsed,
        "complete": bool(progress.get("complete")),
        "reached": (
            bool(progress.get("target_reached"))
            if target_mbb is not None else None
        ),
        "stopping_reason": stopping_reason(progress, target_mbb),
        "exploitability_mbb_per_game": float(exploitability),
        "measurement_iteration": metrics.get("measurement_iteration"),
        "iterations_run": progress.get("iteration"),
        "br_mode": metrics.get("br_mode"),
        "guarantee": metrics.get("guarantee"),
        "infosets": metrics.get("infosets"),
        "storage_bytes": metrics.get("storage_bytes"),
        "peak_rss_bytes": report.get("peak_rss_bytes"),
        "timing": report.get("timing", {}),
        "checkpoints": checkpoints,
        "target_bracket": (
            target_bracket(checkpoints, target_mbb)
            if target_mbb is not None else None
        ),
    }


def _median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def _format_optional(value: float | None, digits: int) -> str:
    return f"{value:.{digits}f}" if value is not None else "-"


def _mib(value: float | None) -> str:
    return f"{value / (1024.0 * 1024.0):.1f}" if value is not None else "-"


def summarize(
    runs: list[dict[str, Any]], spots: list[str], targets: list[float]
) -> dict[str, Any]:
    """Group runs into per-spot, per-method, per-target medians and spreads.

    A target counts as reached only when every seed reached it; the individual
    reached counts and times are kept so a partial result cannot look complete.
    """
    summary: dict[str, Any] = {}
    for spot in spots:
        by_method: dict[str, Any] = {}
        for method in METHOD_LABELS:
            by_target: dict[str, Any] = {}
            for target in targets:
                matching = [
                    run for run in runs
                    if run["spot"] == spot
                    and run["method"] == method
                    and run["target_mbb"] == target
                ]
                if not matching:
                    continue
                reached = [run for run in matching if run["reached"]]
                times = [run["seconds"] for run in reached]
                iterations = [
                    run["measurement_iteration"] for run in reached
                    if run["measurement_iteration"] is not None
                ]
                first_below = [
                    run["target_bracket"]["first_below"] for run in reached
                    if run.get("target_bracket", {}).get("first_below")
                ]
                by_target[f"{target:.17g}"] = {
                    "runs": len(matching),
                    "reached": len(reached),
                    "all_reached": len(reached) == len(matching),
                    "median_seconds": _median(times),
                    "min_seconds": min(times) if times else None,
                    "max_seconds": max(times) if times else None,
                    "median_measurement_iteration": _median(
                        [float(value) for value in iterations]
                    ),
                    "median_seconds_to_first_below": _median(
                        [float(entry["elapsed_seconds"]) for entry in first_below]
                    ),
                    "median_solve_seconds_to_first_below": _median(
                        [
                            float(entry["solve_seconds"]) for entry in first_below
                            if entry.get("solve_seconds") is not None
                        ]
                    ),
                    "median_br_seconds_to_first_below": _median(
                        [
                            float(entry["br_seconds"]) for entry in first_below
                            if entry.get("br_seconds") is not None
                        ]
                    ),
                    "median_iteration_to_first_below": _median(
                        [float(entry["iteration"]) for entry in first_below]
                    ),
                    "exploitability_mbb_per_game": [
                        run["exploitability_mbb_per_game"] for run in matching
                    ],
                    # Memory is measured for every run of the target, reached
                    # or not: a process peak does not depend on hitting it.
                    "peak_rss_bytes": _stats(
                        [run.get("peak_rss_bytes") for run in matching]
                    ),
                    "solver_storage_bytes": _stats(
                        [run.get("storage_bytes") for run in matching]
                    ),
                    "infosets": _stats(
                        [run.get("infosets") for run in matching]
                    ),
                    "stopping_reasons": sorted(
                        {run.get("stopping_reason") for run in matching} - {None}
                    ),
                }
            if by_target:
                by_method[method] = by_target
        summary[spot] = by_method
    return summary


def summarize_fixed(
    runs: list[dict[str, Any]], spots: list[str]
) -> dict[str, Any]:
    """Summarize a fixed-horizon campaign per spot and method.

    Every run is expected to stop at the same iteration cap rather than at a
    target. The final checkpoint supplies the solver-only timing, so the exact
    best-response cost is kept separate from the per-iteration solve cost.
    """
    summary: dict[str, Any] = {}
    for spot in spots:
        by_method: dict[str, Any] = {}
        for method in METHOD_LABELS:
            matching = [
                run for run in runs
                if run["spot"] == spot and run["method"] == method
            ]
            if not matching:
                continue
            completed = [run for run in matching if run.get("complete")]
            measured: list[tuple[dict[str, Any], dict[str, Any]]] = []
            for run in completed:
                checkpoints = run.get("checkpoints") or []
                if not checkpoints:
                    continue
                checkpoint = checkpoints[-1]
                if int(checkpoint.get("iteration", 0)) <= 0:
                    continue
                measured.append((run, checkpoint))

            walls = [float(run["seconds"]) for run, _ in measured]
            exploitations = [
                float(checkpoint["exploitability_mbb_per_game"])
                for _, checkpoint in measured
            ]
            iterations = [
                float(checkpoint["iteration"]) for _, checkpoint in measured
            ]
            solves = [
                float(checkpoint["solve_seconds"]) for _, checkpoint in measured
            ]
            elapsed = [
                float(checkpoint["elapsed_seconds"]) for _, checkpoint in measured
            ]
            solve_per_iteration = [
                solve / iteration * 1000.0
                for solve, iteration in zip(solves, iterations)
            ]
            br_seconds = [
                max(0.0, total - solve) for total, solve in zip(elapsed, solves)
            ]

            by_method[method] = {
                "algorithm": matching[0]["algorithm"],
                "schedule": matching[0]["schedule"],
                "role": matching[0]["role"],
                "runs": len(matching),
                "completed": len(completed),
                "all_completed": len(completed) == len(matching),
                "median_seconds": _median(walls),
                "min_seconds": min(walls) if walls else None,
                "max_seconds": max(walls) if walls else None,
                "median_exploitability_mbb_per_game": _median(exploitations),
                "min_exploitability_mbb_per_game":
                    min(exploitations) if exploitations else None,
                "max_exploitability_mbb_per_game":
                    max(exploitations) if exploitations else None,
                "median_final_iteration": _median(iterations),
                "median_solve_seconds": _median(solves),
                "median_solve_ms_per_iteration":
                    _median(solve_per_iteration),
                "median_br_seconds": _median(br_seconds),
                "final_params": (
                    {
                        key: measured[0][0].get("dcfr_params", {}).get(key)
                        for key in ("alpha", "beta", "gamma")
                    } if measured and measured[0][0].get("dcfr_params") else None
                ),
                "peak_rss_bytes": _stats(
                    [run.get("peak_rss_bytes") for run in matching]
                ),
                "solver_storage_bytes": _stats(
                    [run.get("storage_bytes") for run in matching]
                ),
                "infosets": _stats(
                    [run.get("infosets") for run in matching]
                ),
                "stopping_reasons": sorted(
                    {run.get("stopping_reason") for run in matching} - {None}
                ),
            }
        summary[spot] = by_method
    return summary


def methods_are_inert(runs: list[dict[str, Any]]) -> bool:
    """True when every available method measured the same thing.

    This happens on a driver that applies one update rule regardless of the
    preset: the algorithm axis is then recorded but has no effect, and the
    comparison would be meaningless. Only spots where every compared method
    actually ran are considered.
    """
    available: dict[str, set[str]] = {}
    grouped: dict[tuple[str, Any, int], list[dict[str, Any]]] = {}
    for run in runs:
        available.setdefault(run["spot"], set()).add(run["method"])
        grouped.setdefault(
            (run["spot"], run["target_mbb"], run["seed"]), []
        ).append(run)
    complete = [
        group for (spot, _, _), group in grouped.items()
        if {run["method"] for run in group} == available[spot]
    ]
    if not complete or any(len(methods) < 2 for methods in available.values()):
        return False
    return all(
        len({
            (run["exploitability_mbb_per_game"], run["measurement_iteration"])
            for run in group
        }) == 1
        for group in complete
    )


def identical_method_groups(runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """List methods that measured the same exploitability on every seed.

    Equality is over the whole per-seed vector, not a rounded median, so this
    only reports methods that are identical by construction (for example two
    traversals of the same update rule).
    """
    by_spot_target: dict[tuple[str, Any], dict[str, list[float]]] = {}
    for run in runs:
        by_spot_target.setdefault(
            (run["spot"], run["target_mbb"]), {}
        ).setdefault(run["method"], []).append(run["exploitability_mbb_per_game"])
    groups: list[dict[str, Any]] = []
    for (spot, target), per_method in sorted(
        by_spot_target.items(), key=lambda item: (item[0][0], str(item[0][1]))
    ):
        buckets: dict[tuple[float, ...], list[str]] = {}
        for method, values in per_method.items():
            buckets.setdefault(tuple(sorted(values)), []).append(method)
        for methods in buckets.values():
            if len(methods) > 1:
                groups.append({
                    "spot": spot,
                    "target_mbb": target,
                    "methods": sorted(methods),
                })
    return groups


def seeds_are_inert(runs: list[dict[str, Any]]) -> bool:
    """True when every seed measured the same exploitability on a sampled range.

    A fully enumerated range is seed-independent by construction, so identical
    rows are expected there and never flagged. A sampled range that reports one
    number for every seed is the failure mode this catches: `--seed` never
    reached the range sample, so the reported spread would be invented. At
    least two seeds of the same spot, method and target are required before the
    claim means anything.
    """
    grouped: dict[tuple[str, str, Any], list[dict[str, Any]]] = {}
    for run in runs:
        grouped.setdefault(
            (run["spot"], run["method"], run["target_mbb"]), []
        ).append(run)
    for group in grouped.values():
        if len({run.get("seed") for run in group}) < 2:
            continue
        if not all(run.get("range_mode") == "sampled" for run in group):
            continue
        if len({run["exploitability_mbb_per_game"] for run in group}) == 1:
            return True
    return False


def render_fixed_markdown(
    summary: dict[str, Any], spots: list[str]
) -> str:
    lines: list[str] = []
    for spot in spots:
        if spot not in summary:
            continue
        lines.append(f"### {spot}")
        lines.append(
            "| method | role | completed | final iter | median exploitability "
            "| min | max | solve ms/iter | solve s | BR s | wall s | peak MiB |"
        )
        lines.append(
            "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
        )
        for method, entry in summary[spot].items():
            final_iteration = entry["median_final_iteration"]
            lines.append(
                "| {method} | {role} | {completed}/{runs} | {iteration} | "
                "{expl} | {emin} | {emax} | {solve_iter} | {solve} | {br} | "
                "{wall} | {peak} |".format(
                    method=method,
                    role=entry["role"],
                    completed=entry["completed"],
                    runs=entry["runs"],
                    iteration=(
                        f"{final_iteration:.0f}"
                        if final_iteration is not None else "-"
                    ),
                    expl=_format_optional(
                        entry["median_exploitability_mbb_per_game"], 3
                    ),
                    emin=_format_optional(
                        entry["min_exploitability_mbb_per_game"], 3
                    ),
                    emax=_format_optional(
                        entry["max_exploitability_mbb_per_game"], 3
                    ),
                    solve_iter=_format_optional(
                        entry["median_solve_ms_per_iteration"], 3
                    ),
                    solve=_format_optional(entry["median_solve_seconds"], 3),
                    br=_format_optional(entry["median_br_seconds"], 3),
                    wall=_format_optional(entry["median_seconds"], 3),
                    peak=_mib(entry.get("peak_rss_bytes", {}).get("max")),
                )
            )
        lines.append("")
    return "\n".join(lines).rstrip()


def render_markdown(
    summary: dict[str, Any], spots: list[str], targets: list[float]
) -> str:
    """Render the time-to-target tables with certification and memory cost.

    "unreached" marks a target no seed reached; the certification (BR) and
    solver (solve) seconds are the components of the crossing time, and the
    peak RSS is the process high-water mark of the same runs.
    """
    lines: list[str] = []
    for spot in spots:
        if spot not in summary:
            continue
        lines.append(f"### {spot}")
        lines.append(
            "| target (mbb/game) | method | reached | median s | min s | max s "
            "| solve s | BR s | peak MiB |"
        )
        lines.append("|---:|---|---:|---:|---:|---:|---:|---:|---:|")
        for target in targets:
            key = f"{target:.17g}"
            for method, by_target in summary[spot].items():
                entry = by_target.get(key)
                if entry is None:
                    continue
                median = entry["median_seconds"]
                lines.append(
                    "| {target:.0f} | {method} | {reached}/{runs} | {median} | "
                    "{minimum} | {maximum} | {solve} | {br} | {peak} |".format(
                        target=target,
                        method=method,
                        reached=entry["reached"],
                        runs=entry["runs"],
                        median=(
                            f"{median:.3f}" if median is not None
                            else "unreached"
                        ),
                        minimum=(
                            f"{entry['min_seconds']:.3f}"
                            if entry["min_seconds"] is not None else "-"
                        ),
                        maximum=(
                            f"{entry['max_seconds']:.3f}"
                            if entry["max_seconds"] is not None else "-"
                        ),
                        solve=_format_optional(
                            entry.get("median_solve_seconds_to_first_below"), 3
                        ),
                        br=_format_optional(
                            entry.get("median_br_seconds_to_first_below"), 3
                        ),
                        peak=_mib(
                            entry.get("peak_rss_bytes", {}).get("max")
                        ),
                    )
                )
        lines.append("")
    return "\n".join(lines).rstrip()


def cpu_model() -> str | None:
    """Best-effort CPU model name; None when the platform does not expose it."""
    system = platform.system()
    try:
        if system == "Darwin":
            completed = subprocess.run(
                ["sysctl", "-n", "machdep.cpu.brand_string"],
                capture_output=True, text=True, check=False, timeout=5,
            )
            return completed.stdout.strip() or None
        if system == "Linux":
            text = Path("/proc/cpuinfo").read_text(
                encoding="utf-8", errors="replace"
            )
            for line in text.splitlines():
                if line.lower().startswith("model name"):
                    return line.split(":", 1)[-1].strip() or None
            return None
        if system == "Windows":
            return os.environ.get("PROCESSOR_IDENTIFIER", "").strip() or None
    except (OSError, subprocess.SubprocessError):
        return None
    return None


def parse_targets(text: str) -> list[float]:
    targets = sorted({float(part) for part in text.split(",")}, reverse=True)
    if not targets or any(target <= 0.0 for target in targets):
        raise argparse.ArgumentTypeError("targets must be positive mbb/game values")
    return targets


def parse_seeds(text: str) -> list[int]:
    seeds = [int(part) for part in text.split(",")]
    if not seeds or any(seed < 0 for seed in seeds):
        raise argparse.ArgumentTypeError("seeds must be non-negative integers")
    return seeds


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--solver", default="build/local/tools/pe-plo4-lane-a",
        help="path to pe-plo4-lane-a",
    )
    parser.add_argument(
        "--spot", type=parse_spots, default=list(spot_ids()),
        help=(
            "comma-separated spot ids, or 'all' (default); "
            f"known: {', '.join(spot_ids())}"
        ),
    )
    parser.add_argument(
        "--list-spots", action="store_true",
        help="print the built-in spots and exit",
    )
    parser.add_argument(
        "--iterations", type=int, default=DEFAULT_ITERATIONS,
        help="iteration cap; exact horizon with --fixed-horizon",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--fixed-horizon", action="store_true",
        help="run every method to --iterations without stopping on a target",
    )
    mode.add_argument(
        "--targets", type=parse_targets, default=DEFAULT_TARGETS,
        help="comma-separated exploitability targets in mbb/game",
    )
    parser.add_argument(
        "--seeds", type=parse_seeds, default=DEFAULT_SEEDS,
        help="comma-separated seeds",
    )
    parser.add_argument(
        "--combos", type=int, default=COMBOS,
        help="combos per range (exact enumeration or deterministic sample)",
    )
    parser.add_argument(
        "--interval", type=int, default=DEFAULT_INTERVAL,
        help="iterations between exact-BR measurements",
    )
    parser.add_argument(
        "--omit-checkpoints", action="store_true",
        help=(
            "write summaries and per-run metrics without the per-run "
            "checkpoint histories"
        ),
    )
    parser.add_argument(
        "--output", default=None,
        help="JSON destination",
    )
    options = parser.parse_args(argv[1:])

    if options.list_spots:
        for spot in SPOTS:
            print(
                f"{spot['id']:<16} {spot['board']}  pot {spot['pot']:g}  "
                f"stack {spot['stack']:g}  bet {spot['bet_size']:g}  "
                f"({spot['note']})"
            )
        return 0
    if options.iterations <= 0:
        parser.error("--iterations must be positive")
    if options.interval <= 0:
        parser.error("--interval must be positive")
    fixed_horizon = options.fixed_horizon
    if fixed_horizon and options.iterations % options.interval != 0:
        parser.error(
            "--iterations must be a multiple of --interval in fixed-horizon mode"
        )
    output = Path(
        options.output
        or (
            "build/plo4-fixed-horizon.json"
            if fixed_horizon else "build/plo4-time-to-target.json"
        )
    )

    solver = Path(options.solver)
    if not solver.is_file():
        print(f"missing solver: {solver}", file=sys.stderr)
        return 2
    spots = options.spot
    for identifier in spots:
        spot = spot_by_id(identifier)
        board = str(spot["board"])
        if len(board) != 10 or len(set(board[i:i + 2] for i in range(0, 10, 2))) != 5:
            print(
                f"spot {identifier}: board must hold five distinct cards",
                file=sys.stderr,
            )
            return 2
        if spot["bet_size"] > spot["stack"]:
            print(
                f"spot {identifier}: bet size exceeds the remaining stack",
                file=sys.stderr,
            )
            return 2
    output.parent.mkdir(parents=True, exist_ok=True)

    runs: list[dict[str, Any]] = []
    unavailable: dict[str, dict[str, str]] = {}
    targets: list[float | None] = (
        [None] if fixed_horizon else list(options.targets)
    )
    # The per-run solver reports are intermediates, not artifacts. Keeping them
    # in a scratch directory means writing a baseline straight into the
    # repository does not litter it with one JSON per run.
    with tempfile.TemporaryDirectory(prefix="pe-plo4-ttt-") as scratch:
        workdir = Path(scratch)
        for identifier in spots:
            spot = spot_by_id(identifier)
            for method in METHODS:
                error = preflight(solver, spot, method, workdir)
                if error is not None:
                    unavailable.setdefault(identifier, {})[
                        method["method"]
                    ] = error
                    print(
                        f"skipping {identifier}/{method['method']}: {error}",
                        file=sys.stderr,
                    )
                    continue
                for target in targets:
                    for seed in options.seeds:
                        try:
                            runs.append(
                                run_once(
                                    solver, spot, method, seed, target,
                                    options.iterations, options.interval,
                                    options.combos, workdir,
                                )
                            )
                        except RuntimeError as runtime_error:
                            print(str(runtime_error), file=sys.stderr)
                            return 1

    if not runs:
        print("no method could run the selected spots", file=sys.stderr)
        return 1

    if fixed_horizon:
        missing = [
            run for run in runs
            if (
                not run.get("complete")
                or not run.get("checkpoints")
                or int(run["checkpoints"][-1].get("iteration", 0))
                != options.iterations
            )
        ]
        if missing:
            print(
                "fixed-horizon run did not produce a final checkpoint at the "
                "horizon",
                file=sys.stderr,
            )
            return 1

    summary = (
        summarize_fixed(runs, spots) if fixed_horizon
        else summarize(runs, spots, options.targets)
    )
    inert = methods_are_inert(runs)
    seeds_inert = seeds_are_inert(runs)
    identical = identical_method_groups(runs)
    document = {
        "schema": SCHEMA,
        "issue": 284,
        "campaign_mode": "fixed-horizon" if fixed_horizon else "time-to-target",
        "spots": [
            {key: spot[key] for key in (
                "id", "board", "pot", "stack", "bet_size", "note"
            )}
            for spot in (spot_by_id(identifier) for identifier in spots)
        ],
        "game": {
            "game": GAME,
            "big_blind": BIG_BLIND,
            "first_to_act": FIRST_TO_ACT,
            "combos": options.combos,
            "iterations_cap": options.iterations,
            "fixed_horizon": fixed_horizon,
            "exploitability_interval": options.interval,
            "targets_mbb_per_game": [] if fixed_horizon else options.targets,
            "seeds": options.seeds,
        },
        "environment": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "processor": platform.processor() or None,
            "cpu_model": cpu_model(),
            "logical_cores": os.cpu_count(),
            "python": platform.python_version(),
            "note": (
                "Wall clocks and peak RSS are machine-specific evidence, not "
                "a CI speed gate."
            ),
        },
        "methods": {
            "compared": [
                {
                    "method": entry["method"],
                    "algorithm": entry["algorithm"],
                    "schedule": entry["schedule"],
                    "role": entry["role"],
                }
                for entry in METHODS
            ],
            "quality_definition": (
                "exploitability_mbb_per_game: exploitability (half the raw "
                "NashConv) in mbb per hand of the declared big blind, from the "
                "same exact best-response evaluator for every method"
            ),
            "unavailable": unavailable,
            "unsupported": list(UNSUPPORTED_METHODS),
        },
        "br_quality": {
            "mode": sorted({run["br_mode"] for run in runs}),
            "guarantee": sorted({run["guarantee"] for run in runs}),
            "note": (
                "Lane A measures exact best responses; the fixed horizon is "
                "an iteration count, not a reached target."
                if fixed_horizon else
                "Lane A measures exact best responses; a reached target is a "
                "measured value, not a sampled estimate."
            ),
        },
        "diagnostics": {
            "methods_are_inert": inert,
            "seeds_are_inert": seeds_inert,
            "identical_method_groups": identical,
            "methods_note": (
                "True means every available method produced the same "
                "measurement at the same iteration: the driver applies one "
                "update rule and the algorithm axis is not a comparison."
            ) if inert else (
                "Methods produced different measurements; the summary is a "
                "comparison. `identical_method_groups` lists methods that "
                "agreed on every seed, which is expected when two presets "
                "share an update rule on a spot without chance nodes."
            ),
            "seeds_note": (
                "True means every seed measured the same exploitability "
                "while the range was sampled: --seed never reached the range "
                "sample, so the per-seed spread is a repetition check, not a "
                "distribution."
            ) if seeds_inert else (
                "No seed group reported one measurement across a sampled "
                "range; an enumerated range is seed-independent and is not "
                "flagged."
            ),
        },
        "runs": runs,
        "summary": summary,
        "checkpoints_omitted": options.omit_checkpoints,
    }
    if options.omit_checkpoints:
        # The summary and the diagnostics above were computed from the full
        # histories; only the written copy drops them.
        for run in document["runs"]:
            run.pop("checkpoints", None)
    with open(output, "w", encoding="utf-8") as output_file:
        json.dump(document, output_file, indent=2, sort_keys=True)
        output_file.write("\n")

    print(
        render_fixed_markdown(summary, spots)
        if fixed_horizon else render_markdown(summary, spots, options.targets)
    )
    print(f"wrote {output}")
    if inert:
        print(
            "error: every method produced the same measurement; this driver "
            "applied a single update rule, so the rows above are not an "
            "algorithm comparison",
            file=sys.stderr,
        )
    if seeds_inert:
        print(
            "error: every seed measured the same exploitability on a sampled "
            "range; --seed did not reach the range sample, so the per-seed "
            "spread is not a distribution",
            file=sys.stderr,
        )
    if inert or seeds_inert:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
