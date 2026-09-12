#!/usr/bin/env python3
"""Validation helpers for the native pe-preflop-solve/v1 report."""

from __future__ import annotations

import math
from typing import Any

# The CLI telemetry prints exploitability with six digits after the decimal,
# while solver-report.json keeps the full %.17g value.  Cross-check those two
# views without rejecting the expected stdout rounding difference.
STDOUT_FLOAT_ABS_TOLERANCE = 1e-6

_REQUIRED_TOP_LEVEL_FIELDS = (
    "game",
    "players",
    "algorithm",
    "backend",
    "backend_validated",
    "precision",
    "simd_detected",
    "simd_cfr_integrated",
    "iterations",
    "showdown_samples",
    "stack",
    "small_blind",
    "big_blind",
    "ante",
    "allow_nonallin_call",
    "postflop_streets",
    "br_samples",
    "infosets",
    "progress",
    "metrics",
)


def _command_option(command: Any, option: str) -> str | None:
    if not isinstance(command, list):
        return None
    try:
        index = command.index(option)
    except ValueError:
        return None
    if index + 1 >= len(command):
        return None
    value = command[index + 1]
    return value if isinstance(value, str) else str(value)


def _json_int(value: Any) -> int | None:
    return value if type(value) is int else None


def _finite_number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    converted = float(value)
    return converted if math.isfinite(converted) else None


def _command_int(command: Any, option: str) -> int | None:
    raw = _command_option(command, option)
    if raw is None:
        return None
    try:
        return int(raw)
    except ValueError:
        return None


def _compare_command_string(
    failures: list[str], native_report: dict[str, Any], command: Any,
    field: str, option: str,
) -> None:
    requested = _command_option(command, option)
    if requested is None:
        failures.append(f"missing requested command option {option}")
        return
    actual = native_report.get(field)
    if not isinstance(actual, str):
        failures.append(f"native solver report {field} is not a string")
    elif actual != requested:
        failures.append(
            f"native solver report {field}={actual!r} != requested {requested!r}"
        )


def _compare_command_int(
    failures: list[str], native_report: dict[str, Any], command: Any,
    field: str, option: str,
) -> None:
    requested = _command_int(command, option)
    if requested is None:
        failures.append(f"missing or invalid requested command option {option}")
        return
    actual = _json_int(native_report.get(field))
    if actual is None:
        failures.append(f"native solver report {field} is not an integer")
    elif actual != requested:
        failures.append(
            f"native solver report {field}={actual} != requested {requested}"
        )


def _compare_stdout_float(
    failures: list[str], native_metrics: dict[str, Any], benchmark_metrics: dict[str, Any],
    field: str,
) -> None:
    native_value = _finite_number(native_metrics.get(field))
    stdout_value = _finite_number(benchmark_metrics.get(field))
    if native_value is None:
        failures.append(f"native solver report metrics.{field} is not finite")
        return
    if stdout_value is None:
        # validate_result() reports the missing stdout telemetry separately.
        return
    if not math.isclose(
        native_value,
        stdout_value,
        rel_tol=0.0,
        abs_tol=STDOUT_FLOAT_ABS_TOLERANCE,
    ):
        failures.append(
            f"native solver report metrics.{field}={native_value} "
            f"!= stdout {stdout_value}"
        )


