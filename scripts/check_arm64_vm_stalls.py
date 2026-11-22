#!/usr/bin/env python3
"""Report windows in which the arm64 native-test runner stopped sampling.

`Native Tests (arm64)` prints `== HH:MM:SS`, then the load average, `vm_stat`
and the busiest processes, every two seconds on a nightly or a manual
dispatch (see .github/workflows/macos-arm64.yml). A window without a sample
is a window in which that runner made no progress: run 44828726427 lost 66 s
and 74 s that way, and ctest charged the whole window to every case it caught
in it (docs/ci.md).

Consecutive samples alone cannot see a sampler that dies before the suite
ends: the log then holds a few samples and no gap at all. The first and last
sample are therefore also checked against the bounds of the suite, the first
`Start N:` line and `Total Test time`, which is the last timestamped line of
the log when the job was killed before printing it.

Usage:
    check_arm64_vm_stalls.py LOG [--threshold SECONDS] [--summary FILE]

Exit status: 0 nothing to report, 1 a window, a coverage gap or a suite
end the log never printed, 2 no sample.
"""

import argparse
import datetime as dt
import re
import sys

# A log line from the jobs API: an ISO timestamp, then the output, e.g.
#   2026-11-22T08:19:31.5746010Z == 08:19:31
LINE = re.compile(
    r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?)Z(?: (.*))?$"
)
# The job log carries the colour codes ctest writes; they sit inside the
# message and, when a step printed them at the start of a line, would hide the
# timestamp this script anchors on.
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
SAMPLE = re.compile(r"^\s*== \d{2}:\d{2}:\d{2}\s*$")
SUITE_START = re.compile(r"^\s+Start\s+(\d+):\s*(.*?)\s*$")
SUITE_END = re.compile(r"^\s*Total Test time \(real\)")

# A window can span a minute of unrelated ctest output; the figures that
# matter are the ones the sampler printed last, so both reports stop after
# this many lines and say so.
MAX_BLOCK = 40
MAX_WINDOWS = 12
MAX_CASES = 20


def _timestamp(text):
    # The log keeps microseconds, the sample line keeps seconds; both are UTC.
    return dt.datetime.fromisoformat(text.split(".")[0])


def parse(lines):
    """Return the samples, the suite bounds and the cases ctest announced."""
    samples, starts, start, end, last = [], [], None, None, None
    for index, line in enumerate(lines):
        found = LINE.match(line)
        if not found:
            continue
        stamp, message = _timestamp(found.group(1)), found.group(2) or ""
        last = stamp
        if SAMPLE.match(message):
            samples.append((stamp, index))
        announced = SUITE_START.match(message)
        if announced:
            starts.append((stamp, announced.group(1), announced.group(2)))
            if start is None:
                start = stamp
        if SUITE_END.match(message):
            end = stamp
    # A log the job never finished has no `Total Test time`: the end stays
    # unset so the caller reports a stopped job rather than a clean suite.
    return samples, start or last, end, last, starts


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


def stalls(lines, marks, threshold):
    """Return the windows longer than `threshold` seconds between samples."""
    found = blocks(lines, marks)
    windows = []
    for before, after in zip(found, found[1:]):
        gap = (after[0] - before[0]).total_seconds()
        if gap > threshold:
            windows.append((before[0], after[0], gap, before[1]))
    return found, windows


def coverage(found, start, end, threshold):
    """Return the stretches the samples do not cover at either suite bound."""
    issues = []
    if (found[0][0] - start).total_seconds() > threshold:
        issues.append(
            ("start", start, found[0][0], (found[0][0] - start).total_seconds())
        )
    if (end - found[-1][0]).total_seconds() > threshold:
        issues.append(
            ("end", found[-1][0], end, (end - found[-1][0]).total_seconds())
        )
    return issues


def started_in(starts, from_at, to_at):
    """Return the cases ctest launched inside a window, in launch order."""
    return [item for item in starts if from_at <= item[0] <= to_at]


def stopped(found, end, last):
    """Return the silence left by a log without `Total Test time`.

    A job stopped at its time limit, or cancelled, never prints that line, and
    its log then ends on its last sample: neither bound looks uncovered, which
    is why this is reported on its own.
    """
    if end is not None or last is None:
        return None
    return (found[-1][0], last, (last - found[-1][0]).total_seconds())


def _hms(stamp):
    return stamp.strftime("%H:%M:%S")


def _coverage_line(found, start, end):
    head = "Coverage: first sample %.1f s after the suite started (%s)" % (
        (found[0][0] - start).total_seconds(),
        start.isoformat(),
    )
    if end is None:
        return head + "; the suite end is not recorded."
    return head + ", last one %.1f s before it ended (%s)." % (
        (end - found[-1][0]).total_seconds(),
        end.isoformat(),
    )


def _clip(block):
    if len(block) <= MAX_BLOCK:
        return list(block)
    return list(block[:MAX_BLOCK]) + [
        "... %d more line(s)" % (len(block) - MAX_BLOCK)
    ]


