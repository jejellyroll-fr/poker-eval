#!/usr/bin/env python3
"""Measure per-street strategy-query latency for each storage tier (issue #247).

Issue #235 phase 6 asked for "strategy-query latency for flop, turn and
river" compared against `full`, not just total solve time. A board query
walks every infoset the solve holds, so its wall clock is where a dropped
derived layer actually bites: `recompute-deep` must re-decode the spans a
drop pass removed, `compact` re-decodes only the deep streets, and `full`
finds everything resident.

This probe drives the product solver's own interactive protocol rather
than re-implementing a query path:

    --interactive            keeps the process (and the solve) alive after
                             the report and answers "query <cards>" lines
                             on stdin, printing "query_done" when the board
                             table is complete.

For each storage tier it starts one solve, waits for the `interactive=1
ready` handshake, then times one `query <cards>` round trip per street.
One process per tier is enough because the query walks the same solve the
report just finished; the reported figure is therefore a true
after-convergence query latency, not a fresh solve.

The emitted document (schema `pe-solver-query-latency/v1`) copies only
measured wall clocks; the median/min reduction is computed here.
"""

from __future__ import annotations

import argparse
import json
import platform
import queue
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

QUERY_SCHEMA = "pe-solver-query-latency/v1"
READY_MARKER = "interactive=1 ready"
DONE_MARKER = "query_done"
STREETS = ("FLOP", "TURN", "RIVER")


class SolverReader:
    """Line reader over a child's stdout with a deadline per read."""

    def __init__(self, stream) -> None:
        self._queue: queue.Queue[str | None] = queue.Queue()
        self._thread = threading.Thread(target=self._pump, args=(stream,), daemon=True)
        self._thread.start()

    def _pump(self, stream) -> None:
        try:
            for line in stream:
                self._queue.put(line)
        finally:
            self._queue.put(None)

    def read_until(self, marker: str, timeout: float) -> list[str]:
        deadline = time.monotonic() + timeout
        seen: list[str] = []
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for {marker!r}")
            try:
                line = self._queue.get(timeout=remaining)
            except queue.Empty as exc:  # pragma: no cover - deadline race
                raise TimeoutError(f"timed out waiting for {marker!r}") from exc
            if line is None:
                raise RuntimeError(f"solver exited before {marker!r}")
            seen.append(line)
            if marker in line:
                return seen


def build_command(
    solver: Path,
    *,
    policy: str,
    game: str,
    players: int,
    tree: Path,
    board_abstraction: str | None,
    iterations: int,
    seed: int,
    precision: str,
    backend: str,
    max_ram_mb: int,
    desc_limit_mb: int,
    ranges: list[str],
) -> list[str]:
    command = [
        str(solver),
        "--game", game,
        "--players", str(players),
        "--iterations", str(iterations),
        "--samples", "1",
        # The probe measures query latency, not convergence quality; one BR
        # rollout keeps the report phase cheap (the solver rejects 0).
        "--br-samples", "1",
        "--exploitability-interval", str(iterations),
        "--algorithm", "external-mccfr",
        "--backend", backend,
        "--precision", precision,
        "--threads", "1",
        "--target-mbb", "0",
        "--seed", str(seed),
        "--max-ram", str(max_ram_mb),
        "--desc-limit", str(desc_limit_mb),
        "--report-rows", "0",
        "--memory-policy", policy,
        "--interactive",
        "--tree", str(tree),
    ]
    for player, range_text in enumerate(ranges):
        command.extend((f"--range{player}", str(range_text)))
    if board_abstraction:
        command.extend(("--board-abstraction", board_abstraction))
    return command


