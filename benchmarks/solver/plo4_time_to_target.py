#!/usr/bin/env python3
"""Time-to-target exploitability probe for a fixed PLO4 heads-up river spot.

Issue #284: iterations per second is not a quality metric. This probe holds the
game, the ranges, the betting tree, the backend, the precision and the best
response evaluator constant, and measures the wall-clock time each supported
algorithm configuration needs to reach a fixed exploitability target. It is a
measurement harness, not a solver: every run is the product-facing
`pe-preflop-solve` binary and every number comes from its native JSON report.

What it records, per (schedule, target, seed):

* wall-clock seconds for the whole process, separately from the solve quality
  fields the report carries (`measurement_iteration`, `br_mode`, `guarantee`);
* the exploitability in mbb/game and whether the target was reached;
* the solver-managed memory (`storage_bytes` and `infosets`);
* the seed actually used, so repeated stochastic runs are reproducible.

The report labels best-response quality explicitly: a sampled estimate is
`br_mode=sample`, `guarantee=empirical` and is not presented as an exact
certificate. Targets that no seed reached are marked, never interpolated.

The compared schedules are DCFR's fixed exponents (the control) and the
training-free HS-DCFR(15)/HS-DCFR(30) schedules of Zhang et al. (AAAI 2026).
"""

from __future__ import annotations

import argparse
import json
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

SCHEMA = "pe-plo4-time-to-target/v1"
GAME = "plo4"
BOARD = "Ks9d4c2hTs"
POT = 63
STREET = "river"
TREE = "examples/plo_hu_streets/plo5_hu_river.tree.json"
SCHEDULES = ("fixed", "hs15", "hs30")
DEFAULT_SEEDS = (11, 23, 37)
DEFAULT_TARGETS = (15000.0, 5000.0, 1000.0)
DEFAULT_ITERATIONS = 1000
DEFAULT_BR_SAMPLES = 16
DEFAULT_INTERVAL = 50


def build_command(
    solver: Path,
    repo_root: Path,
    schedule: str,
    seed: int,
    target_mbb: float,
    iterations: int,
    br_samples: int,
    interval: int,
    output: Path,
) -> list[str]:
    return [
        str(solver),
        "--game", GAME,
        "--players", "2",
        "--iterations", str(iterations),
        "--samples", "1",
        "--br-samples", str(br_samples),
        "--exploitability-interval", str(interval),
        "--algorithm", "external-dcfr",
        "--dcfr-schedule", schedule,
        "--backend", "cpu_ref",
        "--precision", "f64",
        "--threads", "1",
        "--target-mbb", f"{target_mbb:.17g}",
        "--seed", str(seed),
        "--max-ram", "512",
        "--desc-limit", "64",
        "--range0", "100%",
        "--range1", "100%",
        "--tree", str((repo_root / TREE).resolve()),
        "--street", STREET,
        "--board", BOARD,
        "--pot", str(POT),
        "--output", str(output),
    ]


