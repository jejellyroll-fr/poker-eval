# Heads-up PLO5 / PLO6 street trees

Eight trees for testing a sim: two variants x four streets, heads-up, with
the four simple actions — **check, call, raise pot, fold**.

    plo5_hu_preflop.tree.json   plo6_hu_preflop.tree.json
    plo5_hu_flop.tree.json      plo6_hu_flop.tree.json
    plo5_hu_turn.tree.json      plo6_hu_turn.tree.json
    plo5_hu_river.tree.json     plo6_hu_river.tree.json

## Why four trees per variant, not one

Lane B follows the tree on the run's **root street only**. When a tree
terminal is reached, the later streets are dealt and rolled out to showdown
rather than read from the tree. A single tree spanning preflop through river
would have everything past the root street silently ignored — the solver
prints a scope warning when it sees such a file. So each street is its own
tree and its own run.

The PLO5 and PLO6 files are byte-identical: a tree carries the street,
seats and actions, never the hole-card count. That comes from `--game`.
Both sets exist so each has a matching name and command; one set would do.

## The spot

100bb heads-up, blinds 0.5 / 1. One consistent line: SB raises pot preflop,
BB calls, then a pot bet is called on every street. Board **Ks 9d 4c 2h Ts**.

| street  | board        | pot at root | stacks |
|---------|--------------|-------------|--------|
| preflop | —            | 1.5         | 99.5 / 99 |
| flop    | `Ks9d4c`     | 7           | 96.5 |
| turn    | `Ks9d4c2h`   | 21          | 89.5 |
| river   | `Ks9d4c2hTs` | 63          | 68.5 |

Postflop the BB (seat 1) acts first, as in real heads-up play. Raises are
pot-sized (`"pot_sizing": true`, size `1.0`), which is the pot-limit maximum.

## Running from the command line

    ./run.sh                # both variants, all four streets
    ./run.sh plo6 turn      # one variant, one street
    ITERATIONS=20000 ./run.sh plo5 river

## Running from Studio

1. **GAME** → `PLO5` or `PLO6`; **PLAYERS** → 2.
2. **.TREE** → type or paste the path. The Browse dialog filters on `.tree`
   files and will not list these `.json` ones; typing the path works.
3. **Load and inspect tree**, then for a postflop tree fill in:
   - **BOARD** — the cards from the table above,
   - **POT AT ROOT (BB)** — the pot from the table above.
   Both are required postflop: there are no blinds to post, so the money
   already in the middle is an input. Preflop leaves both empty.
4. Leave the range fields empty (they default to `100%`) or enter a
   ProPokerTools rank pattern: `AAxxx` / `AKQxx` / `AAKKx` for PLO5,
   `AKQJxx` / `AAKKxx` for PLO6. Use the same kind of range for both
   players — mixing a pattern with `100%` is not supported.
5. **Solve this spot**.
