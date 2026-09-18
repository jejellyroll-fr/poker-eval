#!/usr/bin/env python3
"""Focused tests for the per-street query-latency probe (issue #247).

The probe drives the solver's interactive protocol and must not mis-parse
it. Three properties are load-bearing and pinned here against a scripted
child process:

- a query's row count comes from the solver's own `board_query_rows=`
  marker, not from counting output lines (the stream also carries
  `ev_update` lines, a range grid and completion markers);
- the first query in a process is reported as cold and the rest as warm,
  because only the first pays the re-materialisation of a dropped span;
- a solver that exits non-zero must fail the probe rather than publish
  latency data that looks valid.
"""

from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import query_latency_probe as probe


class FakeStdout:
    def __init__(self, lines: list[str]) -> None:
        self._lines = list(lines)

    def __iter__(self):
        return iter(self._lines)


class FakeStdin:
    def __init__(self) -> None:
        self.writes: list[str] = []
        self.closed = False

    def write(self, text: str) -> int:
        self.writes.append(text)
        return len(text)

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True


class FakeProcess:
    def __init__(self, lines: list[str], returncode: int = 0) -> None:
        self.stdout = FakeStdout(lines)
        self.stdin = FakeStdin()
        self.returncode = returncode
        self.waited = False

    def wait(self, timeout: float | None = None) -> int:
        self.waited = True
        return self.returncode

    def kill(self) -> None:  # pragma: no cover - defensive path
        pass


def scripted_lines(queries: int, rows_per_query: int = 2) -> list[str]:
    """One query's output: rows, ev_updates, grid, markers, then query_done."""
    lines = [f"{probe.READY_MARKER} interrupted=0\n"]
    for _ in range(queries):
        for index in range(rows_per_query):
            lines.append(f"AhAs\t7\tP1\tCALL=50.0%\tCALL=pending\tKs7d2c\n")
            lines.append(f"ev_update\tAhAs\t7\tP1\tCALL=1.00\n")
        # The grid and the authoritative row marker.
        lines.append("RANGE GRID (highest-frequency action)\n")
        for _ in range(13):
            lines.append("A  R R R R R R R R R R R R R \n")
        lines.append(f"board_query_rows={rows_per_query}\n")
        lines.append(f"report_phase=complete rows={rows_per_query}\n")
        lines.append(f"{probe.DONE_MARKER}\n")
    return lines


def build_command(**overrides):
    kwargs = dict(
        solver=Path("/tmp/pe-preflop-solve"),
        policy="full",
        game="holdem",
        players=2,
        tree=Path("/tmp/tree.json"),
        board_abstraction="large",
        iterations=20000,
        seed=7,
        precision="f32",
        backend="cpu_ref",
        max_ram_mb=128,
        desc_limit_mb=16,
        startup_report_rows=1,
        ranges=["100%", "100%"],
    )
    kwargs.update(overrides)
    return probe.build_command(**kwargs)


class BuildCommandTests(unittest.TestCase):
    def test_command_carries_policy_interactive_and_capped_report(self) -> None:
        command = build_command(policy="recompute-deep", startup_report_rows=1)
        self.assertIn("--interactive", command)
        self.assertEqual(command[command.index("--memory-policy") + 1], "recompute-deep")
        self.assertEqual(command[command.index("--board-abstraction") + 1], "large")
        self.assertEqual(command[command.index("--tree") + 1], "/tmp/tree.json")
        # The startup report must stay capped so it cannot re-materialise the
        # spans a tier evicted before the timed query.
        self.assertEqual(command[command.index("--report-rows") + 1], "1")
        self.assertEqual(command.count("--range0"), 1)
        self.assertEqual(command.count("--range1"), 1)

    def test_abstraction_is_optional(self) -> None:
        command = build_command(board_abstraction=None)
        self.assertNotIn("--board-abstraction", command)


