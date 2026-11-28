#!/usr/bin/env python3
"""Issue #332: the four-card turn at 512 combos is the one configuration the
sweep's missing tail moved.

The sweep used to read its subset table before folding in the opponents above
every hero, so the compatible mass came back short. Every reference PLO4
configuration is blind to that: the certifier, the river bench and the turn at
2,000 and 2,091 combos have no opponent above every hero and are bit-identical
with and without the correction. This configuration is not. Locking it keeps
the tail of `out_mass` covered end to end, through the turn terminal and the
sweep that feeds it.

The fingerprint is checked exactly because it is stable across builds: the
same run returns `aaff858c1a112dfb` on Apple silicon with clang, on x86_64
under Rosetta and on Linux amd64 with gcc 13. The floating-point fields keep a
tolerance for the re-associated sums elsewhere in the report. The short
mass the bug produced (`2355807.320556641`, 7.5e-5 below) is reported in the
failure message so a regression is recognisable on sight.

Usage: plo4_turn_tail_fingerprint.py <pe-omaha-turn-bench>
"""

import json
import subprocess
import sys

FINGERPRINT = "aaff858c1a112dfb"
WEIGHT_SUM = 2355983.720703125
VALUES = [0.574786242525, -0.574786242525]
SHORT_WEIGHT_SUM = 2355807.320556641
PATH = "plo4_sweep"
RELATIVE_TOLERANCE = 1e-9


def fail(message):
    print(message, file=sys.stderr)
    return 1


def close(actual, expected):
    return abs(actual - expected) <= RELATIVE_TOLERANCE * abs(expected)


def main(argv):
    if len(argv) != 2:
        return fail("usage: plo4_turn_tail_fingerprint.py <pe-omaha-turn-bench>")

    completed = subprocess.run(
        [argv[1], "--holes", "4", "--combos", "512", "--repetitions", "1"],
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        return fail(
            "pe-omaha-turn-bench exited %d\n%s"
            % (completed.returncode, completed.stderr.strip())
        )
    try:
        report = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        return fail("bench report is not JSON: %s" % error)

    failures = 0
    if report.get("holes") != 4 or report.get("combos_per_player") != 512:
        print(
            "the bench ran a different configuration: holes %r, combos %r"
            % (report.get("holes"), report.get("combos_per_player")),
            file=sys.stderr,
        )
        failures += 1

    river = report.get("river_reference") or {}
    if river.get("path") != PATH:
        print(
            "river path is %r, expected %r: the turn left the sweep"
            % (river.get("path"), PATH),
            file=sys.stderr,
        )
        failures += 1

    weight_sum = report.get("weight_sum")
    if not isinstance(weight_sum, (int, float)):
        print("weight_sum missing from the report", file=sys.stderr)
        failures += 1
    elif not close(weight_sum, WEIGHT_SUM):
        note = ""
        if close(weight_sum, SHORT_WEIGHT_SUM):
            note = " (the pre-fix short mass: the sweep dropped the tail)"
        print(
            "weight_sum %.17g, expected %.17g%s"
            % (weight_sum, WEIGHT_SUM, note),
            file=sys.stderr,
        )
        failures += 1

    values = report.get("values") or []
    if len(values) != 2 or not all(close(a, e) for a, e in zip(values, VALUES)):
        print("values %r, expected %r" % (values, VALUES), file=sys.stderr)
        failures += 1

    if report.get("values_fingerprint") != FINGERPRINT:
        print(
            "values_fingerprint %r, expected %r"
            % (report.get("values_fingerprint"), FINGERPRINT),
            file=sys.stderr,
        )
        failures += 1

    if failures:
        return fail("%d check(s) failed" % failures)
    print("plo4_turn_tail_fingerprint: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
