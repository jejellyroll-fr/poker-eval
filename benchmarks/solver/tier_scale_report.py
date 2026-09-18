#!/usr/bin/env python3
"""Derive the tier-scale comparison artifact (storage-tier follow-up).

Reads the summary of a run over `cases_storage_tier_scale.json` and emits
one document (schema `pe-solver-tier-scale/v1`). Every figure is copied
from what the runner measured (exact `solver_accounting` bytes, solve
wall clock, exhaustive-report fingerprints); only family grouping and
ratios are computed here.

Two family kinds live in the same document, because issue #247 asks for
two different comparisons over the same storage twins:

- `families` — the `*_scale_*` cases: a large iteration budget under a
  hard memory budget that does *not* bind. The comparison is throughput
  and peak/storage ratios at equal iteration counts, so the exhaustive
  strategy fingerprints must match the `full` baseline.
- `budget_families` — the `*_budget_*` cases: a deliberately tight RAM
  budget so every tier is stopped by `memory_budget` at a different
  iteration. The comparison is iterations and infosets *reached* before
  the budget stopped the run, which is the fixed-budget criterion of
  issue #235 measured directly.

`--summary` may be repeated so the two suites can be run (and re-run)
independently and still land in one artifact.
"""

from __future__ import annotations

import argparse
import json
import platform
import sys
from pathlib import Path
from typing import Any

SCALE_SCHEMA = "pe-solver-tier-scale/v1"
BENCHMARK_SCHEMA = "pe-solver-benchmark-summary/v1"
SUFFIX_ORDER = ("full", "compact", "deep")
BASELINE_SUFFIX = "full"
VARIANT_SUFFIXES = SUFFIX_ORDER[1:]
SUFFIX_SET = frozenset(SUFFIX_ORDER)
SCALE_MARKER = "_scale_"
BUDGET_MARKER = "_budget_"


def split_marked_case(case_id: str, marker: str) -> tuple[str, str] | None:
    if marker not in case_id:
        return None
    family, separator, suffix = case_id.rpartition(marker)
    if not separator or not family or suffix not in SUFFIX_SET:
        return None
    return family, suffix


def split_family_case(case_id: str) -> tuple[str, str] | None:
    """Parse a `*_scale_*` case id into (family, requested policy)."""
    return split_marked_case(case_id, SCALE_MARKER)


def split_budget_case(case_id: str) -> tuple[str, str] | None:
    """Parse a `*_budget_*` case id into (family, requested policy)."""
    return split_marked_case(case_id, BUDGET_MARKER)


