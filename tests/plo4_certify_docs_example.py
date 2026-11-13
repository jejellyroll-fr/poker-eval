#!/usr/bin/env python3
"""Issue #283: the certified example in the PLO4 river guide must stay the real
`pe-plo4-certify` output.

The guide's first ```json block is the example emitted by

    pe-plo4-certify --combos 32 --strategy mixed --weighted --max-iterations 32

Every key, string, boolean and integer must match exactly. Floating-point
fields may differ in their last bits, because the showdown kernel and the best
response accumulate sums in a platform-dependent order, so they are compared
with a relative tolerance instead of byte for byte.
"""

import json
import re
import subprocess
import sys

COMMAND = [
    "--combos", "32",
    "--strategy", "mixed",
    "--weighted",
    "--max-iterations", "32",
]
RELATIVE_TOLERANCE = 1e-9
ABSOLUTE_TOLERANCE = 1e-12


def fail(message):
    print(message, file=sys.stderr)
    return 1


def compare(documented, actual, path="<root>"):
    if isinstance(documented, dict) and isinstance(actual, dict):
        missing = sorted(set(documented) - set(actual))
        unexpected = sorted(set(actual) - set(documented))
        if missing:
            return fail(f"{path}: documented keys missing from the tool: {missing}")
        if unexpected:
            return fail(f"{path}: undocumented keys emitted by the tool: {unexpected}")
        for key in documented:
            result = compare(documented[key], actual[key], f"{path}.{key}")
            if result:
                return result
        return 0

    if isinstance(documented, list) and isinstance(actual, list):
        if len(documented) != len(actual):
            return fail(
                f"{path}: documented {len(documented)} entries, tool emitted "
                f"{len(actual)}"
            )
        for index, (left, right) in enumerate(zip(documented, actual)):
            result = compare(left, right, f"{path}[{index}]")
            if result:
                return result
        return 0

    if isinstance(documented, bool) or isinstance(actual, bool):
        return 0 if documented is actual else fail(
            f"{path}: documented {documented!r}, tool emitted {actual!r}"
        )

    if isinstance(documented, int) and isinstance(actual, int):
        return 0 if documented == actual else fail(
            f"{path}: documented {documented}, tool emitted {actual}"
        )

    if isinstance(documented, float) and isinstance(actual, float):
        margin = max(ABSOLUTE_TOLERANCE,
                     RELATIVE_TOLERANCE * max(abs(documented), abs(actual)))
        return 0 if abs(documented - actual) <= margin else fail(
            f"{path}: documented {documented!r}, tool emitted {actual!r} "
            f"(difference {abs(documented - actual)!r} > {margin!r})"
        )

    return 0 if documented == actual else fail(
        f"{path}: documented {documented!r}, tool emitted {actual!r}"
    )


def main(argv):
    if len(argv) != 3:
        return fail(f"usage: {argv[0]} <pe-plo4-certify> <guide.md>")

    tool, guide_path = argv[1], argv[2]
    completed = subprocess.run(
        [tool, *COMMAND], capture_output=True, text=True, check=False
    )
    if completed.returncode != 0:
        return fail(
            f"pe-plo4-certify failed with exit status {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )

    with open(guide_path, encoding="utf-8") as guide_file:
        guide = guide_file.read()
    block = re.search(r"```json\n(.*?)```", guide, re.S)
    if not block:
        return fail(f"{guide_path} has no json example block")

    try:
        documented = json.loads(block.group(1))
    except json.JSONDecodeError as error:
        return fail(f"{guide_path}: the example is not valid JSON: {error}")
    try:
        actual = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        return fail(f"pe-plo4-certify did not print valid JSON: {error}")

    result = compare(documented, actual)
    if result:
        return result
    print("documented example matches pe-plo4-certify")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
