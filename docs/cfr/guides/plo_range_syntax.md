# PLO5/PLO6 range syntax: rank patterns and suit shapes (issue #236)

Hold'em ranges are two-card words. Omaha ranges are four-card words. PLO5 and
PLO6 are five- and six-card ranges, and the four-card suit vocabulary does not
widen: `ds` is 2-2-0-0, `ss` is 2-1-1-0, `ts` is 3-1-0-0, `qs` is 4-0-0-0 and
`r` is 1-1-1-1. Two suits of two cards is 2-2-1 on a five-card hand only if
that is what was meant, and no six-card hand can be rainbow at all. A suffix
that has two plausible readings is not a shorthand, it is a different range.

So the solver takes the structure itself:

```text
AAxxx[suits=2-2-1]        PLO5: two suits of two, one singleton
AKQJxx[suits=3-1-1-1]     PLO6: one suit of three, three singletons
AAKKxx[suits=2-2-2]       PLO6: three suits of two
AsKsQd3c9h[suits=2-1-1-1] a concrete hand, stating the shape it has
```

## The model

A **suit shape** is the multiset of non-empty suit-group sizes, largest first,
with the empty groups dropped. It is stored as a vector (`pe_suit_shape_t`),
never as a suffix string, and it is exact at every width:

| Hand | Shape |
|------|-------|
| `AsKs` | 2 |
| `AsKhQdJc` | 1-1-1-1 |
| `AsKsJh` | 2-1 |
| `AsKsJh9d3c` | 2-1-1-1 |
| `AsKsQsJh9d3c` | 3-1-1-1 |
| `AsKsQsJsAhKh` | 4-2 |

Group sizes are 1..13, `1-2-2` and `2-2-1` are the same shape, and a shape must
sum to the number of private cards. The empty hand has no shape.

## Grammar

A PLO5/PLO6 range is a comma-separated union of terms; each term is one of:

```text
<rank pattern> [ "[" "suits=" <shape> "]" ] [ ":" <weight> ]
<concrete hand> [ "[" "suits=" <shape> "]" ] [ ":" <weight> ]
100%
```

- `<rank pattern>` is `n` rank slots with `x` for any rank, `n` being the
  private-card count: `AAxxx`, `AKQxx`, `AAKKx` for PLO5, `AKQJxx`, `AAKKxx`
  for PLO6. Multiplicity is "at least", as ProPokerTools defines it: `AAxxx`
  contains `AhAsAcKd2c`.
- `<shape>` is `g1-g2-...-gk`, the group sizes separated by `-`; they may be
  written in any order and are canonicalised to descending (`1-2-2` is
  `2-2-1`).
- `<concrete hand>` is an even-length list of cards, `AsKsQd3c9h`.
- `<weight>` is a non-negative double, `:0.25`; the default is 1.0.
- `100%` is the whole range.

Every term's weights are kept as written; `total_weight` is their sum, so a
union of a weight-2 term and a weight-1 term is not renormalised behind the
caller's back.

## What is refused

Nothing is inferred from a malformed term. Each of these is a parse error, not
a range that happens to be close:

```text
AAxxxds               a four-card word on a five-card token
AAxxx[suits=2-2-1]ds  a word and a shape at once
AAxxx[suits=2-2]      sums to 4, not 5
AAxxx[suits=2-2-2]    sums to 6, not 5
AAxxx[suits=0-2-3]    a zero group is a second spelling of 2-3
AAxxx[suits=1-1-1-1-1] five groups, the deck has four suits
AKQJxx[suits=2-1-1-1-1] the same on six cards
AAxxx[2-2-1]          the key is required: "suits="
AAxxx[suit=2-2-1]     unknown key
AAxxx[suits=]         empty
AAxxx[suits=2-2-1     unterminated
AAxxx[suits=2 2 1]    whitespace inside the brackets
AAxx[suits=2-2]       four-card pattern in a five-card game
AsKsQd3c9h[suits=2-2-1] a concrete hand with a shape it does not have
AAxxx[suits=14-1]     a group larger than the number of ranks
```

PLO4 keeps its existing suffix vocabulary (`ds`, `ss`, `ts`, `qs`, `r`) exactly
as it was; this path is only entered for `game_omaha5` and `game_omaha6`.

## Cold radii, dead cards, weights

The shape is applied while the pattern expands, at the leaf, because a shape is
a property of the whole hand and no prefix of the expansion decides it. Dead
cards are removed before the shape test, so `AAxxx[suits=2-2-1]` with `As` and
`Ks` dead holds 19 080 hands instead of 41 472, none of them touching a dead
card and all of them 2-2-1.

Independent closed-form checks used by `tests/test_plo_suit_shapes.c` (the
five-card counts verified by brute-force enumeration over C(52,5), the six-card
counts by suit-group enumeration):

| Range | Combos |
|-------|--------|
| `AAxxx[suits=2-2-1]` | 41 472 |
| `AKQxx[suits=3-1-1]` | 14 268 |
| `AAKKx[suits=2-2-1]` | 684 |
| `AAxxx[suits=4-1]` | 2 640 |
| `AAxxx[suits=3-2]` | 9 504 |
| `AAxxx[suits=2-1-1-1]` | 32 280 |
| `AKQJxx[suits=2-2-1-1]` | 61 596 |
| `AAKKxx[suits=2-2-2]` | 3 832 |
| `AAKKxx[suits=3-2-1]` | 11 880 |
| `AAKKxx[suits=2-2-1-1]` | 13 290 |
| `AAKKxx[suits=3-3]` | 726 |

As with the unsuffixed patterns, the same hand is reachable through several
required/wildcard splits and the expansion deduplicates.

## Mixing `100%` with an explicit range

A full PLO5/PLO6 range is C(52,5) = 2.6M or C(52,6) = 20.4M hands, which is
neither storable nor walkable per deal. The solver therefore draws a `100%`
player straight from the live deck instead of materialising its combo list, and
the direct solver used to require the whole table to agree on that choice:
mixing `--range0 AAxxx --range1 100%` failed with `invalid plo5 range1: 100%`,
blaming the full range for a limitation of the combination.

A complete player now coexists with explicit ones. The sampler places the
explicit players first and draws the `100%` players from what the live deck
leaves, contributing the same `1 / C(live, n)` proposal and the same exact
importance weight as an all-complete table. So `--range0 AAxxx --range1 100%`
is a valid PLO5 spot, and there is no closed-form normalisation to enumerate for
it (the range has no combo list); the importance weight stays exact regardless.
`tests/test_plo_mixed_ranges.c` pins the equivalence against the enumerated
path and the closed-form ratios.

## Resource guard

One pattern may materialise at most 500 000 hands (`PLO_PATTERN_MAX_COMBOS`;
`AAxxxx` alone is 1.4M). Past that the term is a parse error and
`pe_preflop_solve` prints the limit with a narrower suggestion, rather than
allocating. `pe_range_memory_bytes()` reports what a parsed range's combo
storage cost, and `range->count` is the materialised count, so a caller can
size a range before building it.

## Where to read the code

- `pe_suit_shape_parse()` / `pe_suit_shape_format()` / `pe_suit_shape_equal()`
  / `pe_suit_shape_from_mask()` / `pe_range_memory_bytes()` — `range.h`
- the expander and the `[suits=...]` splitter — `src/range/pe_range.c`
- the cases above — `tests/test_plo_suit_shapes.c`
