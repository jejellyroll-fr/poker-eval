#!/usr/bin/env python3
"""Regression coverage for solve-only wall-clock timing."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_benchmarks as bench


class _FakeStdout:
    def __init__(self, lines: list[str]) -> None:
        self._lines = lines

    def __iter__(self):
        return iter(self._lines)

    def close(self) -> None:
        pass


class _FakeProcess:
    def __init__(self, lines: list[str]) -> None:
        self.stdout = _FakeStdout(lines)

    def wait(self) -> int:
        return 0


class SolveTimingTests(unittest.TestCase):
    def test_timer_ends_at_solve_loop_end_before_cli_shutdown(self) -> None:
        lines = [
            "solver created\n",
            (
                "telemetry solve_loop_end cause=max_iterations iteration=64 "
                "memory_mb=1.0 storage_mb=0.5 adapter_mb=0.5\n"
            ),
            "solver_phase=complete stop_reason=max_iterations report=starting\n",
        ]
        # process start, solver-created, loop-end, CLI report boundary, process end
        timestamps = iter(
            [
                0,
                6_000_000,
                12_000_000,
                57_000_000,
                60_000_000,
            ]
        )

        with tempfile.TemporaryDirectory() as tmpdir:
            stderr_path = Path(tmpdir) / "stderr.log"
            with mock.patch.object(
                bench.subprocess,
                "Popen",
                return_value=_FakeProcess(lines),
            ), mock.patch.object(
                bench.time,
                "perf_counter_ns",
                side_effect=lambda: next(timestamps),
            ):
                returncode, stdout, process_elapsed, solve_elapsed = (
                    bench.run_process_with_solve_timing(
                        ["fake-solver"], Path(tmpdir), stderr_path
                    )
                )

        self.assertEqual(returncode, 0)
        self.assertIn("solve_loop_end", stdout)
        self.assertAlmostEqual(process_elapsed, 0.060)
        self.assertAlmostEqual(solve_elapsed, 0.006)
        # If the old solver_phase=complete marker were still used, this would
        # be roughly 0.051 seconds instead of 0.006 seconds.
        self.assertLess(solve_elapsed, 0.020)


if __name__ == "__main__":
    unittest.main()