def validate_native_report(
    native_report: dict[str, Any], result: dict[str, Any]
) -> list[str]:
    """Validate required native-report evidence and cross-check independent views."""
    failures: list[str] = []
    missing = [field for field in _REQUIRED_TOP_LEVEL_FIELDS if field not in native_report]
    failures.extend(
        f"native solver report missing field {field!r}" for field in missing
    )

    benchmark = result.get("benchmark")
    if not isinstance(benchmark, dict):
        failures.append("missing benchmark data for native report cross-check")
        return failures
    command = result.get("command")

    _compare_command_string(failures, native_report, command, "game", "--game")
    _compare_command_int(failures, native_report, command, "players", "--players")
    _compare_command_string(failures, native_report, command, "algorithm", "--algorithm")
    _compare_command_string(failures, native_report, command, "backend", "--backend")
    _compare_command_string(failures, native_report, command, "precision", "--precision")
    _compare_command_int(
        failures, native_report, command, "showdown_samples", "--samples"
    )
    _compare_command_int(failures, native_report, command, "iterations", "--iterations")
    _compare_command_int(failures, native_report, command, "br_samples", "--br-samples")

    if native_report.get("backend_validated") is not True:
        failures.append("native solver report backend_validated is not true")
    if not isinstance(native_report.get("simd_detected"), str):
        failures.append("native solver report simd_detected is not a string")
    if type(native_report.get("simd_cfr_integrated")) is not bool:
        failures.append("native solver report simd_cfr_integrated is not boolean")
    for field in ("allow_nonallin_call", "postflop_streets"):
        if type(native_report.get(field)) is not bool:
            failures.append(f"native solver report {field} is not boolean")
    for field in ("stack", "small_blind", "big_blind", "ante"):
        if _finite_number(native_report.get(field)) is None:
            failures.append(f"native solver report {field} is not finite")

    native_iterations = _json_int(native_report.get("iterations"))
    requested_iterations = benchmark.get("requested_iterations")
    if (
        native_iterations is not None
        and isinstance(requested_iterations, int)
        and native_iterations != requested_iterations
    ):
        failures.append(
            f"native solver report iterations={native_iterations} "
            f"!= benchmark requested {requested_iterations}"
        )

    native_br_samples = _json_int(native_report.get("br_samples"))
    benchmark_metrics = benchmark.get("metrics", {})
    if not isinstance(benchmark_metrics, dict):
        benchmark_metrics = {}
    requested_br_samples = benchmark_metrics.get("requested_br_samples")
    reported_br_samples = benchmark_metrics.get("br_samples")
    if (
        native_br_samples is not None
        and isinstance(requested_br_samples, int)
        and native_br_samples != requested_br_samples
    ):
        failures.append(
            f"native solver report br_samples={native_br_samples} "
            f"!= benchmark requested {requested_br_samples}"
        )
    if (
        native_br_samples is not None
        and isinstance(reported_br_samples, int)
        and native_br_samples != reported_br_samples
    ):
        failures.append(
            f"native solver report br_samples={native_br_samples} "
            f"!= stdout {reported_br_samples}"
        )

    # write_report() receives pe_preflop_allin_infodesc_count(), so this native
    # field is the description-table cardinality, not pe_solver_strategy_count().
    native_infosets = _json_int(native_report.get("infosets"))
    description_infosets = benchmark.get("description_infosets")
    if native_infosets is None:
        failures.append("native solver report infosets is not an integer")
    elif isinstance(description_infosets, int) and native_infosets != description_infosets:
        failures.append(
            f"native solver report infosets={native_infosets} "
            f"!= stdout descriptions {description_infosets}"
        )

    progress = native_report.get("progress")
    if not isinstance(progress, dict):
        failures.append("native solver report progress is not an object")
    else:
        progress_iteration = _json_int(progress.get("iteration"))
        actual_iterations = benchmark.get("actual_iterations")
        if progress_iteration is None:
            failures.append("native solver report progress.iteration is not an integer")
        elif isinstance(actual_iterations, int) and progress_iteration != actual_iterations:
            failures.append(
                f"native solver report progress.iteration={progress_iteration} "
                f"!= stdout {actual_iterations}"
            )
        progress_complete = progress.get("complete")
        stdout_complete = benchmark.get("complete")
        if type(progress_complete) is not bool:
            failures.append("native solver report progress.complete is not boolean")
        elif isinstance(stdout_complete, bool) and progress_complete != stdout_complete:
            failures.append(
                f"native solver report progress.complete={progress_complete} "
                f"!= stdout {stdout_complete}"
            )
        if progress_complete is not True:
            failures.append("native solver report progress is not complete")

    native_metrics = native_report.get("metrics")
    if not isinstance(native_metrics, dict):
        failures.append("native solver report metrics is not an object")
    else:
        guarantee = native_metrics.get("guarantee")
        stdout_guarantee = benchmark_metrics.get("guarantee")
        if not isinstance(guarantee, str) or not guarantee:
            failures.append("native solver report metrics.guarantee is missing")
        elif isinstance(stdout_guarantee, str) and guarantee != stdout_guarantee:
            failures.append(
                f"native solver report metrics.guarantee={guarantee!r} "
                f"!= stdout {stdout_guarantee!r}"
            )
        _compare_stdout_float(
            failures, native_metrics, benchmark_metrics, "exploitability_raw"
        )
        _compare_stdout_float(
            failures,
            native_metrics,
            benchmark_metrics,
            "exploitability_mbb_per_game",
        )
        native_metric_bb = _finite_number(native_metrics.get("big_blind"))
        native_bb = _finite_number(native_report.get("big_blind"))
        if native_metric_bb is None:
            failures.append("native solver report metrics.big_blind is not finite")
        elif native_bb is not None and native_metric_bb != native_bb:
            failures.append(
                f"native solver report metrics.big_blind={native_metric_bb} "
                f"!= top-level big_blind {native_bb}"
            )

    return failures
