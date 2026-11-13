#!/usr/bin/env python3
"""Issue #283: the certified example in the PLO4 river guide must stay the real
`pe-plo4-certify` output.

Three checks keep the guide honest:

* the shell command documented for the certified example must use exactly the
  flags this test runs, so the example cannot describe a run nobody executes;
* the guide's full ```json block must match that run: every key, string,
  boolean and integer exactly, floating-point fields within a relative
  tolerance because the showdown kernel sums in a platform-dependent order;
* the short not_certified excerpt must match a run of the same command with
  the documented `--max-iterations 1` override, and the guide must state that
  override.
"""

import json
import re
import subprocess
import sys
from collections import namedtuple

BASE_COMMAND = [
    "--combos", "32",
    "--strategy", "mixed",
    "--weighted",
    "--max-iterations", "32",
]
RELATIVE_TOLERANCE = 1e-9
ABSOLUTE_TOLERANCE = 1e-12

Fence = namedtuple("Fence", "language content start end")


def fail(message):
    print(message, file=sys.stderr)
    return 1


def fenced_blocks(text):
    pattern = re.compile(r"```(\w*)\n(.*?)```", re.S)
    return [
        Fence(match.group(1), match.group(2), match.start(), match.end())
        for match in pattern.finditer(text)
    ]


def option_pairs(command):
    """Split a documented shell command into (flag, value) pairs.

    Order is irrelevant for the comparison; a flag without a value such as
    --weighted is recorded as (flag, None).
    """
    tokens = command.replace("\\\n", " ").split()
    pairs = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token.startswith("--"):
            following = tokens[index + 1] if index + 1 < len(tokens) else None
            if following is not None and not following.startswith("--"):
                pairs.append((token, following))
                index += 2
                continue
            pairs.append((token, None))
        index += 1
    return pairs


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


def run_tool(tool, command):
    completed = subprocess.run(
        [tool, *command], capture_output=True, text=True, check=False
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"pe-plo4-certify failed with exit status {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    return json.loads(completed.stdout)


def replace_option(command, flag, value):
    updated = list(command)
    updated[updated.index(flag) + 1] = value
    return updated


def main(argv):
    if len(argv) != 3:
        return fail(f"usage: {argv[0]} <pe-plo4-certify> <guide.md>")

    tool, guide_path = argv[1], argv[2]
    with open(guide_path, encoding="utf-8") as guide_file:
        guide = guide_file.read()
    blocks = fenced_blocks(guide)

    certificate_index = next(
        (index for index, fence in enumerate(blocks)
         if fence.language == "json" and '"schema"' in fence.content),
        None,
    )
    if certificate_index is None:
        return fail(f"{guide_path} has no full certificate example")
    certificate = blocks[certificate_index]

    command_fence = next(
        (fence for fence in reversed(blocks[:certificate_index])
         if fence.language == "bash" and "pe-plo4-certify" in fence.content),
        None,
    )
    if command_fence is None:
        return fail(f"{guide_path} does not document the command for the example")

    documented = sorted(option_pairs(command_fence.content))
    executed = sorted(option_pairs(" ".join(BASE_COMMAND)))
    if documented != executed:
        return fail(
            "the documented command and the test disagree:\n"
            f"  documented: {documented}\n"
            f"  test:       {executed}"
        )

    try:
        actual = run_tool(tool, BASE_COMMAND)
    except (RuntimeError, json.JSONDecodeError) as error:
        return fail(str(error))

    try:
        documented_certificate = json.loads(certificate.content)
    except json.JSONDecodeError as error:
        return fail(f"{guide_path}: the example is not valid JSON: {error}")
    result = compare(documented_certificate, actual)
    if result:
        return result

    excerpt = next(
        (fence for index, fence in enumerate(blocks)
         if index != certificate_index and fence.language == "json"
         and '"verdict"' in fence.content),
        None,
    )
    if excerpt is None:
        print("no not_certified excerpt documented; skipping it")
        print("documented example matches pe-plo4-certify")
        return 0

    region = guide[certificate.end:excerpt.start]
    if not re.search(r"--max-iterations\s+1\b", region):
        return fail(
            "the not_certified example does not document the "
            "--max-iterations 1 override it is measured with"
        )

    labels = excerpt.content.strip()
    labels = re.sub(r",\s*$", "", labels)
    try:
        documented_labels = json.loads("{" + labels + "}")
    except json.JSONDecodeError as error:
        return fail(f"{guide_path}: the not_certified excerpt is invalid: {error}")

    try:
        not_certified = run_tool(
            tool, replace_option(BASE_COMMAND, "--max-iterations", "1")
        )
    except (RuntimeError, json.JSONDecodeError) as error:
        return fail(str(error))
    for key, value in documented_labels.items():
        if key not in not_certified:
            return fail(f"not_certified excerpt documents unknown key {key!r}")
        result = compare(value, not_certified[key], f"not_certified.{key}")
        if result:
            return result

    print("documented example matches pe-plo4-certify")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
