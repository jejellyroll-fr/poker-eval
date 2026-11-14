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
) -> tuple[float, tuple[float, ...]]:
    """Validate the report labels and return exploitability plus gaps."""
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
    return exploitability, gaps


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
        measured["fixed"] != measured["hs30"] for measured in values.values()
    )
    if not differ:
        print(
            "schedule check failed: fixed and hs30 reported identical "
            f"exploitability and gaps at {', '.join(map(str, CHECKPOINTS))} "
            "iterations",
            file=sys.stderr,
        )
        return 1
    for iterations, measured in values.items():
        print(
            f"{iterations} iterations: fixed={measured['fixed'][0]:.6f} "
            f"hs30={measured['hs30'][0]:.6f} mbb/game"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
