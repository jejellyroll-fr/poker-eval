#!/usr/bin/env python3
"""Issue #283, full-combo tranche: certify a whole PLO4 river range.

A five-card board leaves 47 live cards, so a complete four-card range holds
C(47, 4) = 178,365 combos.  The first tranche stopped at 4,096 generated
combos; this test drives the CLI at the full dimension twice and checks the
documents it publishes:

* the whole range is declared and live on both sides;
* the measurement is exact (not sampled, no fallback) and the reported
  showdown path is the blocker-aware sweep;
* both best responses converged, so the certificate is labelled certified,
  and the arithmetic it claims holds: NashConv is the sum of the two
  unilateral gaps, exploitability is half of it, and the policy values of
  this zero-sum game cancel;
* with `--max-iterations 1` the same full range must publish `not_certified`
  with reason `not_converged`, never an exact label, so the "non-convergence
  cannot produce exact" criterion is exercised at the full dimension.
"""

import json
import subprocess
import sys

FULL_RANGE = 178365
SCHEMA = "pe-plo4-certification/v1"


def fail(message):
    print(message, file=sys.stderr)
    return 1


def run_tool(tool, extra_args):
    completed = subprocess.run(
        [
            tool,
            "--combos", str(FULL_RANGE),
            "--strategy", "mixed",
            "--weighted",
            *extra_args,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"pe-plo4-certify {' '.join(extra_args)} exited with "
            f"{completed.returncode}: {completed.stderr.strip()}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"the certificate is not valid JSON: {error}") from error


def check_full_range(document, label):
    combos = document.get("combos", {})
    if combos.get("declared") != [FULL_RANGE, FULL_RANGE]:
        return fail(
            f"{label}: declared ranges are {combos.get('declared')!r}, "
            "expected the full range"
        )
    if combos.get("width") != FULL_RANGE:
        return fail(f"{label}: measured width is {combos.get('width')!r}")
    if combos.get("live") != [FULL_RANGE, FULL_RANGE]:
        return fail(
            f"{label}: live ranges are {combos.get('live')!r}, "
            "expected every combo to weigh"
        )
    if document.get("schema") != SCHEMA:
        return fail(f"{label}: schema is {document.get('schema')!r}")
    if document.get("sampled") is not False:
        return fail(f"{label}: sampled is {document.get('sampled')!r}")
    if document.get("showdown_path") != "plo4":
        return fail(f"{label}: showdown_path is {document.get('showdown_path')!r}")
    return 0


def check_certified(document):
    expected = {
        "mode": "exact",
        "guarantee": "nash",
        "verdict": "certified",
        "reason": "none",
        "converged": True,
    }
    for field, wanted in expected.items():
        if document.get(field) != wanted:
            return fail(f"{field} is {document.get(field)!r}, expected {wanted!r}")

    nashconv = document["nashconv"]["chips"]
    gap = document["br_gap"]["chips"]
    exploitability = document["exploitability"]["chips"]
    policy = document["policy_value"]["chips"]

    if abs(nashconv - (gap[0] + gap[1])) > 1e-9 * (1.0 + abs(nashconv)):
        return fail(f"nashconv {nashconv!r} is not the sum of the gaps {gap!r}")
    if abs(exploitability - 0.5 * nashconv) > 1e-12 * (1.0 + abs(exploitability)):
        return fail(
            f"exploitability {exploitability!r} is not half of nashconv"
        )
    if abs(policy[0] + policy[1]) > 1e-6 * (1.0 + abs(policy[0])):
        return fail(f"policy values {policy!r} do not cancel in this zero-sum game")

    print(
        "plo4 certification covers the full river range: "
        f"{FULL_RANGE} combos, exploitability {exploitability:.6f} chips "
        f"({document['exploitability']['percent_of_reference_pot']:.3f}% of pot)"
    )
    return 0


def check_not_converged(document):
    expected = {
        "mode": "not_certified",
        "guarantee": "none",
        "verdict": "not_certified",
        "reason": "not_converged",
        "converged": False,
    }
    for field, wanted in expected.items():
        if document.get(field) != wanted:
            return fail(
                f"--max-iterations 1: {field} is {document.get(field)!r}, "
                f"expected {wanted!r}"
            )
    if document["br_iterations"] != [1, 1]:
        return fail(
            f"--max-iterations 1: br_iterations is "
            f"{document['br_iterations']!r}, expected one iteration per player"
        )
    # The financial fields are still emitted; they simply are not certified.
    exploitability = document.get("exploitability", {}).get("chips")
    if not isinstance(exploitability, (int, float)):
        return fail("--max-iterations 1: exploitability.chips is missing")
    print(
        "plo4 certification at the full river range reports not_certified/"
        "not_converged with --max-iterations 1"
    )
    return 0


def main(argv):
    if len(argv) != 2:
        return fail(f"usage: {argv[0]} <pe-plo4-certify>")
    tool = argv[1]

    try:
        certified = run_tool(tool, [])
        not_converged = run_tool(tool, ["--max-iterations", "1"])
    except RuntimeError as error:
        return fail(str(error))

    for document, label in ((certified, "default budget"),
                            (not_converged, "--max-iterations 1")):
        result = check_full_range(document, label)
        if result:
            return result

    result = check_certified(certified)
    if result:
        return result
    return check_not_converged(not_converged)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