class QueryRowsTests(unittest.TestCase):
    def test_reads_the_solver_row_marker(self) -> None:
        lines = ["junk\n", "board_query_rows=3229\n", "report_phase=complete\n"]
        self.assertEqual(probe.query_rows(lines), 3229)

    def test_absent_marker_is_none(self) -> None:
        self.assertIsNone(probe.query_rows(["row\n", "ev_update\n"]))


class ProbeStreetTests(unittest.TestCase):
    def _run(self, lines: list[str], repeats: int = 3, returncode: int = 0):
        process = FakeProcess(lines, returncode=returncode)
        with mock.patch.object(probe.subprocess, "Popen", return_value=process):
            result = probe.probe_street(
                ["solver"], "Ks7d2c", repeats=repeats,
                startup_timeout=5.0, query_timeout=5.0,
            )
        return result, process

    def test_first_query_is_cold_and_the_rest_warm(self) -> None:
        result, process = self._run(scripted_lines(3), repeats=3)
        self.assertIsNotNone(result["cold_seconds"])
        self.assertEqual(len(result["warm_samples_seconds"]), 2)
        self.assertIsNotNone(result["warm_median_seconds"])
        self.assertIn("quit\n", process.stdin.writes)
        self.assertTrue(process.waited)

    def test_rows_come_from_the_marker_not_the_line_count(self) -> None:
        # Each query emits far more lines than rows; the marker is authoritative.
        result, _ = self._run(scripted_lines(1, rows_per_query=7), repeats=1)
        self.assertEqual(result["rows"], 7)
        self.assertEqual(result["warm_samples_seconds"], [])
        self.assertIsNotNone(result["cold_seconds"])

    def test_single_repeat_yields_cold_only(self) -> None:
        result, _ = self._run(scripted_lines(1), repeats=1)
        self.assertIsNotNone(result["cold_seconds"])
        self.assertEqual(result["warm_samples_seconds"], [])

    def test_nonzero_exit_fails_the_probe(self) -> None:
        with self.assertRaises(RuntimeError) as ctx:
            self._run(scripted_lines(3), repeats=3, returncode=3)
        self.assertIn("exited 3", str(ctx.exception))

    def test_stream_ending_before_handshake_is_an_error(self) -> None:
        process = FakeProcess(["some unrelated line\n"])
        with mock.patch.object(probe.subprocess, "Popen", return_value=process):
            with self.assertRaises(RuntimeError):
                probe.probe_street(
                    ["solver"], "Ks7d2c", repeats=1,
                    startup_timeout=0.05, query_timeout=0.05,
                )

    def test_open_stream_without_handshake_times_out(self) -> None:
        class BlockingStdout:
            def __iter__(self):
                import time as _time

                while True:
                    _time.sleep(0.01)

        process = FakeProcess([])
        process.stdout = BlockingStdout()
        with mock.patch.object(probe.subprocess, "Popen", return_value=process):
            with self.assertRaises(TimeoutError):
                probe.probe_street(
                    ["solver"], "Ks7d2c", repeats=1,
                    startup_timeout=0.05, query_timeout=0.05,
                )


class BuildDocumentTests(unittest.TestCase):
    def test_document_schema_and_payload(self) -> None:
        document = probe.build_document(
            solver="/tmp/pe-preflop-solve",
            settings={"iterations": 20000, "startup_report_rows": 1},
            boards={"FLOP": "Ks7d2c", "TURN": "Ks7d2c9h", "RIVER": "Ks7d2c9h4s"},
            policies={"full": {"streets": {}}},
        )
        self.assertEqual(document["schema"], probe.QUERY_SCHEMA)
        self.assertEqual(document["solver"], "/tmp/pe-preflop-solve")
        self.assertEqual(document["settings"]["startup_report_rows"], 1)
        self.assertIn("platform", document)
        self.assertEqual(
            json.loads(json.dumps(document))["boards"]["RIVER"], "Ks7d2c9h4s"
        )


if __name__ == "__main__":
    unittest.main()
