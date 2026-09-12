#!/usr/bin/env python3
"""Reproducible cross-variant solver benchmark runner.

Uses the product-facing pe-preflop-solve binary as the single source of
solver semantics. The runner adds stable case definitions, timing, telemetry
parsing, per-street strategy coverage and reproducibility fingerprints without
introducing a second solver/report implementation.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import unicodedata
from typing import Any

from native_report_validation import validate_native_report

SCHEMA = "pe-solver-benchmark/v1"
SUMMARY_SCHEMA = "pe-solver-benchmark-summary/v1"
NATIVE_REPORT_SCHEMA = "pe-preflop-solve/v1"
SELECTION_SCHEMA = "pe-solver-benchmark-selection/v1"
STREETS = ("PREFLOP", "FLOP", "TURN", "RIVER")
SOLVE_START_MARKER = "solver created"
MANAGED_SUMMARY_FILES = ("selection.json", "summary.json", "summary.csv")
MANAGED_SUMMARY_CASEFOLDS = frozenset(name.casefold() for name in MANAGED_SUMMARY_FILES)
WINDOWS_INVALID_CASE_CHARS = frozenset('<>:"/\\|?*')
WINDOWS_RESERVED_CASE_NAMES = frozenset(
    {"con", "prn", "aux", "nul"}
    | {f"com{index}" for index in range(1, 10)}
    | {f"lpt{index}" for index in range(1, 10)}
)
ACTION_PERCENT_SUM_TOLERANCE = 0.51

# The final "infosets" field on this legacy line is the description-table
# count, not necessarily the solver's retained strategy count when --desc-limit
# caps descriptions.
RE_ITERATIONS = re.compile(r"^iterations=(\d+)\s+complete=(\d+)\s+infosets=(\d+)$")
RE_GUARANTEE = re.compile(
    r"^guarantee=(\S+)\s+exploitability_raw=([^\s]+)\s+"
    r"exploitability_mbb=([^\s]+)\s+br_samples=(\d+)$"
)
RE_LOOP_END = re.compile(
    r"solve_loop_end cause=(\S+)\s+iteration=(\d+)\s+"
    r"memory_mb=([0-9.]+)\s+storage_mb=([0-9.]+)\s+adapter_mb=([0-9.]+)"
)
RE_STOP_DETAIL = re.compile(
    r"^stop_detail cause=(\S+)\s+interrupted=(\d+)\s+iteration=(\d+)\s+"
    r"held_mb=([0-9.]+)\s+budget_mb=([0-9.]+)\s+"
    r"descriptions_mb=([0-9.]+)\s+descriptions_capped=([01])$"
)
RE_MEMORY = re.compile(r"\bmemory_mb=([0-9.]+)")
RE_TREE_STREETS = re.compile(r"^tree_streets=(.*)$")
RE_REPORT_START = re.compile(r"^report_phase=starting rows=(\d+)\s+infosets=(\d+)$")
RE_REPORT_COMPLETE = re.compile(r"^report_phase=complete rows=(\d+)$")

MB = 1024 * 1024


def _float(text: str | None) -> float | None:
    if text is None:
        return None
    try:
        value = float(text)
    except ValueError:
        return None
    return value if math.isfinite(value) else None


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        data = json.load(stream)
    if data.get("schema") != "pe-solver-benchmark-cases/v1":
        raise ValueError(f"unsupported manifest schema in {path}")
    if not isinstance(data.get("cases"), list):
        raise ValueError("manifest must contain a cases array")
    return data


def resolve_solver(args: argparse.Namespace, root: Path) -> Path:
    if args.solver:
        candidate = Path(args.solver)
    else:
        suffix = ".exe" if os.name == "nt" else ""
        candidate = Path(args.build_dir) / "tools" / f"pe-preflop-solve{suffix}"
    if not candidate.is_absolute():
        candidate = root / candidate
    candidate = candidate.resolve()
    if not candidate.exists():
        raise FileNotFoundError(
            f"solver not found: {candidate}\n"
            "Build it with: cmake --build build --target pe-preflop-solve"
        )
    return candidate


def case_selected(case: dict[str, Any], suites: set[str], names: set[str]) -> bool:
    if names:
        return case.get("id") in names
    return bool(set(case.get("tags", [])) & suites)


def _validate_safe_case_id(case_id: str) -> str:
    windows_device_stem = case_id.split(".", 1)[0].casefold()
    has_windows_invalid_char = any(
        ord(char) < 32 or char in WINDOWS_INVALID_CASE_CHARS for char in case_id
    )
    if (
        case_id in {".", ".."}
        or case_id.casefold() in MANAGED_SUMMARY_CASEFOLDS
        or case_id.rstrip(" .") != case_id
        or windows_device_stem in WINDOWS_RESERVED_CASE_NAMES
        or has_windows_invalid_char
        or Path(case_id).is_absolute()
        or Path(case_id).name != case_id
    ):
        raise ValueError(f"unsafe benchmark case id: {case_id!r}")
    return case_id


def safe_case_id(case: dict[str, Any]) -> str:
    """Return a case id safe as one portable output-directory component."""
    case_id = case.get("id")
    if not isinstance(case_id, str) or not case_id:
        raise ValueError(f"unsafe benchmark case id: {case_id!r}")
    case_id = unicodedata.normalize("NFC", case_id)
    return _validate_safe_case_id(case_id)


def _case_id_key(case_id: str) -> str:
    return unicodedata.normalize("NFC", case_id).casefold()


def validate_manifest_case_ids(manifest_cases: list[dict[str, Any]]) -> list[str]:
    """Validate case ids once and reject aliases that would share evidence."""
    case_ids: list[str] = []
    seen: set[str] = set()
    for case in manifest_cases:
        case_id = safe_case_id(case)
        case_key = _case_id_key(case_id)
        if case_key in seen:
            raise ValueError(f"duplicate benchmark case id: {case_id!r}")
        seen.add(case_key)
        case_ids.append(case_id)
    return case_ids


def previous_selection_case_ids(out_dir: Path) -> list[str]:
    """Return safe case ids recorded by a prior runner selection, if readable."""
    selection_path = out_dir / "selection.json"
    try:
        selection = json.loads(selection_path.read_text(encoding="utf-8"))
    except (FileNotFoundError, OSError, json.JSONDecodeError):
        return []
    if not isinstance(selection, dict) or selection.get("schema") != SELECTION_SCHEMA:
        return []
    raw_cases = selection.get("cases")
    if not isinstance(raw_cases, list):
        return []

    # Keep distinct historical spellings. On a case-sensitive filesystem an
    # old `Foo` directory and a new/current `foo` directory are different paths
    # and both must be cleaned. Current manifests are still validated with a
    # case-folded uniqueness key so new evidence cannot create such aliases.
    case_ids: list[str] = []
    seen: set[str] = set()
    for raw_case_id in raw_cases:
        try:
            if not isinstance(raw_case_id, str) or not raw_case_id:
                raise ValueError
            # Validate the historical spelling, but do not normalize it: on a
            # case-sensitive filesystem its exact spelling identifies the
            # directory that must be removed.
            case_id = _validate_safe_case_id(raw_case_id)
        except ValueError:
            # A stale or edited selection must never turn cleanup into an
            # arbitrary path deletion. Ignore unsafe historical entries.
            continue
        if case_id not in seen:
            seen.add(case_id)
            case_ids.append(case_id)
    return case_ids


def prepare_output_dir(out_dir: Path, manifest_cases: list[dict[str, Any]]) -> None:
    """Remove only evidence owned by a prior benchmark selection.

    A first run may point --output-dir at a directory that already contains
    unrelated files or directories whose names happen to match case ids. Without
    prior selection metadata those paths are not ours to delete or overwrite.
    Once a valid prior selection records case ownership, current and historical
    case spellings can be pruned safely before the next run.
    """
    # Validate every current manifest id before performing any cleanup. This
    # prevents malformed or duplicate ids from partially deleting prior evidence.
    current_case_ids = validate_manifest_case_ids(manifest_cases)
    previous_case_ids = previous_selection_case_ids(out_dir)
    prior_selection_owned = bool(previous_case_ids)

    if prior_selection_owned:
        previous_keys = {_case_id_key(case_id) for case_id in previous_case_ids}
        # Prior metadata owns only the case ids it recorded. A current spelling
        # that is case-insensitively equivalent (old `Foo`, current `foo`) is
        # the same logical case and both spellings are safe to prune on a
        # case-sensitive filesystem. Newly introduced ids are not owned yet.
        managed_case_ids = list(
            dict.fromkeys(
                [
                    *previous_case_ids,
                    *(
                        case_id
                        for case_id in current_case_ids
                        if _case_id_key(case_id) in previous_keys
                    ),
                ]
            )
        )
        unowned_current_ids = [
            case_id
            for case_id in current_case_ids
            if _case_id_key(case_id) not in previous_keys
        ]
        collisions = [
            out_dir / case_id
            for case_id in unowned_current_ids
            if (out_dir / case_id).exists() or (out_dir / case_id).is_symlink()
        ]
    else:
        managed_case_ids = []
        # With no trustworthy previous selection, preserve colliding user data
        # instead of guessing that it belongs to this runner. Aggregate names
        # are unowned too on first use.
        collisions = [
            *(out_dir / name for name in MANAGED_SUMMARY_FILES),
            *(out_dir / case_id for case_id in current_case_ids),
        ]
        collisions = [
            path for path in collisions if path.exists() or path.is_symlink()
        ]

    # Refuse before deleting anything so a manifest switch cannot partially
    # clean prior evidence and then discover that a newly introduced case id
    # collides with unrelated user data (for example build/tools).
    if collisions:
        rendered = ", ".join(str(path) for path in collisions)
        raise ValueError(
            "refusing to overwrite unowned benchmark output path(s): "
            f"{rendered}"
        )

    out_dir.mkdir(parents=True, exist_ok=True)
    if prior_selection_owned:
        for name in MANAGED_SUMMARY_FILES:
            path = out_dir / name
            try:
                path.unlink()
            except FileNotFoundError:
                pass

        for case_id in managed_case_ids:
            case_path = out_dir / case_id
            if case_path.is_symlink():
                case_path.unlink()
            elif case_path.is_dir():
                shutil.rmtree(case_path)
            elif case_path.exists():
                case_path.unlink()


def tree_nodes(
    tree_path: Path,
    root_to_act: int | None = None,
) -> tuple[dict[int, str], dict[int, str], dict[str, int]]:
    with tree_path.open("r", encoding="utf-8") as stream:
        tree = json.load(stream)
    by_index: dict[int, str] = {}
    actors: dict[int, str] = {}
    decisions = {street: 0 for street in STREETS}
    root_id = tree.get("root")
    for index, node in enumerate(tree.get("nodes", [])):
        if node.get("type") != "player":
            continue
        street = str(node.get("street", "")).upper()
        if street in decisions:
            by_index[index] = street
            player = (
                root_to_act
                if root_to_act is not None and node.get("id") == root_id
                else node.get("player")
            )
            actors[index] = (
                f"P{player + 1}"
                if isinstance(player, int) and not isinstance(player, bool) and player >= 0
                else ""
            )
            decisions[street] += 1
    return by_index, actors, decisions


def build_command(
    solver: Path,
    root: Path,
    raw_report: Path,
    case: dict[str, Any],
    defaults: dict[str, Any],
    iteration_override: int | None,
) -> list[str]:
    def setting(name: str, fallback: Any = None) -> Any:
        return case.get(name, defaults.get(name, fallback))

    iterations = iteration_override or int(setting("iterations", 2000))
    command = [
        str(solver),
        "--game", str(case["game"]),
        "--players", str(setting("players", 2)),
        "--iterations", str(iterations),
        "--samples", str(setting("showdown_samples", 1)),
        "--br-samples", str(setting("br_samples", 16)),
        "--exploitability-interval", str(setting("exploitability_interval", iterations)),
        "--algorithm", str(setting("algorithm", "external-mccfr")),
        "--backend", str(setting("backend", "cpu_ref")),
        "--precision", str(setting("precision", "f64")),
        "--threads", str(setting("threads", 1)),
        "--target-mbb", "0",
        "--seed", str(setting("seed", 20260906)),
        "--max-ram", str(setting("max_ram_mb", 512)),
        "--desc-limit", str(setting("desc_limit_mb", 64)),
        "--report-rows", str(setting("report_rows", 0)),
        "--output", str(raw_report),
    ]
    ranges = case.get("ranges", defaults.get("ranges", ["100%", "100%"]))
    for player, range_text in enumerate(ranges):
        command.extend((f"--range{player}", str(range_text)))

    tree = root / case["tree"]
    command.extend(("--tree", str(tree.resolve())))

    street = case.get("street")
    if street and street != "preflop":
        command.extend(("--street", street))
        command.extend(("--board", str(case["board"])))
        command.extend(("--pot", str(case["pot"])))
    if case.get("to_act") is not None:
        command.extend(("--to-act", str(case["to_act"])))
    abstraction = case.get("board_abstraction")
    if abstraction:
        command.extend(("--board-abstraction", abstraction))
    return command


def strategy_frequencies(action_field: str) -> list[float] | None:
    """Parse a rendered strategy into a finite percentage distribution."""
    values: list[float] = []
    for token in action_field.split(","):
        action, separator, raw_percent = token.partition("=")
        raw_percent = raw_percent.strip()
        if not separator or not action.strip() or not raw_percent.endswith("%"):
            return None
        value = _float(raw_percent[:-1])
        if value is None or value < 0.0 or value > 100.0:
            return None
        values.append(value)
    if not values or abs(sum(values) - 100.0) > ACTION_PERCENT_SUM_TOLERANCE:
        return None
    return values


def _is_uniform_frequencies(values: list[float]) -> bool:
    expected = 100.0 / len(values)
    return max(abs(value - expected) for value in values) <= 0.11


def is_uniform_strategy(action_field: str) -> bool:
    values = strategy_frequencies(action_field)
    return values is not None and _is_uniform_frequencies(values)


def board_identity(board_text: str) -> tuple[str, ...] | None:
    """Return an order-independent normalized card identity for a compact board."""
    compact = "".join(board_text.split())
    if not compact or compact == "-" or len(compact) % 2 != 0:
        return None
    cards: list[str] = []
    for index in range(0, len(compact), 2):
        rank = compact[index].upper()
        suit = compact[index + 1].lower()
        if rank not in "23456789TJQKA" or suit not in "cdhs":
            return None
        card = rank + suit
        if card in cards:
            return None
        cards.append(card)
    return tuple(sorted(cards))


def _strategy_rows(
    stdout: str,
    node_streets: dict[int, str],
    node_actors: dict[int, str] | None = None,
) -> tuple[list[tuple[str, int, str, str]], int]:
    rows: list[tuple[str, int, str, str]] = []
    actor_mismatch_rows = 0
    for line in stdout.splitlines():
        if line.startswith("ev_update\t"):
            continue
        fields = line.split("\t")
        if len(fields) < 6:
            continue
        try:
            node_index = int(fields[1])
        except ValueError:
            continue
        street = node_streets.get(node_index)
        if street not in STREETS:
            continue
        actor = fields[2].strip()
        if node_actors is not None and actor != node_actors.get(node_index):
            actor_mismatch_rows += 1
            continue
        action_field = fields[3]
        if "%" not in action_field or "=" not in action_field:
            continue
        board = fields[5].strip()
        stable = (
            f"{street}\t{fields[0]}\t{node_index}\t{fields[2]}\t"
            f"{action_field}\t{board}"
        )
        rows.append((stable, node_index, action_field, board))
    return rows, actor_mismatch_rows


def parse_strategy_rows(
    stdout: str,
    node_streets: dict[int, str],
    *,
    exhaustive_report: bool = False,
    node_actors: dict[int, str] | None = None,
) -> tuple[dict[str, dict[str, Any]], str, dict[str, Any]]:
    """Parse visible strategy rows without destroying real multiplicity.

    With --report-rows 0 the current solver executes both report sweeps with
    filtering disabled, so the complete sequence is emitted twice. Remove only
    that known whole-sweep duplication. Distinct solver infosets that happen to
    render to the same stable row remain distinct entries and therefore remain
    represented in counts and in the reproducibility hash.
    """
    rows, actor_mismatch_rows = _strategy_rows(stdout, node_streets, node_actors)
    raw_rows = len(rows)
    duplicate_sweep_removed = False

    if exhaustive_report and raw_rows > 0 and raw_rows % 2 == 0:
        half = raw_rows // 2
        if rows[:half] == rows[half:]:
            rows = rows[:half]
            duplicate_sweep_removed = True

    street_data = {
        street: {
            "strategy_rows": 0,
            "uniform_rows": 0,
            "non_uniform_rows": 0,
            "invalid_strategy_rows": 0,
            "missing_board_rows": 0,
            "unique_nodes_with_rows": 0,
            "unique_boards": 0,
            "observed_boards": [],
        }
        for street in STREETS
    }
    nodes_seen = {street: set() for street in STREETS}
    boards_seen = {street: set() for street in STREETS}
    fingerprint_rows: list[str] = []
    invalid_strategy_rows = 0

    for stable, node_index, action_field, board in rows:
        street = node_streets[node_index]
        data = street_data[street]
        data["strategy_rows"] += 1
        frequencies = strategy_frequencies(action_field)
        if frequencies is None:
            data["invalid_strategy_rows"] += 1
            invalid_strategy_rows += 1
        elif _is_uniform_frequencies(frequencies):
            data["uniform_rows"] += 1
        else:
            data["non_uniform_rows"] += 1
        nodes_seen[street].add(node_index)
        if board and board != "-":
            boards_seen[street].add(board)
        else:
            data["missing_board_rows"] += 1
        fingerprint_rows.append(stable)

    for street in STREETS:
        street_data[street]["unique_nodes_with_rows"] = len(nodes_seen[street])
        street_data[street]["unique_boards"] = len(boards_seen[street])
        street_data[street]["observed_boards"] = sorted(boards_seen[street])

    # sorted(list) intentionally retains duplicates: multiplicity is part of
    # the deterministic result and must affect the fingerprint.
    digest = hashlib.sha256(
        ("\n".join(sorted(fingerprint_rows)) + "\n").encode("utf-8")
    ).hexdigest()
    details = {
        "raw_strategy_rows": raw_rows,
        "normalized_strategy_rows": len(rows),
        "invalid_strategy_rows": invalid_strategy_rows,
        "actor_mismatch_rows": actor_mismatch_rows,
        "duplicate_exhaustive_sweep_removed": duplicate_sweep_removed,
    }
    return street_data, digest, details


def parse_stdout(
    stdout: str,
    tree_decisions: dict[str, int],
    node_streets: dict[int, str],
    process_elapsed_seconds: float,
    solve_elapsed_seconds: float | None,
    requested_iterations: int,
    report_rows_requested: int,
    post_solve_elapsed_seconds: float | None = None,
    node_actors: dict[int, str] | None = None,
) -> dict[str, Any]:
    actual_iterations = None
    complete = None
    description_infosets = None
    solver_infosets = None
    guarantee = None
    exploitability_raw = None
    exploitability_mbb = None
    br_samples = None
    stop_cause = None
    final_memory_mb = None
    storage_mb = None
    adapter_mb = None
    descriptions_mb = None
    descriptions_capped = None
    reporter_emitted_entries = None
    tree_census = None

    for line in stdout.splitlines():
        match = RE_ITERATIONS.match(line)
        if match:
            actual_iterations = int(match.group(1))
            complete = bool(int(match.group(2)))
            description_infosets = int(match.group(3))
            continue
        match = RE_REPORT_START.match(line)
        if match:
            solver_infosets = int(match.group(1))
            description_infosets = int(match.group(2))
            continue
        match = RE_GUARANTEE.match(line)
        if match:
            guarantee = match.group(1)
            exploitability_raw = _float(match.group(2))
            exploitability_mbb = _float(match.group(3))
            br_samples = int(match.group(4))
            continue
        match = RE_LOOP_END.search(line)
        if match:
            stop_cause = match.group(1)
            final_memory_mb = float(match.group(3))
            storage_mb = float(match.group(4))
            adapter_mb = float(match.group(5))
            continue
        match = RE_STOP_DETAIL.match(line)
        if match:
            stop_cause = stop_cause or match.group(1)
            if final_memory_mb is None:
                final_memory_mb = float(match.group(4))
            descriptions_mb = float(match.group(6))
            descriptions_capped = bool(int(match.group(7)))
            continue
        match = RE_TREE_STREETS.match(line)
        if match:
            tree_census = match.group(1)
            continue
        match = RE_REPORT_COMPLETE.match(line)
        if match:
            reporter_emitted_entries = int(match.group(1))

    measured_memory = [float(value) for value in RE_MEMORY.findall(stdout)]
    peak_measured_memory_mb = max(measured_memory) if measured_memory else final_memory_mb
    per_street, fingerprint, row_details = parse_strategy_rows(
        stdout,
        node_streets,
        exhaustive_report=report_rows_requested == 0,
        node_actors=node_actors,
    )
    total_rows = sum(v["strategy_rows"] for v in per_street.values())
    total_non_uniform = sum(v["non_uniform_rows"] for v in per_street.values())
    normalized_strategy_rows = row_details["normalized_strategy_rows"]
    report_completed = reporter_emitted_entries is not None
    report_exhaustive = (
        report_rows_requested == 0
        and descriptions_capped is False
        and report_completed
        and solver_infosets is not None
        and normalized_strategy_rows == solver_infosets
    )

    for street, data in per_street.items():
        data["decision_nodes"] = tree_decisions.get(street, 0)
        nodes = data["decision_nodes"]
        data["decision_node_coverage"] = (
            data["unique_nodes_with_rows"] / nodes if nodes else None
        )
        data["materialized_infoset_share"] = (
            data["strategy_rows"] / total_rows if total_rows else 0.0
        )
        data["non_uniform_share"] = (
            data["non_uniform_rows"] / total_non_uniform if total_non_uniform else 0.0
        )

    iterations_per_second = (
        actual_iterations / solve_elapsed_seconds
        if actual_iterations is not None
        and actual_iterations > 0
        and solve_elapsed_seconds is not None
        and solve_elapsed_seconds > 0
        else None
    )
    final_memory_bytes = int(round(final_memory_mb * MB)) if final_memory_mb is not None else None
    storage_bytes = int(round(storage_mb * MB)) if storage_mb is not None else None
    adapter_bytes = int(round(adapter_mb * MB)) if adapter_mb is not None else None
    descriptions_bytes = int(round(descriptions_mb * MB)) if descriptions_mb is not None else None

    return {
        "requested_iterations": requested_iterations,
        "actual_iterations": actual_iterations,
        "complete": complete,
        "stop_cause": stop_cause,
        "elapsed_seconds": process_elapsed_seconds,
        "solve_elapsed_seconds": solve_elapsed_seconds,
        "post_solve_elapsed_seconds": post_solve_elapsed_seconds,
        "iterations_per_second": iterations_per_second,
        "infosets": solver_infosets,
        "description_infosets": description_infosets,
        "infosets_per_1k_iterations": (
            solver_infosets * 1000.0 / actual_iterations
            if solver_infosets is not None and actual_iterations else None
        ),
        "memory": {
            "peak_measured_bytes": (
                int(round(peak_measured_memory_mb * MB))
                if peak_measured_memory_mb is not None else None
            ),
            "final_bytes": final_memory_bytes,
            "storage_bytes": storage_bytes,
            "adapter_bytes": adapter_bytes,
            "descriptions_bytes": descriptions_bytes,
            "bytes_per_infoset": (
                final_memory_bytes / solver_infosets
                if final_memory_bytes is not None and solver_infosets else None
            ),
            "storage_bytes_per_infoset": (
                storage_bytes / solver_infosets
                if storage_bytes is not None and solver_infosets else None
            ),
            "descriptions_capped": descriptions_capped,
        },
        "metrics": {
            "guarantee": guarantee,
            "exploitability_raw": exploitability_raw,
            "exploitability_mbb_per_game": exploitability_mbb,
            "br_samples": br_samples,
        },
        "report": {
            "requested_rows": report_rows_requested,
            "emitted_rows": row_details["raw_strategy_rows"],
            "reporter_emitted_entries": reporter_emitted_entries,
            "completed": report_completed,
            "exhaustive_requested": report_rows_requested == 0,
            "exhaustive": report_exhaustive,
            "strategy_fingerprint_sha256": fingerprint,
            **row_details,
        },
        "tree_streets": tree_census,
        "per_street": per_street,
        "sampled_private_deals": None,
    }


def validate_result(result: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    process = result["process"]
    if process["returncode"] != 0:
        failures.append(f"solver exited {process['returncode']}")
        return failures

    if process.get("solver_report") is None:
        failures.append("missing native solver report")
    else:
        native_report = result.get("native_solver_report")
        if not isinstance(native_report, dict):
            detail = process.get("solver_report_error")
            suffix = f": {detail}" if detail else ""
            failures.append(f"invalid native solver report JSON{suffix}")
        elif native_report.get("schema") != NATIVE_REPORT_SCHEMA:
            failures.append(
                f"native solver report schema={native_report.get('schema')!r}, "
                f"expected {NATIVE_REPORT_SCHEMA}"
            )
        else:
            failures.extend(validate_native_report(native_report, result))

    metrics = result["benchmark"]
    if metrics["actual_iterations"] != metrics["requested_iterations"]:
        failures.append(
            f"iterations {metrics['actual_iterations']} != requested "
            f"{metrics['requested_iterations']}"
        )
    if metrics["stop_cause"] != "max_iterations":
        failures.append(f"stop_cause={metrics['stop_cause']!r}, expected max_iterations")
    if metrics["solve_elapsed_seconds"] is None:
        failures.append("missing solver timing markers")
    elif metrics["solve_elapsed_seconds"] <= 0:
        failures.append("non-positive solve elapsed time")
    if metrics["infosets"] is None:
        failures.append("missing solver strategy count from report start")
    elif metrics["infosets"] <= 0:
        failures.append("no infosets were materialized")

    convergence = metrics.get("metrics", {})
    guarantee = convergence.get("guarantee")
    if not guarantee:
        failures.append("missing convergence guarantee telemetry")
    elif guarantee == "unspecified":
        failures.append("unspecified convergence guarantee telemetry")
    if convergence.get("exploitability_raw") is None:
        failures.append("missing exploitability_raw telemetry")
    if convergence.get("exploitability_mbb_per_game") is None:
        failures.append("missing exploitability_mbb telemetry")
    reported_br_samples = convergence.get("br_samples")
    requested_br_samples = convergence.get("requested_br_samples")
    if reported_br_samples is None:
        failures.append("missing br_samples telemetry")
    if requested_br_samples is None:
        failures.append("missing requested br_samples configuration")
    elif reported_br_samples is not None and reported_br_samples != requested_br_samples:
        failures.append(
            f"br_samples {reported_br_samples} != requested {requested_br_samples}"
        )

    report = metrics["report"]
    if not report.get("completed", False):
        failures.append("missing completed strategy report")
    invalid_strategy_rows = report.get("invalid_strategy_rows", 0)
    if invalid_strategy_rows:
        failures.append(
            f"invalid strategy frequencies in {invalid_strategy_rows} row(s)"
        )
    actor_mismatch_rows = report.get("actor_mismatch_rows", 0)
    if actor_mismatch_rows:
        failures.append(f"invalid strategy actors in {actor_mismatch_rows} row(s)")
    descriptions_capped = metrics.get("memory", {}).get("descriptions_capped")
    if report.get("exhaustive_requested", False) and descriptions_capped is None:
        failures.append("missing description cap telemetry for exhaustive report")
    if (
        report.get("exhaustive_requested", False)
        and descriptions_capped is False
        and report.get("completed", False)
        and metrics["infosets"] is not None
        and not report.get("exhaustive", False)
    ):
        failures.append(
            "incomplete exhaustive strategy report: "
            f"normalized_rows={report.get('normalized_strategy_rows')} "
            f"solver_infosets={metrics['infosets']}"
        )
    required_streets = result["case"].get("expect_streets", [])
    configured_board = result["case"].get("board")
    expected_board_identity = (
        board_identity(str(configured_board)) if configured_board is not None else None
    )
    if configured_board is not None and expected_board_identity is None:
        failures.append(f"invalid configured board {configured_board!r}")
    for street in required_streets:
        normalized = street.upper()
        data = metrics["per_street"].get(normalized)
        if not data or data["decision_nodes"] <= 0:
            failures.append(f"tree has no {normalized} decision node")
        if result["case"].get("expect_strategy_rows", True):
            if not data or data["strategy_rows"] <= 0:
                failures.append(f"no reported strategy row on {normalized}")
        if configured_board is not None and data and data.get("strategy_rows", 0) > 0:
            missing_board_rows = data.get("missing_board_rows", 0)
            if missing_board_rows:
                failures.append(
                    f"missing board context in {missing_board_rows} strategy row(s) "
                    f"on {normalized}; expected {configured_board}"
                )
            observed_boards = data.get("observed_boards", [])
            if not observed_boards:
                failures.append(
                    f"no observed board identity on {normalized}; "
                    f"expected {configured_board}"
                )
            else:
                mismatches = [
                    board
                    for board in observed_boards
                    if board_identity(str(board)) != expected_board_identity
                ]
                if mismatches:
                    failures.append(
                        f"reported board(s) on {normalized} do not match configured "
                        f"{configured_board}: {mismatches}"
                    )
    return failures


def stable_reproducibility_view(result: dict[str, Any]) -> dict[str, Any]:
    benchmark = result["benchmark"]
    return {
        "actual_iterations": benchmark["actual_iterations"],
        "infosets": benchmark["infosets"],
        "description_infosets": benchmark["description_infosets"],
        "stop_cause": benchmark["stop_cause"],
        "guarantee": benchmark["metrics"]["guarantee"],
        "exploitability_raw": benchmark["metrics"]["exploitability_raw"],
        "exploitability_mbb_per_game": benchmark["metrics"]["exploitability_mbb_per_game"],
        "requested_br_samples": benchmark["metrics"].get("requested_br_samples"),
        "br_samples": benchmark["metrics"]["br_samples"],
        "strategy_fingerprint_sha256": benchmark["report"]["strategy_fingerprint_sha256"],
        "per_street_rows": {
            street: {
                "strategy_rows": values["strategy_rows"],
                "uniform_rows": values["uniform_rows"],
                "non_uniform_rows": values["non_uniform_rows"],
                "missing_board_rows": values.get("missing_board_rows", 0),
                "unique_nodes_with_rows": values["unique_nodes_with_rows"],
                "observed_boards": values.get("observed_boards", []),
            }
            for street, values in benchmark["per_street"].items()
        },
    }


def run_process_with_solve_timing(
    command: list[str], root: Path, stderr_path: Path
) -> tuple[int, str, float, float | None, float | None]:
    process_started_ns = time.perf_counter_ns()
    solve_started_ns: int | None = None
    solve_ended_ns: int | None = None
    stdout_lines: list[str] = []
    with stderr_path.open("w", encoding="utf-8") as stderr_stream:
        process = subprocess.Popen(
            command, cwd=root, text=True, stdout=subprocess.PIPE,
            stderr=stderr_stream, bufsize=1,
        )
        if process.stdout is None:
            raise RuntimeError("failed to capture solver stdout")
        for line in process.stdout:
            observed_ns = time.perf_counter_ns()
            stdout_lines.append(line)
            stripped = line.strip()
            if solve_started_ns is None and stripped == SOLVE_START_MARKER:
                solve_started_ns = observed_ns
            # The solver emits solve_loop_end before the CLI stops/joins its
            # watcher thread. Timestamp that solver-owned boundary so short
            # runs do not include watcher shutdown latency in throughput.
            if solve_ended_ns is None and RE_LOOP_END.search(stripped):
                solve_ended_ns = observed_ns
        process.stdout.close()
        returncode = process.wait()
    process_ended_ns = time.perf_counter_ns()
    process_elapsed_seconds = (process_ended_ns - process_started_ns) / 1_000_000_000.0
    solve_elapsed_seconds = (
        (solve_ended_ns - solve_started_ns) / 1_000_000_000.0
        if solve_started_ns is not None and solve_ended_ns is not None
        and solve_ended_ns >= solve_started_ns else None
    )
    post_solve_elapsed_seconds = (
        (process_ended_ns - solve_ended_ns) / 1_000_000_000.0
        if solve_ended_ns is not None and process_ended_ns >= solve_ended_ns
        else None
    )
    return (
        returncode,
        "".join(stdout_lines),
        process_elapsed_seconds,
        solve_elapsed_seconds,
        post_solve_elapsed_seconds,
    )


def run_once(
    solver: Path,
    root: Path,
    out_dir: Path,
    case: dict[str, Any],
    defaults: dict[str, Any],
    iteration_override: int | None,
    repetition: int,
) -> dict[str, Any]:
    case_id = safe_case_id(case)
    case_dir = out_dir / case_id / f"run-{repetition}"
    case_dir.mkdir(parents=True, exist_ok=True)
    raw_report = case_dir / "solver-report.json"
    stdout_path = case_dir / "stdout.log"
    stderr_path = case_dir / "stderr.log"
    result_path = case_dir / "benchmark.json"
    for stale_path in (raw_report, stdout_path, stderr_path, result_path):
        try:
            stale_path.unlink()
        except FileNotFoundError:
            pass
    tree_path = (root / case["tree"]).resolve()
    node_streets, node_actors, decisions = tree_nodes(
        tree_path, root_to_act=case.get("to_act")
    )
    command = build_command(solver, root, raw_report, case, defaults, iteration_override)
    requested_iterations = iteration_override or int(
        case.get("iterations", defaults.get("iterations", 2000))
    )
    requested_br_samples = int(case.get("br_samples", defaults.get("br_samples", 16)))
    report_rows = int(case.get("report_rows", defaults.get("report_rows", 0)))
    (
        returncode,
        stdout,
        process_elapsed,
        solve_elapsed,
        post_solve_elapsed,
    ) = run_process_with_solve_timing(command, root, stderr_path)
    stdout_path.write_text(stdout, encoding="utf-8")

    native_report: dict[str, Any] | None = None
    native_report_error: str | None = None
    if raw_report.exists():
        try:
            decoded = json.loads(raw_report.read_text(encoding="utf-8"))
            if isinstance(decoded, dict):
                native_report = decoded
            else:
                native_report_error = "top-level JSON value is not an object"
        except (json.JSONDecodeError, OSError) as exc:
            native_report_error = str(exc)

    benchmark = parse_stdout(
        stdout,
        decisions,
        node_streets,
        process_elapsed,
        solve_elapsed,
        requested_iterations,
        report_rows,
        post_solve_elapsed_seconds=post_solve_elapsed,
        node_actors=node_actors,
    )
    benchmark["metrics"]["requested_br_samples"] = requested_br_samples
    result = {
        "schema": SCHEMA,
        "case": case,
        "command": command,
        "process": {
            "returncode": returncode,
            "stdout": str(stdout_path.relative_to(out_dir)),
            "stderr": str(stderr_path.relative_to(out_dir)),
            "solver_report": str(raw_report.relative_to(out_dir)) if raw_report.exists() else None,
            "solver_report_error": native_report_error,
        },
        "native_solver_report": native_report,
        "benchmark": benchmark,
    }
    result["validation_failures"] = validate_result(result)
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return result


def write_summary(
    out_dir: Path, results: list[dict[str, Any]], repro: dict[str, Any]
) -> None:
    summary = {
        "schema": SUMMARY_SCHEMA,
        "results": [
            {
                "case": result["case"]["id"],
                "returncode": result["process"]["returncode"],
                "validation_failures": result["validation_failures"],
                "benchmark": result["benchmark"],
            }
            for result in results
        ],
        "reproducibility": repro,
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    fields = [
        "case", "game", "street", "iterations", "solve_seconds",
        "process_seconds", "post_solve_seconds", "iterations_per_second",
        "infosets", "description_infosets", "infosets_per_1k_iterations",
        "peak_measured_bytes", "final_memory_bytes", "storage_bytes",
        "adapter_bytes", "bytes_per_infoset", "exploitability_mbb",
        "br_samples_requested", "br_samples_reported", "stop_cause",
        "preflop_rows", "flop_rows", "turn_rows", "river_rows",
        "strategy_fingerprint_sha256", "valid",
    ]
    with (out_dir / "summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for result in results:
            b = result["benchmark"]
            writer.writerow({
                "case": result["case"]["id"],
                "game": result["case"]["game"],
                "street": result["case"].get("street", "full"),
                "iterations": b["actual_iterations"],
                "solve_seconds": b["solve_elapsed_seconds"],
                "process_seconds": f"{b['elapsed_seconds']:.9f}",
                "post_solve_seconds": b["post_solve_elapsed_seconds"],
                "iterations_per_second": b["iterations_per_second"],
                "infosets": b["infosets"],
                "description_infosets": b["description_infosets"],
                "infosets_per_1k_iterations": b["infosets_per_1k_iterations"],
                "peak_measured_bytes": b["memory"]["peak_measured_bytes"],
                "final_memory_bytes": b["memory"]["final_bytes"],
                "storage_bytes": b["memory"]["storage_bytes"],
                "adapter_bytes": b["memory"]["adapter_bytes"],
                "bytes_per_infoset": b["memory"]["bytes_per_infoset"],
                "exploitability_mbb": b["metrics"]["exploitability_mbb_per_game"],
                "br_samples_requested": b["metrics"].get("requested_br_samples"),
                "br_samples_reported": b["metrics"]["br_samples"],
                "stop_cause": b["stop_cause"],
                "preflop_rows": b["per_street"]["PREFLOP"]["strategy_rows"],
                "flop_rows": b["per_street"]["FLOP"]["strategy_rows"],
                "turn_rows": b["per_street"]["TURN"]["strategy_rows"],
                "river_rows": b["per_street"]["RIVER"]["strategy_rows"],
                "strategy_fingerprint_sha256": b["report"]["strategy_fingerprint_sha256"],
                "valid": not result["validation_failures"],
            })


def main() -> int:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest", default=str(root / "benchmarks" / "solver" / "cases.json"),
        help="benchmark case manifest",
    )
    parser.add_argument("--suite", action="append", default=[], help="case tag to run")
    parser.add_argument("--case", action="append", default=[], help="exact case id to run")
    parser.add_argument("--solver", help="path to pe-preflop-solve")
    parser.add_argument("--build-dir", default="build", help="build directory")
    parser.add_argument(
        "--output-dir", default=str(root / "build" / "solver-benchmarks"),
        help="result directory",
    )
    parser.add_argument("--iterations", type=int, help="override iterations for every selected case")
    parser.add_argument("--repeat", type=int, default=1, help="number of runs per case")
    parser.add_argument(
        "--check-reproducibility", action="store_true",
        help="compare deterministic fields across repetitions",
    )
    parser.add_argument(
        "--strict", action="store_true",
        help="return non-zero on validation/reproducibility failures",
    )
    parser.add_argument("--list", action="store_true", help="list selected cases and exit")
    args = parser.parse_args()

    if args.iterations is not None and args.iterations <= 0:
        parser.error("--iterations must be > 0")
    if args.repeat <= 0:
        parser.error("--repeat must be > 0")
    if args.check_reproducibility and args.repeat < 2:
        parser.error("--check-reproducibility requires --repeat >= 2")

    manifest_path = Path(args.manifest)
    if not manifest_path.is_absolute():
        manifest_path = (root / manifest_path).resolve()
    manifest = load_manifest(manifest_path)
    validate_manifest_case_ids(manifest["cases"])
    suites = set(args.suite or ["smoke"])
    names = set(args.case)
    cases = [case for case in manifest["cases"] if case_selected(case, suites, names)]
    if not cases:
        parser.error("no benchmark cases selected")

    if args.list:
        for case in cases:
            print(f"{case['id']}: {case['game']} {case.get('street', 'full')}")
        return 0

    solver = resolve_solver(args, root)
    out_dir = Path(args.output_dir)
    if not out_dir.is_absolute():
        out_dir = (root / out_dir).resolve()
    prepare_output_dir(out_dir, cases)

    selected = {
        "schema": SELECTION_SCHEMA,
        "manifest": str(manifest_path),
        "solver": str(solver),
        "suites": sorted(suites),
        "cases": [case["id"] for case in cases],
        "iteration_override": args.iterations,
        "repeat": args.repeat,
    }
    (out_dir / "selection.json").write_text(
        json.dumps(selected, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    results: list[dict[str, Any]] = []
    by_case: dict[str, list[dict[str, Any]]] = {}
    for case in cases:
        for repetition in range(1, args.repeat + 1):
            print(f"[benchmark] {case['id']} run {repetition}/{args.repeat}", flush=True)
            result = run_once(
                solver, root, out_dir, case, manifest.get("defaults", {}),
                args.iterations, repetition,
            )
            results.append(result)
            by_case.setdefault(case["id"], []).append(result)
            b = result["benchmark"]
            ips = b["iterations_per_second"]
            ips_text = f"{ips:.1f}" if ips is not None else "n/a"
            print(
                f"  iterations={b['actual_iterations']} infosets={b['infosets']} "
                f"solve_seconds={b['solve_elapsed_seconds'] or 0:.3f} "
                f"process_seconds={b['elapsed_seconds']:.3f} "
                f"ips={ips_text} "
                f"stop={b['stop_cause']} valid={not result['validation_failures']}",
                flush=True,
            )

    repro: dict[str, Any] = {}
    repro_failures: list[str] = []
    if args.check_reproducibility:
        for case_id, case_runs in by_case.items():
            views = [stable_reproducibility_view(result) for result in case_runs]
            reference = views[0]
            mismatches = [
                index + 1 for index, view in enumerate(views[1:], start=1)
                if view != reference
            ]
            passed = not mismatches
            repro[case_id] = {
                "passed": passed,
                "runs": len(views),
                "mismatching_runs": mismatches,
                "reference": reference,
            }
            if not passed:
                repro_failures.append(
                    f"{case_id}: deterministic fields differ in runs {mismatches}"
                )

    write_summary(out_dir, results, repro)
    validation_failures = [
        f"{result['case']['id']}: {failure}"
        for result in results for failure in result["validation_failures"]
    ]
    all_failures = validation_failures + repro_failures
    if all_failures:
        print("\nFailures:", file=sys.stderr)
        for failure in all_failures:
            print(f"  - {failure}", file=sys.stderr)
    print(f"\nResults: {out_dir}")
    if args.strict and all_failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
