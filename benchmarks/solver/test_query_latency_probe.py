#!/usr/bin/env python3
"""Focused tests for the per-street query-latency probe (issue #247).

The probe drives the solver's interactive protocol and must not mis-parse
it: a board query ends at the `query_done` marker, the first repeat of a
street absorbs the one-off re-decode of a dropped span, and the emitted
document must carry every measured sample. These tests pin that contract
against a scripted child process so a protocol drift fails fast without a
multi-minute solve.
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
    def __init__(self, lines: list[str]) -> None:
        self.stdout = FakeStdout(lines)
        self.stdin = FakeStdin()
        self.waited = False

    def wait(self, timeout: float | None = None) -> int:
        self.waited = True
        return 0

    def kill(self) -> None:  # pragma: no cover - defensive path
        pass


def scripted_lines(queries: int, rows_per_query: int = 2) -> list[str]:
    lines = [f"{probe.READY_MARKER} interrupted=0\n"]
    for _ in range(queries):
        for index in range(rows_per_query):
            lines.append(f"strategy_row_{index}\n")
        lines.append(f"{probe.DONE_MARKER}\n")
    return lines


class BuildCommandTests(unittest.TestCase):
    def test_command_carries_policy_and_interactive_flag(self) -> None:
        command = probe.build_command(
            Path("/tmp/pe-preflop-solve"),
            policy="recompute-deep",
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
            ranges=["100%", "100%"],
        )
        self.assertIn("--interactive", command)
        self.assertEqual(command[command.index("--memory-policy") + 1], "recompute-deep")
        self.assertEqual(command[command.index("--board-abstraction") + 1], "large")
        self.assertEqual(command[command.index("--tree") + 1], "/tmp/tree.json")
        self.assertEqual(command.count("--range0"), 1)
        self.assertEqual(command.count("--range1"), 1)

    def test_abstraction_is_optional(self) -> None:
        command = probe.build_command(
            Path("/tmp/pe-preflop-solve"),
            policy="full",
            game="holdem",
            players=2,
            tree=Path("/tmp/tree.json"),
            board_abstraction=None,
            iterations=100,
            seed=1,
            precision="f64",
            backend="cpu_ref",
            max_ram_mb=64,
            desc_limit_mb=8,
            ranges=["100%", "100%"],
        )
        self.assertNotIn("--board-abstraction", command)


class ProbePolicyTests(unittest.TestCase):
    def _run(self, lines: list[str], repeats: int = 3) -> tuple[dict, FakeProcess]:
        process = FakeProcess(lines)
        with mock.patch.object(probe.subprocess, "Popen", return_value=process):
            result = probe.probe_policy(
                ["solver"],
                {"FLOP": "Ks7d2c", "TURN": "Ks7d2c9h", "RIVER": "Ks7d2c9h4s"},
                repeats=repeats,
                startup_timeout=5.0,
                query_timeout=5.0,
            )
        return result, process

    def test_reads_one_query_per_street_and_records_samples(self) -> None:
        # 3 streets x `repeats` queries, each terminated by query_done.
        result, process = self._run(scripted_lines(9), repeats=3)

        self.assertEqual(set(result["streets"]), set(probe.STREETS))
        for street in probe.STREETS:
            entry = result["streets"][street]
            # The first repeat is dropped as warmup, leaving repeats-1 samples.
            self.assertEqual(len(entry["samples_seconds"]), 2)
            self.assertIsNotNone(entry["median_seconds"])
            self.assertIsNotNone(entry["min_seconds"])
            self.assertEqual(entry["rows"], 2)
        self.assertIn("quit\n", process.stdin.writes)
        self.assertTrue(process.waited)

    def test_single_repeat_keeps_its_only_sample(self) -> None:
        result, _ = self._run(scripted_lines(3), repeats=1)
        for street in probe.STREETS:
            self.assertEqual(len(result["streets"][street]["samples_seconds"]), 1)

    def test_stops_at_done_marker_not_at_stream_end(self) -> None:
        # Extra trailing lines after the last query_done must not be consumed
        # into a query's row count.
        lines = scripted_lines(3, rows_per_query=1) + ["trailing_noise\n"] * 5
        result, _ = self._run(lines, repeats=1)
        for street in probe.STREETS:
            self.assertEqual(result["streets"][street]["rows"], 1)

    def test_stream_ending_before_handshake_is_an_error(self) -> None:
        # A child that closes its stdout without the handshake must fail
        # loudly rather than hang the probe.
        process = FakeProcess(["some unrelated line\n"])
        with mock.patch.object(probe.subprocess, "Popen", return_value=process):
            with self.assertRaises(RuntimeError):
                probe.probe_policy(
                    ["solver"],
                    {"FLOP": "Ks7d2c", "TURN": "Ks7d2c9h", "RIVER": "Ks7d2c9h4s"},
                    repeats=1,
                    startup_timeout=0.05,
                    query_timeout=0.05,
                )

    def test_open_stream_without_handshake_times_out(self) -> None:
        class BlockingStdout:
            def __iter__(self):
                # Never yields and never ends: the deadline must fire.
                while True:
                    import time as _time

                    _time.sleep(0.01)

        process = FakeProcess([])
        process.stdout = BlockingStdout()
        with mock.patch.object(probe.subprocess, "Popen", return_value=process):
            with self.assertRaises(TimeoutError):
                probe.probe_policy(
                    ["solver"],
                    {"FLOP": "Ks7d2c", "TURN": "Ks7d2c9h", "RIVER": "Ks7d2c9h4s"},
                    repeats=1,
                    startup_timeout=0.05,
                    query_timeout=0.05,
                )


class BuildDocumentTests(unittest.TestCase):
    def test_document_schema_and_payload(self) -> None:
        document = probe.build_document(
            solver="/tmp/pe-preflop-solve",
            settings={"iterations": 20000},
            boards={"FLOP": "Ks7d2c", "TURN": "Ks7d2c9h", "RIVER": "Ks7d2c9h4s"},
            policies={"full": {"streets": {}}},
        )
        self.assertEqual(document["schema"], probe.QUERY_SCHEMA)
        self.assertEqual(document["solver"], "/tmp/pe-preflop-solve")
        self.assertEqual(document["settings"]["iterations"], 20000)
        self.assertIn("platform", document)
        # The document must round-trip through JSON for the committed artifact.
        self.assertEqual(
            json.loads(json.dumps(document))["boards"]["RIVER"], "Ks7d2c9h4s"
        )


if __name__ == "__main__":
    unittest.main()
