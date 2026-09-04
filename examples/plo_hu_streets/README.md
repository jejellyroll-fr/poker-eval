# Heads-up PLO5 / PLO6 trees

Trees for testing a sim: heads-up, with the four simple actions —
**check, call, raise pot, fold**.

**One tree, preflop through river** — the Monker Solver shape. The
round-closing action of each street wires straight to the first node of the
next, so a single run solves all four streets:

    plo5_hu_full.tree.json      plo6_hu_full.tree.json

**One tree per street**, for working a single street with a chosen board and
pot:

    plo5_hu_preflop.tree.json   plo6_hu_preflop.tree.json
    plo5_hu_flop.tree.json      plo6_hu_flop.tree.json
    plo5_hu_turn.tree.json      plo6_hu_turn.tree.json
    plo5_hu_river.tree.json     plo6_hu_river.tree.json

The full tree is rooted preflop and deals its own boards, so it takes no
`--street` / `--board` / `--pot`. It has 30 nodes, 20 of them decisions,
five per street.

The PLO5 and PLO6 files are byte-identical: a tree carries the street,
seats and actions, never the hole-card count. That comes from `--game`.
Both sets exist so each has a matching name and command; one set would do.

## The spot

100bb heads-up, blinds 0.5 / 1. Raises are pot-sized
(`"pot_sizing": true`, size `1.0`), the pot-limit maximum. Preflop the SB
acts first; postflop the BB does, as in real heads-up play.

The full tree deals its own boards from the ranges, so the table below
applies to the **per-street** trees only: one consistent line where SB
raises pot preflop, BB calls, then a pot bet is called on every street,
on board **Ks 9d 4c 2h Ts**.

| street  | board        | pot at root | stacks |
|---------|--------------|-------------|--------|
| preflop | —            | 1.5         | 99.5 / 99 |
| flop    | `Ks9d4c`     | 7           | 96.5 |
| turn    | `Ks9d4c2h`   | 21          | 89.5 |
| river   | `Ks9d4c2hTs` | 63          | 68.5 |

## Running from the command line

    ./run.sh                # per-street trees, both variants
    ./run.sh plo6 turn      # one variant, one street
    ./run.sh plo5 full      # the single preflop..river tree
    ITERATIONS=20000 ./run.sh plo5 full

## Running from Studio

1. **GAME** → `PLO5` or `PLO6`; **PLAYERS** → 2.
2. **.TREE** → type or paste the path. The Browse dialog filters on `.tree`
   files and will not list these `.json` ones; typing the path works.
3. **Load and inspect tree**. For the full tree nothing else is needed —
   TREE CONTEXT will show `PRE=5 FLOP=5 TURN=5 RIVER=5`. For a per-street
   postflop tree fill in:
   - **BOARD** — the cards from the table above,
   - **POT AT ROOT (BB)** — the pot from the table above.
   Both are required postflop: there are no blinds to post, so the money
   already in the middle is an input. Preflop leaves both empty.
4. Leave the range fields empty (they default to `100%`) or enter a
   ProPokerTools rank pattern: `AAxxx` / `AKQxx` / `AAKKx` for PLO5,
   `AKQJxx` / `AAKKxx` for PLO6. Use the same kind of range for both
   players — mixing a pattern with `100%` is not supported.
5. **Solve this spot**.
