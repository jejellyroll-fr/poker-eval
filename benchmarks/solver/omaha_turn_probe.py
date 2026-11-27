#!/usr/bin/env python3
"""Guard the PLO6 turn terminal against losing design A (issue #332).

`pe-omaha-turn-bench` prices the reference configuration of the turn terminal:
PLO6, two thousand combos per range, the board `AsKd7c2h`, every legal deal
visited.  This probe runs that configuration and refuses the run when either
half of the design A gain is gone:

- what the terminal visits and pays is compared exactly -- the values
  fingerprint, the deal count, the deal mass, the river count -- so a change of
  order, weight or payoff fails before any clock is read;
- the terminal phase and the cost of one visit are compared against the anchors
  below with a 2x envelope.  The anchors are the figures measured on the shared
  runner that executes this probe, not on a developer machine: a CI host is
  slower than a workstation, and the regression being caught is a factor of
  fifty, not a few percent.

Both measured figures are printed whether the gate passes or fails, so a runner
that drifts says so in the log instead of turning into a mystery.

The reason the envelope is worth having at all: before design A the same turn
took 53.94 s and 1087 ns per deal (docs/cfr/guides/plo_turn_pair_walk.md), which
is 25x past the envelope even after leaving room for a slower host.
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path
from typing import Any

# Reference configuration: PLO6, 2000 combos per range, no dead cards.
HOLES = 6
COMBOS = 2000
REPETITIONS = 3
FINGERPRINT = "ac3016522c620075"
DEALS = 50_849_064
RIVERS = 48
MASS = 19_821_384.246093750
# Anchors measured on the ubuntu-latest runner that executes this probe, in
# seconds and in nanoseconds per visit (run 45987938776: 1.785 s and 32.14 ns).
# They are the runner's numbers rather than a workstation's, because the gate
# has to leave room for the host it runs on and not for the one it was written
# on: twice these is still fifteen times below the pre-design-A figures.
TURN_SECONDS_ANCHOR = 1.78
VISIT_NS_ANCHOR = 32.1
ENVELOPE = 2.0


class RegressionError(ValueError):
    """The reference turn no longer matches what design A bought."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RegressionError(message)


def _number(value: Any, label: str) -> float:
    _require(
        isinstance(value, (int, float)) and not isinstance(value, bool),
        f"{label} must be a number",
    )
    number = float(value)
    _require(math.isfinite(number) and number >= 0.0,
             f"{label} must be finite and non-negative")
    return number


def validate(report: Any, envelope: float) -> None:
    """Reject a report whose values or whose time moved out of the envelope."""
    _require(isinstance(report, dict), "the bench report must be a JSON object")
    assert isinstance(report, dict)
    _require(report.get("holes") == HOLES, f"holes must be {HOLES}")
    _require(report.get("combos_per_player") == COMBOS,
             f"combos_per_player must be {COMBOS}")
    _require(report.get("hilo8") is False, "this gate prices the high-only turn")
    _require(report.get("deterministic_across_repetitions") is True,
             "the three repetitions did not agree")
    _require(report.get("rivers_priced") == RIVERS,
             f"the turn must price {RIVERS} rivers, got {report.get('rivers_priced')!r}")
    _require(report.get("values_fingerprint") == FINGERPRINT,
             "the values fingerprint moved: "
             f"{report.get('values_fingerprint')!r} instead of {FINGERPRINT!r}")
    mass = _number(report.get("weight_sum"), "weight_sum")
    _require(abs(mass - MASS) <= 1e-9 * MASS,
             f"the deal mass moved: {mass!r} instead of {MASS!r}")

    profile = report.get("profile")
    _require(isinstance(profile, dict), "the run must be taken with --profile")
    assert isinstance(profile, dict)
    _require(profile.get("deals") == DEALS,
             f"the walk must visit {DEALS} deals, got {profile.get('deals')!r}")
    _require(profile.get("counts_stable_across_repetitions") is True,
             "the profile counters moved between repetitions")

    probe = report.get("probe")
    _require(isinstance(probe, dict),
             "the run must be taken with --frozen-strength")
    assert isinstance(probe, dict)
    _require(probe.get("matches_live") is True,
             "the frozen-strength walk disagrees with the live terminal")
    _require(probe.get("deals") == DEALS,
             f"the probe counted {probe.get('deals')!r} deals")

    seconds = report.get("turn_seconds")
    _require(isinstance(seconds, dict), "turn_seconds must be an object")
    assert isinstance(seconds, dict)
    turn = _number(seconds.get("min"), "turn_seconds.min")
    visits = _number(probe.get("nanoseconds_per_visit"),
                     "probe.nanoseconds_per_visit")

    turn_budget = envelope * TURN_SECONDS_ANCHOR
    visit_budget = envelope * VISIT_NS_ANCHOR
    print(f"terminal phase: {turn:.3f} s of {turn_budget:.3f} s "
          f"(anchor {TURN_SECONDS_ANCHOR} s, envelope {envelope}x)")
    print(f"per visit: {visits:.2f} ns of {visit_budget:.2f} ns "
          f"(anchor {VISIT_NS_ANCHOR} ns, envelope {envelope}x)")
    _require(turn <= turn_budget,
             f"the terminal phase took {turn:.3f} s, past the {turn_budget:.3f} s "
             "envelope")
    _require(visits <= visit_budget,
             f"a visit costs {visits:.2f} ns, past the {visit_budget:.2f} ns "
             "envelope")
    print(f"fingerprint {FINGERPRINT} over {DEALS} deals, {RIVERS} rivers")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bench", type=Path,
                        help="the pe-omaha-turn-bench binary to run")
    parser.add_argument("--report", type=Path,
                        help="validate an existing report instead of running")
    parser.add_argument("--output", type=Path,
                        help="where to keep the report the bench printed")
    parser.add_argument("--envelope", type=float, default=ENVELOPE,
                        help=f"time envelope as a multiple of the anchor "
                             f"(default {ENVELOPE})")
    args = parser.parse_args(argv)

    try:
        if args.report is not None:
            text = args.report.read_text()
        else:
            if args.bench is None:
                parser.error("give --bench or --report")
            command = [
                str(args.bench),
                "--holes", str(HOLES),
                "--combos", str(COMBOS),
                "--profile",
                "--frozen-strength",
                "--repetitions", str(REPETITIONS),
            ]
            print("running " + " ".join(command), flush=True)
            completed = subprocess.run(command, check=True, text=True,
                                       capture_output=True)
            text = completed.stdout
            if args.output is not None:
                args.output.write_text(text)
        validate(json.loads(text), args.envelope)
    except RegressionError as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1
    except (OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1
    print("the reference turn keeps its values and its design A speed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