def load_json(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read {path}: {exc}")
    if not isinstance(document, dict):
        raise SystemExit(f"{path} does not contain a JSON object")
    return document


def case_entry(summary_case: dict[str, Any]) -> dict[str, Any]:
    benchmark = summary_case["benchmark"]
    memory = benchmark["memory"]
    metrics = benchmark["metrics"]
    accounting = memory.get("solver_accounting") or {}
    entry: dict[str, Any] = {
        "infosets": benchmark["infosets"],
        "actual_iterations": benchmark["actual_iterations"],
        "solve_seconds": benchmark["solve_elapsed_seconds"],
        "peak_measured_bytes": memory["peak_measured_bytes"],
        "final_bytes": memory["final_bytes"],
        "storage_bytes": memory["storage_bytes"],
        "adapter_bytes": memory["adapter_bytes"],
        "exploitability_mbb": metrics["exploitability_mbb_per_game"],
        # Issue #249: without this, a budget-stopped case's 0.0 exploitability
        # is indistinguishable from a converged solve that reached 0.
        "metrics_available": metrics.get("metrics_available"),
        "nash_conv_mbb": metrics.get("nash_conv_mbb_per_game"),
        "stop_cause": benchmark.get("stop_cause"),
        "strategy_fingerprint_sha256":
            benchmark["report"]["strategy_fingerprint_sha256"],
        "valid": not summary_case.get("validation_failures"),
        "validation": summary_case.get("validation_failures", []),
    }
    if accounting:
        entry["memory_accounting"] = {
            "memory_policy": accounting.get("memory_policy"),
            "storage_bytes": accounting.get("storage_bytes"),
            "retained_bytes": accounting.get("retained_bytes"),
            "recomputable_bytes": accounting.get("recomputable_bytes"),
            "recompute_calls": accounting.get("recompute_calls"),
            "bytes_saved_vs_full": accounting.get("bytes_saved_vs_full"),
            "bytes_per_infoset": accounting.get("bytes_per_infoset"),
            "bytes_per_strategy_slot": accounting.get("bytes_per_strategy_slot"),
        }
    return entry


def _group_families(
    summary: dict[str, Any],
    splitter,
    label: str,
    marker: str,
) -> dict[str, dict[str, Any]]:
    families: dict[str, dict[str, Any]] = {}
    for summary_case in summary.get("results", []):
        case_id = str(summary_case["case"])
        if marker not in case_id:
            # A merged summary may carry cases from another suite; they are
            # simply not this family kind.
            continue
        decoded = splitter(case_id)
        if decoded is None:
            raise SystemExit(
                f"{label} case {case_id!r} does not follow the {label} naming"
            )
        family, suffix = decoded
        entry = case_entry(summary_case)
        entry["requested_policy"] = suffix
        family_doc = families.setdefault(family, {"baseline": None, "variants": {}})
        if suffix == BASELINE_SUFFIX:
            if family_doc["baseline"] is not None:
                raise SystemExit(f"{label} family {family!r} has more than one baseline")
            family_doc["baseline"] = entry
        else:
            if suffix in family_doc["variants"]:
                raise SystemExit(
                    f"{label} family {family!r} repeats policy {suffix!r}"
                )
            family_doc["variants"][suffix] = entry
    return families


def _ordered_cases(
    family: str,
    doc: dict[str, Any],
    label: str,
    extra_ratios,
) -> list[dict[str, Any]]:
    baseline = doc["baseline"]
    if baseline is None:
        raise SystemExit(f"{label} family {family!r} has no baseline")
    missing = [s for s in VARIANT_SUFFIXES if s not in doc["variants"]]
    if missing:
        raise SystemExit(f"{label} family {family!r} is missing variant(s): {missing}")
    cases: list[dict[str, Any]] = [dict(baseline, policy=BASELINE_SUFFIX)]
    for suffix in VARIANT_SUFFIXES:
        variant = doc["variants"][suffix]
        case_doc = dict(variant, policy=suffix)
        if baseline["solve_seconds"] and variant["solve_seconds"]:
            case_doc["solve_ratio_vs_baseline"] = (
                variant["solve_seconds"] / baseline["solve_seconds"]
            )
        if baseline["peak_measured_bytes"] and variant["peak_measured_bytes"]:
            case_doc["peak_ratio_vs_baseline"] = (
                variant["peak_measured_bytes"] / baseline["peak_measured_bytes"]
            )
        if baseline["storage_bytes"] and variant["storage_bytes"]:
            case_doc["storage_ratio_vs_baseline"] = (
                variant["storage_bytes"] / baseline["storage_bytes"]
            )
        extra_ratios(case_doc, baseline, variant)
        cases.append(case_doc)
    return cases


def _no_extra_ratios(case_doc, baseline, variant) -> None:
    case_doc["strategy_matches_baseline"] = (
        variant["strategy_fingerprint_sha256"]
        == baseline["strategy_fingerprint_sha256"]
    )


def _budget_extra_ratios(case_doc, baseline, variant) -> None:
    # Under a binding budget the tiers stop at different iteration counts,
    # so their strategies legitimately diverge; the figure of merit is how
    # much further the compressed tier got.
    for field in ("actual_iterations", "infosets"):
        if baseline[field] and variant[field]:
            case_doc[f"{field}_ratio_vs_baseline"] = variant[field] / baseline[field]


def build_scale(summary: dict[str, Any], provenance: dict[str, Any] | None) -> dict[str, Any]:
    if summary.get("schema") != BENCHMARK_SCHEMA:
        raise SystemExit(
            f"unexpected summary schema {summary.get('schema')!r}, "
            f"expected {BENCHMARK_SCHEMA}"
        )

    scale_families = _group_families(summary, split_family_case, "scale", SCALE_MARKER)
    budget_families = _group_families(
        summary, split_budget_case, "budget", BUDGET_MARKER
    )

    compared: dict[str, Any] = {}
    for family, doc in sorted(scale_families.items()):
        compared[family] = {
            "cases": _ordered_cases(family, doc, "scale", _no_extra_ratios)
        }

    budget_compared: dict[str, Any] = {}
    for family, doc in sorted(budget_families.items()):
        budget_compared[family] = {
            "cases": _ordered_cases(family, doc, "budget", _budget_extra_ratios)
        }

    document: dict[str, Any] = {
        "schema": SCALE_SCHEMA,
        "platform": platform.platform(),
        "families": compared,
        "budget_families": budget_compared,
    }
    if provenance:
        document["solver"] = provenance.get("solver")
        document["manifest"] = provenance.get("manifest")
    return document


def merge_summaries(summaries: list[dict[str, Any]]) -> dict[str, Any]:
    for summary in summaries:
        if summary.get("schema") != BENCHMARK_SCHEMA:
            raise SystemExit(
                f"unexpected summary schema {summary.get('schema')!r}, "
                f"expected {BENCHMARK_SCHEMA}"
            )
    merged: dict[str, Any] = {"schema": BENCHMARK_SCHEMA, "results": []}
    for summary in summaries:
        merged["results"].extend(summary.get("results", []))
    return merged


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--summary",
        required=True,
        action="append",
        help="summary.json of a scale run (repeatable; results are merged)",
    )
    parser.add_argument(
        "--selection", help="optional selection.json for provenance"
    )
    parser.add_argument("--output", help="write the comparison here (default: stdout)")
    args = parser.parse_args()

    summaries = [load_json(Path(path)) for path in args.summary]
    summary = merge_summaries(summaries)
    selection = load_json(Path(args.selection)) if args.selection else None
    document = build_scale(summary, selection)

    rendered = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if args.output:
        Path(args.output).write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)

    failures = [
        f"{kind}/{family}/{case['policy']}: {failure}"
        for kind, group in (
            ("scale", document["families"]),
            ("budget", document["budget_families"]),
        )
        for family, doc in group.items()
        for case in doc["cases"]
        for failure in case.get("validation", [])
    ]
    if failures:
        print("\nFailures:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
