#!/usr/bin/env python3
"""Fail unless `pe-plo4-lane-a` applies fixed and HS30 differently.

Issue #284 compared schedules on a driver that ignored them. This guard solves
the same spot with both schedules at two checkpoints and fails when every
measurement is identical, which is what an inert schedule flag produces.
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

CHECKPOINTS = (5, 20)


def solve(
    solver: Path, schedule: str, combos: int, iterations: int, workdir: Path
) -> dict[str, Any]:
    output = workdir / f"lane-{schedule}-{iterations}.json"
    command = [
        str(solver),
        "--combos", str(combos),
        "--iterations", str(iterations),
        "--exploitability-interval", str(iterations),
        "--dcfr-schedule", schedule,
        "--output", str(output),
    ]
    completed = subprocess.run(
        command, capture_output=True, text=True, check=False, timeout=120
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"pe-plo4-lane-a exited with {completed.returncode} for {schedule} "
            f"at {iterations} iterations: {completed.stderr.strip()}"
        )
    with open(output, encoding="utf-8") as report_file:
        return json.load(report_file)


def measurement(
    schedule: str, report: dict[str, Any]
) -> tuple[float, tuple[float, ...], tuple[float, float, float]]:
    """Validate labels and trajectories; return exploitability, gaps, params."""
    if report.get("dcfr_schedule") != schedule:
        raise AssertionError(
            f"report requested {report.get('dcfr_schedule')!r}, "
            f"expected {schedule!r}"
        )
    metrics = report["metrics"]
    if metrics.get("br_mode") != "exact":
        raise AssertionError(f"{schedule}: br_mode={metrics.get('br_mode')!r}")
    if metrics.get("guarantee") != "nash":
        raise AssertionError(f"{schedule}: guarantee={metrics.get('guarantee')!r}")
    if report["progress"]["target_reached"]:
        raise AssertionError(f"{schedule}: no target was requested")
    exploitability = float(metrics["exploitability_mbb_per_game"])
    gaps = tuple(float(gap) for gap in metrics["br_gap"])
    if not all(math.isfinite(value) for value in (exploitability, *gaps)):
        raise AssertionError(f"{schedule}: non-finite measurement")
    checkpoints = report.get("checkpoints")
    if not checkpoints:
        raise AssertionError(f"{schedule}: no checkpoint history")
    previous_iteration = -1
    previous_elapsed = -1.0
    for checkpoint in checkpoints:
        values = (
            float(checkpoint["elapsed_seconds"]),
            float(checkpoint["solve_seconds"]),
            float(checkpoint["br_seconds"]),
            float(checkpoint["exploitability_mbb_per_game"]),
        )
        if not all(math.isfinite(value) for value in values):
            raise AssertionError(f"{schedule}: non-finite checkpoint")
        raw_exponents = [
            checkpoint.get(key) for key in ("alpha", "beta", "gamma")
        ]
        if any(value is None for value in raw_exponents):
            raise AssertionError(
                f"{schedule}: checkpoint records no finite DCFR parameters"
            )
        exponents = tuple(float(value) for value in raw_exponents)
        if not all(math.isfinite(value) for value in exponents):
            raise AssertionError(
                f"{schedule}: checkpoint records no finite DCFR parameters"
            )
        iteration = int(checkpoint["iteration"])
        if iteration <= previous_iteration:
            raise AssertionError(f"{schedule}: checkpoints are not ordered")
        if values[0] < 0.0 or values[0] < previous_elapsed:
            raise AssertionError(f"{schedule}: checkpoint time went backwards")
        if values[1] < 0.0 or values[2] < 0.0:
            raise AssertionError(f"{schedule}: negative solve or BR time")
        if values[1] + values[2] > values[0] + 1e-6:
            raise AssertionError(f"{schedule}: solve + BR exceeds elapsed")
        previous_iteration = iteration
        previous_elapsed = values[0]
    if abs(values[3] - exploitability) > 1e-6:
        raise AssertionError(f"{schedule}: last checkpoint disagrees with metrics")
    if iteration != int(metrics.get("measurement_iteration", -1)):
        raise AssertionError(
            f"{schedule}: last checkpoint iteration disagrees with metrics"
        )
    timing = report.get("timing")
    if not isinstance(timing, dict):
        raise AssertionError(f"{schedule}: no timing summary")
    if int(timing.get("checkpoints", -1)) != len(checkpoints):
        raise AssertionError(f"{schedule}: timing summary has the wrong count")
    timing_values = (
        float(timing.get("elapsed_seconds", -1.0)),
        float(timing.get("solve_seconds", -1.0)),
        float(timing.get("br_seconds", -1.0)),
    )
    if not all(math.isfinite(value) for value in timing_values):
        raise AssertionError(f"{schedule}: non-finite timing summary")
    if abs(timing_values[0] - previous_elapsed) > 1e-6:
        raise AssertionError(f"{schedule}: timing elapsed disagrees with history")
    if abs(timing_values[1] - values[1]) > 1e-6:
        raise AssertionError(f"{schedule}: timing solve disagrees with history")
    total_br = sum(float(item["br_seconds"]) for item in checkpoints)
    if abs(timing_values[2] - total_br) > 1e-6:
        raise AssertionError(f"{schedule}: timing BR disagrees with history")
    # The report's root parameters must describe the same iteration as the last
    # checkpoint, otherwise the recorded trajectory is not what was solved.
    recorded = report.get("dcfr_params")
    if not isinstance(recorded, dict):
        raise AssertionError(f"{schedule}: no resolved DCFR parameters")
    if int(recorded.get("iteration", -1)) != iteration:
        raise AssertionError(
            f"{schedule}: root parameters describe a different iteration"
        )
    raw_root = [recorded.get(key) for key in ("alpha", "beta", "gamma")]
    if any(value is None for value in raw_root):
        raise AssertionError(f"{schedule}: root parameters are incomplete")
    root_exponents = tuple(float(value) for value in raw_root)
    if root_exponents != exponents:
        raise AssertionError(
            f"{schedule}: root parameters disagree with the last checkpoint"
        )
    if not isinstance(report.get("precision"), str):
        raise AssertionError(f"{schedule}: no recorded precision")
    if "peak_rss_bytes" not in report:
        raise AssertionError(f"{schedule}: peak RSS is not recorded")
    return exploitability, gaps, exponents


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--solver", default="build/local/tools/pe-plo4-lane-a",
        help="path to pe-plo4-lane-a",
    )
    parser.add_argument("--combos", type=int, default=64)
    options = parser.parse_args(argv[1:])

    solver = Path(options.solver)
    if not solver.is_file():
        print(f"missing solver: {solver}", file=sys.stderr)
        return 2
    try:
        values: dict[int, dict[str, tuple[float, tuple[float, ...]]]] = {}
        with tempfile.TemporaryDirectory(prefix="pe-lane-a-") as directory:
            workdir = Path(directory)
            for iterations in CHECKPOINTS:
                values[iterations] = {
                    schedule: measurement(
                        schedule,
                        solve(solver, schedule, options.combos, iterations, workdir),
                    )
                    for schedule in ("fixed", "hs30")
                }
    except (
        AssertionError, KeyError, OSError, RuntimeError,
        subprocess.SubprocessError, ValueError,
    ) as error:
        print(f"schedule check failed: {error}", file=sys.stderr)
        return 1

    differ = any(
        measured["fixed"][:2] != measured["hs30"][:2]
        for measured in values.values()
    )
    if not differ:
        print(
            "schedule check failed: fixed and hs30 reported identical "
            f"exploitability and gaps at {', '.join(map(str, CHECKPOINTS))} "
            "iterations",
            file=sys.stderr,
        )
        return 1
    # A driver that records the schedule name but resolves one parameter set
    # for every preset would pass the rows check by accident on another spot,
    # so the recorded trajectory is checked on its own.
    trajectories_differ = all(
        measured["fixed"][2] != measured["hs30"][2]
        for measured in values.values()
    )
    if not trajectories_differ:
        print(
            "schedule check failed: fixed and hs30 recorded identical DCFR "
            "parameter trajectories",
            file=sys.stderr,
        )
        return 1
    for iterations, measured in values.items():
        print(
            f"{iterations} iterations: fixed={measured['fixed'][0]:.6f} "
            f"hs30={measured['hs30'][0]:.6f} mbb/game "
            f"(alpha {measured['fixed'][2][0]:.3f} vs "
            f"{measured['hs30'][2][0]:.3f})"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
