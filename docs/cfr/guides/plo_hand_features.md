# PLO Hand Features and Strategy Buckets

Issue #238. API: `include/poker_eval/solver/pe_hand_features.h`.
Implementation: `src/solver/domain/hand_features.c`.
Test: `tests/test_pe_hand_features.c`. Benchmark: `bench_plo_hand_features`.

Once a hand has more than two private cards, a combo-by-combo strategy is
hard to read, and PLO6 is the worst case. This layer maps a concrete PLO4,
PLO5 or PLO6 hand on a board to typed poker features. It turns any
combination of those features into a stable bucket key, and it aggregates a
strategy table over the buckets. A report can then read like this:

```text
made=pair,flush_draw=nut     combos 18,432   weight 7.4%   check 21.3%   bet 48.8%
```

The layer is for analysis only. The solver does not use it, and it is not an
abstraction. It depends only on the card and evaluator layers, so reports,
range filters, diagnostics and a later abstraction engine can all reuse it.

## Two kinds of features

| | Private structure | Evaluated |
|---|---|---|
| What it describes | the hole cards on their own | the hand on this board |
| Needs a board | no | three cards or more |
| Examples | pairs, ranks, connectivity, suit shape | made hand, draws, nuts, blockers |

Evaluated features follow Omaha rules: **exactly two hole cards and exactly
three board cards**. One heart in the hand with three hearts on the board is no
flush. A single connecting rank in the hand with four to a straight on the
board is no straight and no draw. The test checks that five-of-N evaluation
would overrate between 279 (PLO4) and 617 (PLO6) of 1,500 random rivers. It
also checks that the classifier never does.

## Usage

```c
pe_hf_board_t board;
pe_hf_board_prepare(board_mask, &board);         /* once per board */

pe_hand_features_t f;
pe_hand_features_compute(&board, hole_mask, &f); /* or _compute_batch */

uint64_t key = pe_hf_key(&f, PE_HF_DIM_MADE | PE_HF_DIM_FLUSH_DRAW);
char text[128];
pe_hf_key_format(key, text, sizeof text);        /* "made=pair,flush_draw=nut" */
```

Nothing allocates. `pe_hf_board_t` holds every board-derived value (texture,
straight windows, the nut straight and what it needs, each suit's nut card,
the absolute nuts). Each hand is classified against it into a caller-owned
value type.

## Features

### Private structure

| Field | Meaning |
|---|---|
| `paired_ranks`, `trips_ranks` | ranks held exactly twice / three or more times |
| `distinct_ranks`, `highest_rank`, `broadway_count` | rank content (broadway = T..A) |
| `rank_gaps` | missing ranks inside the tightest span of the distinct ranks; the ace counts high or low, whichever is tighter |
| `connectivity` | `rundown` (0 gaps), `high` (1), `medium` (2–3), `low` (4+), `none` (fewer than two ranks) |
| `rank_components`, `longest_run` | runs of consecutive ranks; the longest run counts the ace at both ends |
| `suit_shape[4]`, `suited_groups` | cards per suit, descending (e.g. 2-2-1), and suits holding two or more |

The shape is not limited to four cards: PLO5 and PLO6 shapes such as 3-2-1 or
6 are represented the same way.

### Made hand

`made` is the category of the best two-plus-three hand (high card through
straight flush). `made_value` is its `HandVal`, so hands can be sorted within
a category. `detail` places the hand against the board's distinct ranks:

| Detail | Meaning |
|---|---|
| `overpair` / `underpair` | a pocket pair above every board rank / below the top one |
| `top_pair`, `second_pair`, `low_pair` | one hole card pairing that board rank |
| `board_pair` | the only pair is the board's own |
| `top_two_pair` | hole cards pairing the top two board ranks |
| `top_set`, `middle_set`, `bottom_set` | a pocket pair matching that board rank |
| `trips` / `board_trips` | one hole card with a paired board rank / all three on the board |

`is_nuts` means the made hand equals the **absolute** nuts: the best hand any
two unseen cards make, the hero's own cards included. On KK7, kings full is
not the nuts while quad kings are possible, even for the player holding one of
those kings. `nut_flush` means a made flush that holds its suit's nut card.

### Draws

Draws exist only while cards are to come, and only for hands a straight or a
flush would still improve (a made straight or less).

- **Flush draw.** Two hole cards and two board cards of one suit. It is `nut`
  when the hand holds that suit's nut card (the highest card of the suit not on
  the board). On the flop, two hole cards and one board card of a suit make a
  `backdoor` draw. `double_flush_draw` marks draws in two suits, which PLO5
  and PLO6 make common.
