#!/usr/bin/env python3
"""Report windows in which the arm64 native-test runner stopped sampling.

`Native Tests (arm64)` prints `== HH:MM:SS`, then the load average, `vm_stat`
and the busiest processes, every two seconds on a nightly or a manual
dispatch (see .github/workflows/macos-arm64.yml). A window without a sample
is a window in which that runner made no progress: run 44828726427 lost 66 s
and 74 s that way, and ctest charged the whole window to every case it caught
in it (docs/ci.md).

Usage:
    check_arm64_vm_stalls.py LOG [--threshold SECONDS] [--summary FILE]

Exit status: 0 no window, 1 at least one window, 2 no sample in the log.
"""

import argparse
import datetime as dt
import re
import sys

# A log line from the jobs API, e.g.
#   2026-11-22T08:19:31.5746010Z == 08:19:31
SAMPLE = re.compile(
    r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?)Z"
    r" == \d{2}:\d{2}:\d{2}\s*$"
)


def samples(lines):
    """Return (timestamp, index) for each sample marker, in log order."""
    found = []
    for index, line in enumerate(lines):
        match = SAMPLE.match(line)
        if match:
            found.append((_timestamp(match.group(1)), index))
    return found


def _timestamp(text):
    # The log keeps microseconds, the sample line keeps seconds; both are UTC.
    return dt.datetime.fromisoformat(text.split(".")[0])


def blocks(lines, marks):
    """Return each sample with the lines its loop printed after it."""
    out = []
    for position, (stamp, index) in enumerate(marks):
        end = (
            marks[position + 1][1]
            if position + 1 < len(marks)
            else len(lines)
        )
        out.append((stamp, lines[index + 1:end]))
    return out


def stalls(lines, threshold):
    """Return the windows longer than `threshold` seconds between samples."""
    found = blocks(lines, samples(lines))
    windows = []
    for before, after in zip(found, found[1:]):
        gap = (after[0] - before[0]).total_seconds()
        if gap > threshold:
            windows.append((before[0], after[0], gap, before[1]))
    return found, windows


# A window can span a minute of unrelated ctest output; the figures that matter
# are the ones the sampler printed last, so both reports stop after this many
# lines and say so.
MAX_BLOCK = 40
MAX_WINDOWS = 12


def _clip(block):
    if len(block) <= MAX_BLOCK:
        return list(block)
    return list(block[:MAX_BLOCK]) + [
        "... %d more line(s)" % (len(block) - MAX_BLOCK)
    ]


def report(found, windows, threshold):
    if not found:
        return "No VM sample in this log: the sampler did not run."
    out = [
        "%d sample(s) every two seconds; reporting windows over %g s."
        % (len(found), threshold)
    ]
    for start, end, gap, block in windows[:MAX_WINDOWS]:
        out.append("")
        out.append(
            "stall %s -> %s (%d s)" % (start.isoformat(), end.isoformat(), gap)
        )
        out.extend("  " + line for line in _clip(block))
    if not windows:
        out.append("No window over %g s." % threshold)
    elif len(windows) > MAX_WINDOWS:
        out.append("")
        out.append(
            "%d window(s) in total; the first %d are the ones above."
            % (len(windows), MAX_WINDOWS)
        )
    return "\n".join(out)


def summary(found, windows, threshold):
    if not windows:
        return "No VM sampling gap over %g s in %d samples.\n" % (
            threshold,
            len(found),
        )
    out = ["| from | to | seconds |", "| --- | --- | --- |"]
    for start, end, gap, _ in windows:
        out.append(
            "| %s | %s | %d |" % (start.isoformat(), end.isoformat(), gap)
        )
    if len(windows) > MAX_WINDOWS:
        out.append("")
        out.append(
            "%d window(s) in total; the first %d are detailed below."
            % (len(windows), MAX_WINDOWS)
        )
    out.append("")
    out.append("Figures at the last sample before each window:")
    for start, _, gap, block in windows[:MAX_WINDOWS]:
        out.append("")
        out.append("`%s`, %d s:" % (start.isoformat(), gap))
        out.append("```")
        out.extend(_clip(block))
        out.append("```")
    out.append("")
    return "\n".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log", help="job log of Native Tests (arm64)")
    parser.add_argument(
        "--threshold",
        type=float,
        default=10.0,
        help="report windows longer than this many seconds (default 10)",
    )
    parser.add_argument(
        "--summary",
        help="also append a Markdown report to this file (GITHUB_STEP_SUMMARY)",
    )
    args = parser.parse_args(argv)

    with open(args.log, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()

    found, windows = stalls(lines, args.threshold)
    print(report(found, windows, args.threshold))
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as handle:
            handle.write(summary(found, windows, args.threshold))

    if not found:
        return 2
    return 1 if windows else 0


if __name__ == "__main__":
    sys.exit(main())