def report(found, windows, gaps, stop, cases, start, end, threshold):
    if not found:
        return "No VM sample in this log: the sampler did not run."
    out = [
        "%d sample(s) every two seconds; reporting windows over %g s."
        % (len(found), threshold),
        _coverage_line(found, start, end),
    ]
    for start_at, end_at, gap, block in windows[:MAX_WINDOWS]:
        out.append("")
        out.append(
            "stall %s -> %s (%d s)"
            % (start_at.isoformat(), end_at.isoformat(), gap)
        )
        out.extend("  " + line for line in _clip(block))
        inside = started_in(cases, start_at, end_at)
        if inside:
            out.append("  cases started in this window:")
            for stamp, number, name in inside[:MAX_CASES]:
                out.append(
                    "    %s  Start %s: %s" % (_hms(stamp), number, name)
                )
            if len(inside) > MAX_CASES:
                out.append(
                    "    ... %d more case(s)" % (len(inside) - MAX_CASES)
                )
        else:
            out.append("  no ctest case started in this window.")
    if not windows:
        out.append("No window over %g s." % threshold)
    elif len(windows) > MAX_WINDOWS:
        out.append("")
        out.append(
            "%d window(s) in total; the first %d are the ones above."
            % (len(windows), MAX_WINDOWS)
        )
    for bound, from_at, to_at, gap in gaps:
        out.append("")
        out.append(
            "coverage %s: no sample from %s to %s (%d s)"
            % (bound, from_at.isoformat(), to_at.isoformat(), gap)
        )
    if gaps:
        out.append("")
        out.append(
            "Figures at the last sample before the coverage gap:"
            if gaps[0][0] == "end"
            else "Figures at the first sample after the coverage gap:"
        )
        block = found[0][1] if gaps[0][0] == "start" else found[-1][1]
        out.extend("  " + line for line in _clip(block))
    if stop:
        out.append("")
        out.append(
            "suite end: no `Total Test time` line; the log stops at %s,"
            " %d s after the last sample, which is what a job stopped at its"
            " time limit leaves."
            % (stop[1].isoformat(), stop[2])
        )
    return "\n".join(out)


def summary(found, windows, gaps, stop, cases, start, end, threshold):
    if not found:
        return "No VM sample in this log.\n"
    out = [_coverage_line(found, start, end), ""]
    for bound, from_at, to_at, gap in gaps:
        out.append(
            "- **coverage %s**: no sample from %s to %s (%d s)"
            % (bound, from_at.isoformat(), to_at.isoformat(), gap)
        )
    if stop:
        out.append(
            "- **suite end**: no `Total Test time`; the log stops at %s,"
            " %d s after the last sample."
            % (stop[1].isoformat(), stop[2])
        )
    if gaps or stop:
        out.append("")
    if not windows:
        out.append(
            "No VM sampling gap over %g s in %d samples.\n"
            % (threshold, len(found))
        )
        return "\n".join(out)
    out.extend(["| from | to | seconds |", "| --- | --- | --- |"])
    for start_at, end_at, gap, _ in windows:
        out.append(
            "| %s | %s | %d |" % (start_at.isoformat(), end_at.isoformat(), gap)
        )
    if len(windows) > MAX_WINDOWS:
        out.append("")
        out.append(
            "%d window(s) in total; the first %d are detailed below."
            % (len(windows), MAX_WINDOWS)
        )
    out.append("")
    out.append("Figures at the last sample before each window:")
    for start_at, _, gap, block in windows[:MAX_WINDOWS]:
        out.append("")
        out.append("`%s`, %d s:" % (start_at.isoformat(), gap))
        out.append("```")
        out.extend(_clip(block))
        out.append("```")
        inside = started_in(cases, start_at, end_at)
        if inside:
            out.append("")
            out.append("Cases started in the window:")
            for stamp, number, name in inside[:MAX_CASES]:
                out.append("- `%s` Start %s: %s" % (_hms(stamp), number, name))
            if len(inside) > MAX_CASES:
                out.append(
                    "- ... %d more case(s)" % (len(inside) - MAX_CASES)
                )
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
        lines = [ANSI.sub("", line) for line in handle.read().splitlines()]

    marks, start, end, last, cases = parse(lines)
    found, windows = stalls(lines, marks, args.threshold)
    gaps = coverage(found, start, end, args.threshold) if found and end else []
    stop = stopped(found, end, last) if found else None

    print(report(found, windows, gaps, stop, cases, start, end, args.threshold))
    if args.summary:
        text = summary(
            found, windows, gaps, stop, cases, start, end, args.threshold
        )
        with open(args.summary, "a", encoding="utf-8") as handle:
            handle.write(text)

    if not found:
        return 2
    return 1 if windows or gaps or stop else 0


if __name__ == "__main__":
    sys.exit(main())
