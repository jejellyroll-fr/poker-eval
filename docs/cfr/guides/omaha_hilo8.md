# Omaha Hi/Lo 8-or-Better

Issue #237. Test: `tests/test_mpf_hilo8.c`. Smoke: `mpf_hilo8_sampled_flop_resolves`.

PLO4, PLO5 and PLO6 can each be played Hi/Lo 8-or-better in the multiway
postflop solver. The solver lifecycle, the tree format, private and board
sampling, the range parser and the convergence machinery are the same ones
the high-only games use. Only the payoff at showdown changes.

## Selecting it

Hi/Lo is a payoff model, not another hand size. It sits next to the rule, not
inside it:

```c
mpf_config_t cfg = {0};
cfg.rules    = MPF_RULE_PLO5;       /* deck, 5 private cards, pot limit */
cfg.showdown = MPF_SHOWDOWN_HILO8;  /* how a showdown pot is awarded   */
```

`MPF_SHOWDOWN_HIGH` is zero, so a zero-initialised config keeps the historical
high-only game. Every path driven by `cfg.rules` (hole-card count, pot-limit
detection, combination indexing for complete ranges, card bunching) is
untouched. The same PLO rule serves both payoffs, and there is no
`MPF_RULE_PLO5_HILO8` to keep in step with it.

From the command line, the three rule names take a `-hilo8` suffix:

```sh
mpf_run_with_metrics --rules plo5-hilo8 --tree flop.json --lane-b --iterations 200
```

`plo4-hilo8`, `plo5-hilo8` and `plo6-hilo8` parse ranges exactly like `plo4`,
`plo5` and `plo6`.

## The rules the showdown applies

For every live player the adapter computes a high value and, when one exists,
a low value:

- **Two plus three, for both halves.** Each half is the best five-card hand
  made from exactly two hole cards and exactly three board cards. Four hearts
  on the board and one in the hand is not a flush. Four low cards in the hand
  and two on the board is not a low.
- **The low is A-5 and 8-or-better.** Aces are low. Straights and flushes do
  not spoil a low, so the wheel is both the best low and a five-high straight.
  The five ranks must differ and be A or 2 to 8. 9-8-7-6-2 does not qualify;
  8-7-6-3-2 does.
- **Counterfeits count.** A2 in the hand with an ace and a deuce on the board
  pairs the board for every two-card choice, so it makes no low at all.

## How the pot is split

The side pots are the ones the high-only game builds. Each slice of the
investments is contested by the live players who reached it. Folded players'
chips stay in, but those players cannot win. A slice nobody live reached goes
back to the players who put it in. Hi/Lo then acts on each contested slice:

1. Rake comes off the slice first, with the same per-slice `pe_apply_rake` the
   high-only game uses.
2. If any contesting player has a qualifying low, the raked slice is split into
   two halves. The high half is divided among the tied best highs, the low half
   among the tied best lows.
3. If none of them has a low, the whole slice goes to the high.

The cases this covers:

| Situation | Result |
|---|---|
| No qualifying low | 100% to the high |
| One high winner, a different low winner | 50% / 50% |
| Same player best high and best low | scoop, 100% |
| High tied between k players | each takes 1/k of the high half (of the whole slice when there is no low) |
| Low tied between k players | each takes 1/k of the low half |
| A ties the low, B ties the low and wins the high (heads-up) | A 25%, B 75% |

The low is decided per side pot, among that pot's contestants. An all-in
player's better low wins the main pot's low half but cannot reach a side pot.
The side pot's low half goes to the best low among the players who can win
it, or to the high when none of them has one.

Terminal utilities are zero-sum before rake, and they sum to minus the rake
taken when rake applies.

## What is refused

`mpf_build_game` fails, rather than quietly solving a different game, when:

- Hi/Lo is requested for a non-Omaha rule (Hold'em, Short Deck);
- a strength-bucket abstraction is enabled (`strength_buckets_per_street > 0`
  or a supplied `abstraction_model`), because those buckets rank hands by high
  strength alone and would merge a nut low with an unplayable one;
- `showdown` holds an unknown value.

`mpf_run_with_metrics` also refuses a `.mkr` strategy import into a Hi/Lo game,
since those archives hold PLO4 high-only strategies.

## Omaha high hands before this change

Hi/Lo depends on the high being right, and the high was not. The MPF adapter
scored each Omaha two-plus-three hand with `pe_eval_7c`, which accepts only
seven cards and returns `EVAL_INVALID` (zero) for five. Every PLO4/5/6 high
therefore scored zero, and every high-only Omaha showdown in the multiway
postflop solver was a chop. The adapter now scores those hands with
`pe_eval_5c`. High-only PLO solves change as a result: they now reflect who
actually wins.

## How it is tested

`tests/test_mpf_hilo8.c` goes through `mpf_build_game` and the production
utility callback:

- **Worked hands with shares computed by hand.** High only, the 8-low boundary,
  a 9-low that does not play, the wheel, a counterfeit, scoop, chop, quarter,
  tied high, tied low, multiway, both kinds of side pot, a folded low, rake
  (plain and capped) and the two-plus-three rule on each half. The hands that
  depend on hand size are repeated with PLO5 and PLO6 holdings.
- **Random deals for PLO4, PLO5 and PLO6.** 3,000 each, with two to four
  players, uneven stacks, folds and rake, checked against an oracle written in
  the test. The oracle shares no code with the adapter: it ranks the high with
  the legacy StdDeck evaluator, the low from card ranks, and builds the side
  pots its own way.
- **A solve on a heads-up river for each hand size.** The player holding only
  the nut low is worth -5 at equilibrium when the pot is high-only and 0 when
  it is split, and the solve must land on both values.

The CTest `mpf_hilo8_sampled_flop_resolves` solves a PLO5 Hi/Lo flop root with
the sampled Lane B traversal. The flop carries three low ranks, so the low half
is live.
