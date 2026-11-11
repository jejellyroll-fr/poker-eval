#!/usr/bin/env python3
"""Run the PLO4 river microbenchmark and archive an exact baseline (issue #280).

Issue #280 asks for a trusted, reproducible heads-up PLO4 river baseline before
the vector traversal or showdown is optimized.  The measurement itself lives in
`tools/pe_plo4_river_bench.c`; this probe only drives it and validates the
report, the same split the rest of `benchmarks/solver/` uses.

What it enforces:

- every scenario is exact (`exact: true`, `sampled: false`);
- four-card private hands take the pairwise fallback, not the sorted path;
- the deterministic fields (combination counts, blocker counts, evaluated
  pairs, showdown path and the value fingerprint) are identical across
  repeated runs.

Peak RSS, heap accounting and wall clocks are copied from the measurement and
kept as machine-specific evidence.  They are **not** compared between runs:
this baseline exists to size the pairwise fallback on one machine, not to gate
a shared CI runner on speed.
"""

from __future__ import annotations

import argparse
import json
import math
import platform
import re
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any

BENCH_SCHEMA = "pe-plo4-river-bench/v1"
BASELINE_SCHEMA = "pe-plo4-river-baseline/v1"
FINGERPRINT_PATTERN = re.compile(r"^[0-9a-f]{16}$")
DETERMINISTIC_FIELDS = (
    "id",
    "hero_combos",
    "opp_combos",
    "zero_reach_opp_combos",
    "evaluated_pairs",
    "blocked_pairs",
    "showdown_path",
    "values_fingerprint",
    "exact",
)


