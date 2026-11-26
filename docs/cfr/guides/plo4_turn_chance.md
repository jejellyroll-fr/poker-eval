# PLO4 Turn: Exact Chance Fixtures

Issue #285, phase A. API: `include/poker_eval/solver/pe_omaha_turn.h`.
Implementation: `src/solver/domain/omaha_turn.c`. Oracle test:
`tests/test_plo4_turn_oracle.c`.

The river certificate of issue #283 prices one five-card board. The next step
in the scaling plan is the turn, and the turn adds exactly one thing: a chance
node. This tranche implements that node as an exact terminal and pins it
against an independent brute force. It does **not** add a turn betting tree or
a turn certificate; see "What is not covered" at the end.

## What it computes

```c
int pe_omaha_turn_range_values(
    const EvalContext *context,
    mask_t board,               /* four cards: the turn */
    mask_t dead,                /* known dead cards, or MASK_EMPTY */
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,   /* pot, invested[], active[], winner < 0 */
    uint8_t hole_cards,         /* 4, 5 or 6 */
    double *out_values,         /* chips per deal, one per player */
    uint8_t player_count,
    size_t *out_river_count,    /* rivers priced, i.e. with at least one deal */
    double *out_weight_sum);    /* total chance-expanded deal mass */
```

A deal is a tuple of private hands and a river card. The hands carry the range
weights, so a legal pair contributes `w0 * w1`, and the river is uniform over
the cards that neither the board nor those two hands hold: 40 for a full PLO4
turn, 36 for PLO6, fewer by one per declared dead card. Because every legal
pair keeps the same number of candidate rivers, the pooled value

```
sum over rivers of (deal mass on that river) * (its conditional value)
--------------------------------------------------------------------
           sum over rivers of (deal mass on that river)
```

is the chance-expanded mean over deals, with no sampling anywhere. The
per-river conditional value is the river terminal's own convention: net of the
player's investment, divided by the reach mass of that river's legal deals.
The implementation adds no second payoff model — it builds the five-card board,
removes the combos that river card blocks, and calls
`pe_omaha_river_range_values()`.

## Board-dependent legality

A combo that holds the river card is not a private hand on that river, so it
leaves the range before the river terminal runs. It has to leave rather than
keep a zero weight: the river terminal rejects a combo that touches its board,
and a zero weight would leave the card in a leg the deal walk cannot use. The
pairs the two hands block are still the river terminal's business. A river that
would empty a range has no legal deal and is skipped, not priced.

The consequence is the identity the fixture checks: the candidate river list is
the deck minus the board and the dead mask (48 for a full PLO4 turn) and does
not shrink with the ranges, while `*out_weight_sum` is the rivers each *pair*
can see times the pooled range mass — 40 × the pooled mass when nothing is
dead, 38 × when two cards are dead.

## Cost

The terminal costs one river terminal per candidate river: 48 for a full PLO4
turn with no dead cards. Measured on the development machine (Release build,
Apple silicon, identical 194,580-combo ranges on both sides of board
`A-K-7-2`, no dead cards):

| Combos per range | Wall clock | Peak RSS |
|---|---|---|
| 6,000 | 0.28 s | 12.6 MiB |
| 25,000 | 1.16 s | 31.5 MiB |
| 194,580 (full PLO4 turn) | 9.42 s | 77.9 MiB |

Both ranges identical makes the game symmetric, so the values are 0 by
construction; the figures above are cost, not equity. These are machine-specific
and are not a threshold any test asserts.

## What the test pins

`tests/test_plo4_turn_oracle.c` computes every expectation itself: one brute
force enumerates the legal combo tuples, the rivers each tuple can still see,
and the showdown of every `(hands, river)` deal through the product Omaha
evaluator. The terminal appears nowhere in the oracle.

- a full-turn fixture on a turn board with a hand collision and a combo that
  holds one of the watched rivers: both player values and the pooled deal mass
  match the oracle;
- the chance normalisation: 48 candidate rivers and 40 rivers of deal mass per
  legal pair, 46 and 38 with two dead cards, with the same values only where
  the oracle says they are the same;
- the chance node itself: the turn value is neither of the two watched
  rivers' values, so it is not one arbitrary river priced as the turn;
- three-way turn (the river terminal's deal-enumeration path) against a
  three-way oracle, values summing to zero;
- PLO5 and PLO6 turn deals at the same rules;
- rejected inputs: a five-card board, a dead card a combo holds, a folded-out
  state, an inactive player, seven-card holes, and outputs left zeroed.

The fixture was checked by mutation: dropping the deal mass from the
accumulation (`values[p] += river_values[p]` instead of
`+= river_values[p] * river_mass`) makes the value and dead-card checks fail.

## What is not covered

- **No turn betting tree and no turn certificate.** A claim over two streets
  would have to enumerate the river decisions after each chance outcome, not
  only the showdown. The solver's exploitability guarantee therefore stays the
  PLO4 river one of issue #283, and nothing here is labelled certified.
- **Hi/Lo** is not covered in this tranche; the terminal is high-only, like the
  heads-up river vector path it builds on.
- **PLO5/PLO6 scalability and the GPU decision gate** (phases B and C) are
  untouched: this tranche measures the turn terminal's own cost and nothing
  wider.

## Running it

```bash
cmake --build build --target test_plo4_turn_oracle --parallel
./build/tests/test_plo4_turn_oracle
# or
ctest --test-dir build -R test_plo4_turn_oracle
```