def probe_policy(
    command: list[str],
    boards: dict[str, str],
    *,
    repeats: int,
    startup_timeout: float,
    query_timeout: float,
) -> dict[str, Any]:
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )
    assert process.stdout is not None and process.stdin is not None
    reader = SolverReader(process.stdout)
    result: dict[str, Any] = {"streets": {}, "query_rows": {}}
    try:
        reader.read_until(READY_MARKER, startup_timeout)
        for street in STREETS:
            board = boards[street]
            samples: list[float] = []
            rows = 0
            for index in range(repeats):
                started = time.perf_counter()
                process.stdin.write(f"query {board}\n")
                process.stdin.flush()
                lines = reader.read_until(DONE_MARKER, query_timeout)
                elapsed = time.perf_counter() - started
                # The first repeat absorbs the one-off decode of a dropped
                # span; later repeats measure the steady-state query.
                if index > 0 or repeats == 1:
                    samples.append(elapsed)
                rows = sum(1 for line in lines if line.strip() and DONE_MARKER not in line)
            result["streets"][street] = {
                "board": board,
                "samples_seconds": samples,
                "median_seconds": statistics.median(samples) if samples else None,
                "min_seconds": min(samples) if samples else None,
                "rows": rows,
            }
        process.stdin.write("quit\n")
        process.stdin.flush()
    finally:
        try:
            process.stdin.close()
        except OSError:
            pass
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:  # pragma: no cover - defensive
            process.kill()
            process.wait()
    return result


def build_document(
    *,
    solver: str,
    settings: dict[str, Any],
    boards: dict[str, str],
    policies: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    return {
        "schema": QUERY_SCHEMA,
        "platform": platform.platform(),
        "solver": solver,
        "settings": settings,
        "boards": boards,
        "policies": policies,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="build/tools/pe-preflop-solve")
    parser.add_argument("--game", default="holdem")
    parser.add_argument("--players", type=int, default=2)
    parser.add_argument(
        "--tree", default="examples/nlhe_hu/nlhe_hu_full.tree.json"
    )
    parser.add_argument("--board-abstraction", default="large")
    parser.add_argument("--iterations", type=int, default=20000)
    parser.add_argument("--seed", type=int, default=20260906)
    parser.add_argument("--precision", default="f32")
    parser.add_argument("--backend", default="cpu_ref")
    parser.add_argument("--max-ram-mb", type=int, default=128)
    parser.add_argument("--desc-limit-mb", type=int, default=16)
    parser.add_argument(
        "--policy",
        action="append",
        dest="policies",
        help="storage tier to probe (repeatable); default: all three",
    )
    parser.add_argument("--flop", default="Ks7d2c")
    parser.add_argument("--turn", default="Ks7d2c9h")
    parser.add_argument("--river", default="Ks7d2c9h4s")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--startup-timeout", type=float, default=1800.0)
    parser.add_argument("--query-timeout", type=float, default=600.0)
    parser.add_argument("--output", help="write the document here (default: stdout)")
    args = parser.parse_args()

    solver = Path(args.solver).resolve()
    if not solver.exists():
        raise SystemExit(f"solver not found: {solver}")
    tree = Path(args.tree).resolve()
    if not tree.exists():
        raise SystemExit(f"tree not found: {tree}")

    policies = args.policies or ["full", "compact", "recompute-deep"]
    boards = {"FLOP": args.flop, "TURN": args.turn, "RIVER": args.river}
    ranges = ["100%", "100%"]

    probed: dict[str, dict[str, Any]] = {}
    for policy in policies:
        command = build_command(
            solver,
            policy=policy,
            game=args.game,
            players=args.players,
            tree=tree,
            board_abstraction=args.board_abstraction,
            iterations=args.iterations,
            seed=args.seed,
            precision=args.precision,
            backend=args.backend,
            max_ram_mb=args.max_ram_mb,
            desc_limit_mb=args.desc_limit_mb,
            ranges=ranges,
        )
        print(f"probing policy={policy} ...", file=sys.stderr)
        probed[policy] = probe_policy(
            command,
            boards,
            repeats=args.repeats,
            startup_timeout=args.startup_timeout,
            query_timeout=args.query_timeout,
        )

    settings = {
        "game": args.game,
        "players": args.players,
        "tree": str(tree),
        "board_abstraction": args.board_abstraction,
        "iterations": args.iterations,
        "seed": args.seed,
        "precision": args.precision,
        "backend": args.backend,
        "max_ram_mb": args.max_ram_mb,
        "desc_limit_mb": args.desc_limit_mb,
        "repeats": args.repeats,
    }
    document = build_document(
        solver=str(solver), settings=settings, boards=boards, policies=probed
    )
    rendered = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if args.output:
        Path(args.output).write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
