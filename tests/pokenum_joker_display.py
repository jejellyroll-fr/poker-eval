#!/usr/bin/env python3
"""Lock how pokenum prints joker hands.

pokenum turns the cards typed on the command line into card masks, enumerates,
then hands StdDeck masks back to the result printers.  Twice that round trip
printed cards that were never entered: the masks were copied across two
incompatible bit layouts, and joker decks lost the joker on the way, so a
five-card hand showed up as four cards plus a dealt one.

This test drives the real pokenum binary and reads its result table.  For each
case, every printed row must hold exactly the cards that were typed for that
player, with the joker shown as "Xx".  Rows are compared as sets: the printers
may order cards however they like, but they may not drop, add or change one.
The cases cover the three table layouts (hi only, lo only, hi/lo) and put the
joker in different seats and positions.
"""

import re
import subprocess
import sys

# (pokenum game flag, players' typed hands, dead cards, complete hands?)
# "Complete" means every hand already holds its five cards, so there is exactly
# one outcome to enumerate; a lost joker would leave a slot to deal into.
CASES = [
    ("-5d", ["Xx As 2c Ad 3h", "Kh Kd Qs Qc 9h"], "", True),
    ("-5d", ["Ks Kd 7c 7d 2s", "Xx Ah Ac 4d 4c"], "", True),
    ("-5d8", ["Xx As 2c 3d 4h", "Kh Kd Qs Qc 9h"], "", True),
    ("-5dnsq", ["9s 8h Xx 6d 4c", "Ks Kd Qh Qc 2h"], "", True),
    # The example from pokenum's own usage text: an incomplete lowball hand
    # with the joker typed last, and dead cards that must not leak into rows.
    ("-l", ["7h 5s 3d Xx", "9s 8h 6d 4c"], "8c Kd", False),
]

CARD = r"(?:[2-9TJQKA][hdcs]|Xx)"
ROW = re.compile(rf"^((?:{CARD} )*{CARD})\s+\d")
HEADER = re.compile(r": (\d+) enumerated outcomes?\b")


def run_case(pokenum, flag, hands, dead, complete):
    args = [pokenum, flag]
    for i, hand in enumerate(hands):
        if i:
            args.append("-")
        args.extend(hand.split())
    if dead:
        args.append("/")
        args.extend(dead.split())
    label = " ".join(args[1:])

    completed = subprocess.run(args, capture_output=True, text=True, check=False)
    output = completed.stdout
    if completed.returncode != 0:
        return [f"{label}: pokenum exited {completed.returncode}\n{output}"
                f"{completed.stderr}"]

    errors = []
    rows = [m.group(1) for line in output.splitlines()
            if (m := ROW.match(line))]
    if len(rows) != len(hands):
        errors.append(f"{label}: expected {len(hands)} hand rows, "
                      f"found {len(rows)}")
    for seat, (typed, printed) in enumerate(zip(hands, rows), start=1):
        want = typed.split()
        got = printed.split()
        if sorted(got) != sorted(want):
            errors.append(f"{label}: player {seat} typed '{typed}' "
                          f"but pokenum printed '{printed}'")
        if got.count("Xx") != want.count("Xx"):
            errors.append(f"{label}: player {seat} should show 'Xx' "
                          f"{want.count('Xx')} time(s), printed '{printed}'")

    if complete:
        header = HEADER.search(output)
        if header is None or header.group(1) != "1":
            errors.append(f"{label}: complete hands must enumerate exactly "
                          f"one outcome")

    if errors:
        errors.append(f"--- pokenum output ---\n{output}")
    return errors


def main(argv):
    if len(argv) != 2:
        print(f"usage: {argv[0]} <path to pokenum>", file=sys.stderr)
        return 2
    errors = []
    for flag, hands, dead, complete in CASES:
        errors.extend(run_case(argv[1], flag, hands, dead, complete))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"pokenum printed the typed joker hands for all {len(CASES)} cases")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
