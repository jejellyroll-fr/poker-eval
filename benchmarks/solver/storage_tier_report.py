#!/usr/bin/env python3
"""Derive the storage-tier comparison artifact for issue #231.

Reads the summary of a run over `cases_storage_tiers.json` and emits one
document (schema `pe-solver-tier-comparison/v1`) comparing the resolved
memory policies per family. This script deliberately adds no solver
semantics: every figure is copied from what the runner measured (exact
`solver_accounting` bytes, solve-owned wall clock, exhaustive-report
fingerprints); only family grouping and ratios are computed here.

Family grouping is by the case-name marker `_tier_` followed by the
requested policy suffix (`full` is the baseline; `compact` and `deep`
are the variants). Each family must contain exactly one baseline case
and both variants; anything else is an error.
"""

from __future__ import annotations

import argparse
import json
import platform
import sys
from pathlib import Path
from typing import Any

COMPARISON_SCHEMA = "pe-solver-tier-comparison/v1"
BENCHMARK_SCHEMA = "pe-solver-benchmark-summary/v1"
# Canonical per-family order: baseline first, then the compact variants.
SUFFIX_ORDER = ("full", "compact", "deep")
BASELINE_SUFFIX = "full"
VARIANT_SUFFIXES = SUFFIX_ORDER[1:]
SUFFIX_SET = frozenset(SUFFIX_ORDER)


def split_family_case(case_id: str) -> tuple[str, str] | None:
    marker = "_tier_"
    if marker not in case_id:
        return None
    family, separator, suffix = case_id.rpartition(marker)
    if not separator or not family or suffix not in SUFFIX_SET:
        return None
    return family, suffix


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
        "nash_conv_mbb": metrics.get("nash_conv_mbb_per_game"),
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


def build_comparison(
    summary: dict[str, Any], selection: dict[str, Any] | None
) -> dict[str, Any]:
    if summary.get("schema") != BENCHMARK_SCHEMA:
        raise SystemExit(
            f"unexpected summary schema {summary.get('schema')!r}, "
            f"expected {BENCHMARK_SCHEMA}"
        )
    families: dict[str, dict[str, Any]] = {}
    for summary_case in summary.get("results", []):
        decoded = split_family_case(str(summary_case["case"]))
        if decoded is None:
            raise SystemExit(
                f"case {summary_case['case']!r} does not follow the tier case naming"
            )
        family, suffix = decoded
        entry = case_entry(summary_case)
        entry["requested_policy"] = suffix
        family_doc = families.setdefault(family, {"baseline": None, "variants": {}})
        if suffix == BASELINE_SUFFIX:
            if family_doc["baseline"] is not None:
                raise SystemExit(f"family {family!r} has more than one baseline")
            family_doc["baseline"] = entry
        else:
            if suffix in family_doc["variants"]:
                raise SystemExit(f"family {family!r} repeats policy {suffix!r}")
            family_doc["variants"][suffix] = entry

    compared: dict[str, Any] = {}
    for family, doc in sorted(families.items()):
        baseline = doc["baseline"]
        if baseline is None:
            raise SystemExit(f"family {family!r} has no baseline")
        missing = [s for s in VARIANT_SUFFIXES if s not in doc["variants"]]
        if missing:
            raise SystemExit(f"family {family!r} is missing variant(s): {missing}")
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
            case_doc["strategy_matches_baseline"] = (
                variant["strategy_fingerprint_sha256"]
                == baseline["strategy_fingerprint_sha256"]
            )
            cases.append(case_doc)
        compared[family] = {"cases": cases}

    document: dict[str, Any] = {
        "schema": COMPARISON_SCHEMA,
        "platform": platform.platform(),
        "families": compared,
    }
    if selection:
        document["solver"] = selection.get("solver")
        document["manifest"] = selection.get("manifest")
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", required=True, help="summary.json of a tiers run")
    parser.add_argument(
        "--selection", help="optional selection.json for solver/manifest provenance"
    )
    parser.add_argument("--output", help="write the comparison here (default: stdout)")
    args = parser.parse_args()

    summary = load_json(Path(args.summary))
    selection = load_json(Path(args.selection)) if args.selection else None
    document = build_comparison(summary, selection)

    rendered = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if args.output:
        Path(args.output).write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)

    failures = [
        f"{family}/{case['policy']}: {failure}"
        for family, doc in document["families"].items()
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