class BenchValidationError(ValueError):
    """The microbenchmark report is not usable as an exact baseline."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise BenchValidationError(message)


def _non_negative_number(value: Any, label: str) -> float:
    _require(
        isinstance(value, (int, float)) and not isinstance(value, bool),
        f"{label} must be a number",
    )
    number = float(value)
    _require(math.isfinite(number) and number >= 0.0, f"{label} must be finite and non-negative")
    return number


def validate_bench_report(report: Any) -> None:
    """Reject anything that cannot be published as an exact PLO4 baseline."""
    _require(isinstance(report, dict), "bench report must be a JSON object")
    assert isinstance(report, dict)
    _require(report.get("schema") == BENCH_SCHEMA, f"schema must be {BENCH_SCHEMA}")
    _require(report.get("exact") is True, "top-level exact must be true")
    _require(report.get("sampled") is False, "a sampled report cannot be an exact baseline")
    repetitions = report.get("repetitions")
    _require(isinstance(repetitions, int) and repetitions >= 1, "repetitions must be >= 1")
    board = report.get("board")
    _require(isinstance(board, str) and len(board) == 10, "board must be five two-character cards")
    _non_negative_number(report.get("pot"), "pot")

    results = report.get("results")
    _require(isinstance(results, list) and results, "results must be a non-empty list")
    assert isinstance(results, list)
    seen: set[str] = set()
    for result in results:
        _require(isinstance(result, dict), "each result must be an object")
        assert isinstance(result, dict)
        result_id = result.get("id")
        _require(isinstance(result_id, str) and result_id, "result id must be a non-empty string")
        _require(result_id not in seen, f"duplicate result id {result_id}")
        seen.add(result_id)
        _require(result.get("exact") is True, f"{result_id}: exact must be true")
        hero = result.get("hero_combos")
        opp = result.get("opp_combos")
        _require(isinstance(hero, int) and hero > 0, f"{result_id}: hero_combos must be positive")
        _require(isinstance(opp, int) and opp > 0, f"{result_id}: opp_combos must be positive")
        pairs = result.get("evaluated_pairs")
        blocked = result.get("blocked_pairs")
        _require(
            isinstance(pairs, int) and 0 < pairs <= hero * opp,
            f"{result_id}: evaluated_pairs must be inside the Cartesian product",
        )
        _require(
            isinstance(blocked, int) and blocked == hero * opp - pairs,
            f"{result_id}: blocked_pairs must account for the rest of the product",
        )
        _require(
            result.get("showdown_path") == "pairwise",
            f"{result_id}: four-card hands must report the pairwise fallback",
        )
        _require(
            result.get("deterministic_across_repetitions") is True,
            f"{result_id}: repeated evaluations changed the values",
        )
        _non_negative_number(result.get("setup_seconds"), f"{result_id}: setup_seconds")
        eval_seconds = result.get("eval_seconds")
        _require(isinstance(eval_seconds, dict), f"{result_id}: eval_seconds must be an object")
        assert isinstance(eval_seconds, dict)
        low = _non_negative_number(eval_seconds.get("min"), f"{result_id}: eval min")
        median = _non_negative_number(eval_seconds.get("median"), f"{result_id}: eval median")
        high = _non_negative_number(eval_seconds.get("max"), f"{result_id}: eval max")
        total = _non_negative_number(eval_seconds.get("total"), f"{result_id}: eval total")
        _require(low <= median <= high, f"{result_id}: eval min/median/max are out of order")
        _require(total + 1e-12 >= high, f"{result_id}: eval total is below the maximum")
        per_call = _non_negative_number(
            result.get("eval_seconds_per_call"), f"{result_id}: eval_seconds_per_call"
        )
        _require(
            abs(per_call - median) <= 1e-12,
            f"{result_id}: eval_seconds_per_call must be the reported median",
        )
        _non_negative_number(
            result.get("pair_evaluations_per_second"),
            f"{result_id}: pair_evaluations_per_second",
        )
        fingerprint = result.get("values_fingerprint")
        _require(
            isinstance(fingerprint, str) and FINGERPRINT_PATTERN.match(fingerprint) is not None,
            f"{result_id}: values_fingerprint must be 16 lowercase hex digits",
        )
        peak = result.get("peak_rss_bytes")
        _require(
            peak is None or (isinstance(peak, int) and peak > 0),
            f"{result_id}: peak_rss_bytes must be null or positive",
        )
        allocations = result.get("allocations")
        _require(isinstance(allocations, dict), f"{result_id}: allocations must be an object")
        assert isinstance(allocations, dict)
        available = allocations.get("available")
        _require(isinstance(available, bool), f"{result_id}: allocations.available must be boolean")
        if available:
            _require(
                isinstance(allocations.get("counter"), str) and allocations["counter"],
                f"{result_id}: an available allocation count needs its counter name",
            )
            _require(
                isinstance(allocations.get("unit"), str) and allocations["unit"],
                f"{result_id}: an available allocation count needs its unit",
            )
        else:
            _require(
                isinstance(allocations.get("reason"), str) and allocations["reason"],
                f"{result_id}: an unavailable allocation count needs a reason",
            )


def fingerprint_map(report: dict[str, Any]) -> dict[str, str]:
    return {result["id"]: result["values_fingerprint"] for result in report["results"]}


def deterministic_mismatches(first: dict[str, Any], second: dict[str, Any]) -> list[str]:
    """Differences in deterministic fields only; timings are excluded."""
    validate_bench_report(first)
    validate_bench_report(second)
    problems: list[str] = []
    first_results = {result["id"]: result for result in first["results"]}
    second_results = {result["id"]: result for result in second["results"]}
    if sorted(first_results) != sorted(second_results):
        problems.append(
            f"scenario sets differ: {sorted(first_results)} vs {sorted(second_results)}"
        )
        return problems
    for result_id in sorted(first_results):
        for field in DETERMINISTIC_FIELDS:
            if first_results[result_id].get(field) != second_results[result_id].get(field):
                problems.append(
                    f"{result_id}.{field}: "
                    f"{first_results[result_id].get(field)!r} != "
                    f"{second_results[result_id].get(field)!r}"
                )
    return problems


def run_bench(
    bench: Path,
    *,
    repetitions: int,
    scenarios: list[str] | None = None,
    timeout: float = 600.0,
) -> dict[str, Any]:
    command = [str(bench), "--repetitions", str(repetitions)]
    for scenario in scenarios or []:
        command.extend(["--scenario", scenario])
    completed = subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )
    if completed.returncode != 0:
        raise BenchValidationError(
            f"{bench} exited {completed.returncode}: {completed.stderr.strip()}"
        )
    try:
        report = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise BenchValidationError(f"{bench} did not emit JSON: {exc}") from exc
    validate_bench_report(report)
    return report


def build_baseline(runs: list[dict[str, Any]], *, bench_binary: str) -> dict[str, Any]:
    """Copy the measured fields and add reproducibility, not a speed verdict."""
    if not runs:
        raise BenchValidationError("at least one run is required")
    for report in runs:
        validate_bench_report(report)
    problems: list[str] = []
    for other in runs[1:]:
        problems.extend(deterministic_mismatches(runs[0], other))
    results = []
    for result in runs[-1]["results"]:
        samples = [
            report_result
            for report in runs
            for report_result in report["results"]
            if report_result["id"] == result["id"]
        ]
        setups = [sample["setup_seconds"] for sample in samples]
        medians = [sample["eval_seconds"]["median"] for sample in samples]
        results.append(
            {
                "id": result["id"],
                "hero_combos": result["hero_combos"],
                "opp_combos": result["opp_combos"],
                "zero_reach_opp_combos": result["zero_reach_opp_combos"],
                "evaluated_pairs": result["evaluated_pairs"],
                "blocked_pairs": result["blocked_pairs"],
                "showdown_path": result["showdown_path"],
                "exact": result["exact"],
                "values_fingerprint": result["values_fingerprint"],
                "setup_seconds": statistics.median(setups),
                "eval_seconds_median": statistics.median(medians),
                "eval_seconds_min": min(sample["eval_seconds"]["min"] for sample in samples),
                "eval_seconds_max": max(sample["eval_seconds"]["max"] for sample in samples),
                "pair_evaluations_per_second": result["pair_evaluations_per_second"],
                "peak_rss_bytes": result["peak_rss_bytes"],
                "allocations": result["allocations"],
            }
        )
    return {
        "schema": BASELINE_SCHEMA,
        "issue": 280,
        "bench_binary": bench_binary,
        "board": runs[-1]["board"],
        "pot": runs[-1]["pot"],
        "seed": runs[-1]["seed"],
        "repetitions_per_run": runs[-1]["repetitions"],
        "runs": len(runs),
        "exact": runs[-1]["exact"],
        "sampled": runs[-1]["sampled"],
        "machine": {
            "system": platform.system(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "python": platform.python_version(),
        },
        "reproducibility": {
            "checked": len(runs) > 1,
            "fingerprints_match": not problems,
            "mismatches": problems,
        },
        "timings_are_machine_specific": True,
        "results": results,
    }


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bench",
        type=Path,
        default=root / "build" / "tools" / "pe-plo4-river-bench",
        help="path to the pe-plo4-river-bench binary",
    )
    parser.add_argument("--repetitions", type=int, default=5, help="timed repetitions inside each run")
    parser.add_argument("--repeat", type=int, default=2, help="process runs used for reproducibility")
    parser.add_argument(
        "--scenario",
        action="append",
        default=[],
        help="scenario id (repeatable); default is every scenario",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "build" / "plo4-river-baseline.json",
        help="baseline document to write",
    )
    parser.add_argument("--timeout", type=float, default=600.0)
    args = parser.parse_args(argv)

    if args.repeat < 1:
        parser.error("--repeat must be >= 1")
    if args.repetitions < 1:
        parser.error("--repetitions must be >= 1")
    if not args.bench.is_file():
        parser.error(f"benchmark binary not found: {args.bench}")

    runs = [
        run_bench(
            args.bench,
            repetitions=args.repetitions,
            scenarios=args.scenario,
            timeout=args.timeout,
        )
        for _ in range(args.repeat)
    ]
    baseline = build_baseline(runs, bench_binary=str(args.bench))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(baseline, indent=2) + "\n")

    for result in baseline["results"]:
        print(
            f"{result['id']}: {result['hero_combos']}x{result['opp_combos']} "
            f"({result['evaluated_pairs']} pairs), "
            f"setup {result['setup_seconds'] * 1000:.3f} ms, "
            f"eval {result['eval_seconds_median'] * 1000:.3f} ms, "
            f"path={result['showdown_path']}, "
            f"fingerprint={result['values_fingerprint']}"
        )
    if not baseline["reproducibility"]["fingerprints_match"]:
        for mismatch in baseline["reproducibility"]["mismatches"]:
            print(f"reproducibility mismatch: {mismatch}", file=sys.stderr)
        return 1
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
