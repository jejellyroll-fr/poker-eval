# Heads-up No-Limit Hold'em trees

The quick ones to test with — Hold'em samples far faster than PLO5/PLO6.

    nlhe_hu_full.tree.json              preflop -> river in one tree, 26 nodes
    nlhe_hu_flop_dry_K72r.tree.json     flop  Ks 7d 2c
    nlhe_hu_flop_wet_JT9ss.tree.json    flop  Jh Th 9s
    nlhe_hu_flop_paired_882.tree.json   flop  8s 8d 2h

Actions are check, call, bet/raise and fold. Preflop opens to 2.5bb; postflop
bets are 66% pot (`"pot_sizing": true`), which is the usual c-bet size and,
unlike PLO, not capped at the pot.

Note the two size conventions, because they are easy to mix up: a plain
`sizes` value is the raise increment **above the call**, not a raise-to. The
preflop profile is `1.5`, which over the 1bb call is an open to 2.5bb — the
solver's own branch label reads `RAISE 1.50`, the increment. A
`"pot_sizing": true` value is a fraction of the pot once the call is made.

## The three flop trees

Same shape, same ranges, same pot — **only the board differs**. So any
difference between their outputs is the board's doing, which makes them the
right way to check that the solve actually responds to texture:

    ./run.sh boards

With a 10-hand range at 20k iterations, AhAs bets 6.9% on the dry K72r,
3.8% on the wet JT9ss and 1.9% on the paired 882; JhJs bets 19.5% on K72r
against 3.3% on 882. An overpair that likes a dry board and dislikes a
coordinated one is the expected shape.

## The full tree

    ./run.sh

Rooted preflop and deals its own boards, so it takes no `--street`,
`--board` or `--pot`. Reports `tree_streets=PRE=4 FLOP=4 TURN=4 RIVER=4`.

## In Studio

GAME -> Hold'em, PLAYERS -> 2, Browse to the tree (the dialog lists `.json`
now), Load and inspect tree, then Solve. A flop tree needs **BOARD** and
**POT AT ROOT (BB)**; the full tree needs neither.

The STREET and STEP filters above the table narrow the view. STREET defaults
to "All streets" — set it to Flop, or pick a flop step, to see that street
alone.