def run_once(
    solver: Path,
    repo_root: Path,
    schedule: str,
    seed: int,
    target_mbb: float,
    iterations: int,
    br_samples: int,
    interval: int,
    workdir: Path,
) -> dict[str, Any]:
    output = workdir / f"report-{schedule}-{int(target_mbb)}-{seed}.json"
    command = build_command(
        solver, repo_root, schedule, seed, target_mbb, iterations, br_samples,
        interval, output,
    )
    started = time.perf_counter()
    completed = subprocess.run(
        command, capture_output=True, text=True, check=False
    )
    elapsed = time.perf_counter() - started
    if completed.returncode != 0:
        raise RuntimeError(
            f"pe-preflop-solve exited with {completed.returncode} for "
            f"{schedule} target {target_mbb}: {completed.stderr.strip()}"
        )
    with open(output, encoding="utf-8") as report_file:
        report = json.load(report_file)

    metrics = report.get("metrics", {})
    exploitability = metrics.get("exploitability_mbb_per_game")
    if exploitability is None:
        raise RuntimeError(f"the {schedule} run reported no exploitability")
    progress = report.get("progress", {})
    memory = report.get("memory", {})
    return {
        "schedule": schedule,
        "seed": seed,
        "target_mbb": target_mbb,
        "requested_schedule": report.get("dcfr_schedule"),
        "horizon": report.get("dcfr_horizon"),
        "seconds": elapsed,
        "reached": float(exploitability) <= target_mbb,
        "exploitability_mbb_per_game": float(exploitability),
        "measurement_iteration": metrics.get("measurement_iteration"),
        "iterations_run": progress.get("iteration"),
        "br_mode": metrics.get("br_mode"),
        "guarantee": metrics.get("guarantee"),
        "br_samples": report.get("br_samples"),
        "sample_count": metrics.get("sample_count"),
        "seed_used": metrics.get("seed"),
        "infosets": report.get("infosets"),
        "storage_bytes": memory.get("storage_bytes"),
        "adapter_bytes": memory.get("adapter_bytes"),
    }


def _median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def summarize(runs: list[dict[str, Any]], targets: list[float]) -> dict[str, Any]:
    """Group runs into per-schedule, per-target medians and spreads.

    A target counts as reached only when every seed reached it; the individual
    reached counts and times are kept so a partial result cannot look complete.
    """
    summary: dict[str, Any] = {}
    for schedule in SCHEDULES:
        by_target: dict[str, Any] = {}
        for target in targets:
            matching = [
                run for run in runs
                if run["schedule"] == schedule and run["target_mbb"] == target
            ]
            reached = [run for run in matching if run["reached"]]
            times = [run["seconds"] for run in reached]
            iterations = [
                run["measurement_iteration"] for run in reached
                if run["measurement_iteration"] is not None
            ]
            by_target[f"{target:.17g}"] = {
                "runs": len(matching),
                "reached": len(reached),
                "all_reached": bool(matching) and len(reached) == len(matching),
                "median_seconds": _median(times),
                "min_seconds": min(times) if times else None,
                "max_seconds": max(times) if times else None,
                "median_measurement_iteration": _median(
                    [float(value) for value in iterations]
                ),
                "exploitability_mbb_per_game": [
                    run["exploitability_mbb_per_game"] for run in matching
                ],
            }
        summary[schedule] = by_target
    return summary


def schedules_are_inert(runs: list[dict[str, Any]]) -> bool:
    """True when every schedule produced the same quality at the same cost.

    This happens on a driver that applies one update rule regardless of the
    preset: the schedule flag is then recorded but has no effect, and a
    time-to-target comparison between schedules would be meaningless. The
    harness reports that instead of presenting three identical rows as a
    result.
    """
    grouped: dict[tuple[float, int], list[tuple[str, float, Any]]] = {}
    for run in runs:
        key = (run["target_mbb"], run["seed"])
        grouped.setdefault(key, []).append(
            (run["schedule"], run["exploitability_mbb_per_game"],
             run["measurement_iteration"])
        )
    complete = [
        values for values in grouped.values()
        if {value[0] for value in values} == set(SCHEDULES)
    ]
    if not complete:
        return False
    return all(
        len({(value[1], value[2]) for value in values}) == 1
        for values in complete
    )