- **Straight draw.** Counted by the ranks that would give the hand a straight,
  or a higher one than it already has: one rank is a `gutshot`, two are
  `open_ended`, three or more a `wrap`. `straight_outs` counts the unseen cards
  of those ranks. JT76 on 9-8-2 is the classic 20-out wrap.
  `straight_draw_to_nuts` marks draws where some out gives the nut straight.
- `combo_draw` marks a flush draw together with an open-ended or wrap draw.
  `redraw` marks a made straight that still draws to a flush or to a higher
  straight.

### Blockers

`blockers` is a set of flags:

- `nut_flush`: the hand holds the nut card of a suit that has, or can still
  make, a flush;
- `nut_straight`: the hand holds a rank the current nut straight needs from the
  hole;
- `board_pair`: the hand holds a card of a paired board rank (boats, quads);
- `top_card`: the hand holds a card of the top board rank.

## Buckets

A bucket is any combination of the dimensions `made`, `detail`, `nuts`,
`flush_draw`, `straight_draw`, `combo_draw`, `redraw`, `blockers`,
`connectivity`, `suits` and `pairs`. `pe_hf_key()` packs the selected values
into a 64-bit key whose top bits record which dimensions it holds. Keys are
therefore:

- **deterministic**: the same hand always gives the same key;
- **sortable**: sorting keys groups identical feature combinations;
- **separated**: keys built over different dimensions never collide, even when
  every value is zero.

Predefined definitions: `PE_HF_BUCKET_MADE`, `PE_HF_BUCKET_DRAWS`,
`PE_HF_BUCKET_MADE_AND_DRAWS` and `PE_HF_BUCKET_STRUCTURE`. `pe_hf_key_matches()`
tests a hand against a key, which is how a range filter uses the layer.
`pe_hf_key_format()` writes `made=two_pair,flush_draw=nut,suits=2-2-1`. The
`pairs` value reads `<pairs>p<trips>t`, so AAKK is `2p0t`.

## Strategy aggregation

`pe_strategy_bucket_aggregate()` takes rows of `{hand, weight, freq[],
ev[] or NULL}` and a bucket definition. Each bucket reports:

- `combos` and `weight`, with `weight_share` of the table's total, so both
  counts and range weight are preserved;
- `freq[a]`, the action frequencies averaged by range weight;
- `ev[a]`, averaged over the rows that carry EVs (`ev_combos`, `ev_weight`);
- `ev_delta[a]`, the average of each action's EV minus the combo's best action
  EV. It is zero for an action that is always best and negative otherwise.

The caller provides the bucket buffer. The result is sorted by key, so it does
not depend on row order. When the buffer is too small, the call returns
`PE_SOLVER_ERR_BUDGET_EXCEEDED`. It refuses a hand that is not four to six cards
or that uses a board card, and any negative or non-finite weight, frequency or
EV.

## Performance

`bench_plo_hand_features` measured these rates (Debug build without
optimisation, Apple silicon, 20,000 hands × 10 boards per case):

| | Board prep (µs) | Hands / s | Rows / s aggregated |
|---|---|---|---|
| PLO4 flop | 53 | 878k | 852k |
| PLO4 river | 220 | 623k | 614k |
| PLO5 flop | 40 | 776k | 763k |
| PLO5 river | 223 | 406k | 403k |
| PLO6 flop | 39 | 666k | 655k |
| PLO6 river | 286 | 224k | 208k |

The made hand dominates the cost: 60 five-card evaluations for PLO4 on the
river, and 150 for PLO6. Draws cost a few rank-mask operations per rank.
Board preparation enumerates every pair of unseen cards once, to find the
nuts.

## How it is tested

- **Worked fixtures, with positive and negative assertions.** They cover dry,
  paired, monotone and connected boards; every pair and set detail; straights,
  nut and non-nut flushes, a boat and quads; the 20-out and 17-out wraps and a
  combo draw; a PLO5 double nut flush draw; the two-plus-three rule for flushes
  and straights; and PLO4/5/6 private structures.
- **Random deals against brute force.** PLO4/5/6 on 400 flops, 400 turns and
  1,500 rivers each, compared with brute force that shares no logic with the
  classifier:
  - the made hand against `pe_eval_5c` over every two-plus-three hand;
  - straight outs, class and nut draws by trying every unseen next card with
    the legacy evaluator;
  - flush draws the same way, and backdoor draws over every pair of runout
    cards;
  - the nuts over every pair of unseen cards.
- **Mutation checks.** Letting one hole card make a straight, or one suited
  hole card make a flush draw, fails the suite.

## Not in this layer

- **No abstraction.** The solver never reads these buckets. A later
  abstraction engine can build on the features without depending on reports or
  UI.
- **No medians.** EV aggregation reports weighted means. A median would need
  per-bucket storage that grows with the table.
- **Backdoor straights are not classified.** Only backdoor flush draws are.