def render_markdown(summary: dict[str, Any], targets: list[float]) -> str:
    lines = [
        "| target (mbb/game) | schedule | reached | median s | min s | max s |",
        "|---:|---|---:|---:|---:|---:|",
    ]
    for target in targets:
        key = f"{target:.17g}"
        for schedule in SCHEDULES:
            entry = summary[schedule][key]
            median = entry["median_seconds"]
            lines.append(
                "| {target:.0f} | {schedule} | {reached}/{runs} | {median} | "
                "{minimum} | {maximum} |".format(
                    target=target,
                    schedule=schedule,
                    reached=entry["reached"],
                    runs=entry["runs"],
                    median=f"{median:.3f}" if median is not None else "unreached",
                    minimum=(
                        f"{entry['min_seconds']:.3f}"
                        if entry["min_seconds"] is not None else "-"
                    ),
                    maximum=(
                        f"{entry['max_seconds']:.3f}"
                        if entry["max_seconds"] is not None else "-"
                    ),
                )
            )
    return "\n".join(lines)


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
        "--solver", default="build/local/tools/pe-preflop-solve",
        help="path to pe-preflop-solve",
    )
    parser.add_argument(
        "--iterations", type=int, default=DEFAULT_ITERATIONS,
        help="iteration cap when a target is not reached",
    )
    parser.add_argument(
        "--targets", type=parse_targets, default=DEFAULT_TARGETS,
        help="comma-separated exploitability targets in mbb/game",
    )
    parser.add_argument(
        "--seeds", type=parse_seeds, default=DEFAULT_SEEDS,
        help="comma-separated seeds",
    )
    parser.add_argument(
        "--br-samples", type=int, default=DEFAULT_BR_SAMPLES,
        help="sampled BR draws per measurement",
    )
    parser.add_argument(
        "--interval", type=int, default=DEFAULT_INTERVAL,
        help="iterations between quality measurements",
    )
    parser.add_argument(
        "--output", default="build/plo4-time-to-target.json",
        help="JSON destination",
    )
    options = parser.parse_args(argv[1:])

    solver = Path(options.solver)
    if not solver.is_file():
        print(f"missing solver: {solver}", file=sys.stderr)
        return 2
    repo_root = Path(__file__).resolve().parents[2]
    workdir = Path(options.output).resolve().parent
    workdir.mkdir(parents=True, exist_ok=True)

    runs: list[dict[str, Any]] = []
    for schedule in SCHEDULES:
        for target in options.targets:
            for seed in options.seeds:
                try:
                    runs.append(
                        run_once(
                            solver, repo_root, schedule, seed, target,
                            options.iterations, options.br_samples,
                            options.interval, workdir,
                        )
                    )
                except RuntimeError as error:
                    print(str(error), file=sys.stderr)
                    return 1

    summary = summarize(runs, options.targets)
    inert = schedules_are_inert(runs)
    document = {
        "schema": SCHEMA,
        "issue": 284,
        "game": {
            "game": GAME,
            "board": BOARD,
            "pot": POT,
            "street": STREET,
            "tree": TREE,
            "ranges": ["100%", "100%"],
            "algorithm": "external-dcfr",
            "backend": "cpu_ref",
            "precision": "f64",
            "threads": 1,
            "iterations_cap": options.iterations,
            "exploitability_interval": options.interval,
            "targets_mbb_per_game": options.targets,
            "seeds": options.seeds,
        },
        "environment": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "br_quality": {
            "mode": sorted({run["br_mode"] for run in runs}),
            "guarantee": sorted({run["guarantee"] for run in runs}),
            "br_samples": options.br_samples,
            "note": (
                "Sampled best responses are empirical estimates, not exact "
                "certificates; the per-run fields carry the mode and guarantee."
            ),
        },
        "diagnostics": {
            "schedules_are_inert": inert,
            "note": (
                "True means every schedule produced the same exploitability "
                "at the same iteration: the driver applies one update rule "
                "and the schedule dimension is not a comparison."
            ) if inert else (
                "Schedules produced different measurements; the per-target "
                "summary is a comparison."
            ),
        },
        "runs": runs,
        "summary": summary,
    }
    output = Path(options.output)
    with open(output, "w", encoding="utf-8") as output_file:
        json.dump(document, output_file, indent=2, sort_keys=True)
        output_file.write("\n")

    print(render_markdown(summary, options.targets))
    if inert:
        print(
            "warning: every schedule produced the same measurement; this "
            "driver applied a single update rule, so the rows above are not "
            "an algorithm comparison",
            file=sys.stderr,
        )
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
