# Sökö (Soko / Canadian Stud)

Sökö is a five-card stud game: one downcard and four upcards, antes and four
betting rounds. This library implements its **showdown ranking** and its
equity enumeration; hand-history parsing, skin integration and betting rules
belong to the client (fpdb-3).

## Ranking

Descending, per the win2day poker-room rules (§7.f.14 and §7.f.16.3):

| Order | Category |
|-------|----------|
| 1 | Straight flush (royal flush is its highest form) |
| 2 | Four of a kind |
| 3 | Full house |
| 4 | Flush (five cards) |
| 5 | Straight (five cards) |
| 6 | Three of a kind |
| 7 | Two pair |
| 8 | **Four-card flush** |
| 9 | **Four-card straight** |
| 10 | One pair |
| 11 | High card |

The four-card categories therefore sit **above one pair and below two pair**.
That differs from standard poker, where a pair beats a four-card flush, and
from the ordering the `canadian_stud` preset shipped with before this fix
(which also put them above trips).

Tie-breaks:

- **Four-card flush**: compare the four suited ranks lexicographically,
  high to low. If they are equal the hands tie; the fifth, off-suit card is
  not a kicker (§7.f.14.1).
- **Four-card straight**: compare the high card of the qualifying four-card
  sequence. An Ace plays the value it has inside the sequence: A-2-3-4 is
  4-high and ranks below 2-3-4-5, while J-Q-K-A is Ace-high. Equal sequences
  tie whatever the fifth card is (§7.f.14.2).
- A hand with a full five-card straight or flush is classified as such and is
  never demoted to a four-card category. A hand matching several categories
  takes the strongest Sökö one.

## C API

```c
#include <poker_eval/core/configurable_ranking.h>

pe_deck_spec_t deck;
pe_hand_ranking_config_t cfg;

pe_deck_get_predefined(PE_DECK_PRESET_STD, &deck);
pe_ranking_config_set_preset("soko", &cfg); /* aliases: "canadian_stud", "sökö" */

HandVal hv = pe_eval_configurable_hand(&deck, &cfg, mask, 5);
```

The preset returns a normalized `HandVal`: a larger value is always a stronger
hand, and the `HANDTYPE` field is the category's position in the configured
order. Sökö category codes are therefore **not** the standard
`StdRules_HandType_*` values; do not mix them.

The game itself is selectable:

```c
enum_gameparams_t *gp = enumGameParams(game_soko); /* 0..5 pocket cards, no board, high only */
enumExhaustive_dispatch(game_soko, pockets, board, dead, npockets, 0, 0, &result);
```

`game_soko` is appended to `enum_game_t`, so existing game ids are unchanged.
`pokenum -soko ...` exposes the same enumeration from the command line.

## Python API

```python
from pokereval import PokerEval

pe = PokerEval()

# Two pair beats a four-card flush in Sökö.
pe.winners(game="soko",
           pockets=[["Ah", "Ad", "Ks", "Kd", "2h"],
                    ["9h", "Th", "Jh", "3h", "2s"]])   # {'hi': [0]}

# Equal four-flush ranks with different fifth cards tie.
pe.winners(game="soko",
           pockets=[["Ah", "Jh", "8h", "5h", "2s"],
                    ["Ad", "Jd", "8d", "5d", "3c"]])   # {'hi': [0, 1]}

# Incomplete pockets are completed by the shared enumerator; pass
# iterations for Monte Carlo and seed for reproducibility.
pe.poker_eval(game="soko",
              pockets=[["9h", "Ts", "Jd", "Qc", "__"],
                       ["Ah", "Ad", "Ks", "8c", "3h"]],
              board=[],
              iterations=500,
              seed=7)
```

## Range equity

`calculate_range_equity` and the C `pe_equity_*` functions rank Sökö by the
Sökö rules. Ranges accept a plain five-card hand or the stud grammar:

```python
pe.calculate_range_equity(
    "soko",
    [["AhAdKsKd2h"], ["9hThJh3h2s"]],   # two pair vs four-card flush
)   # two pair wins
```

```c
pe_range_t *r1 = NULL, *r2 = NULL;
pe_range_parse(game_soko, "(9hTh)Jh3h2s", dead, NULL, &r1);
pe_range_parse(game_soko, "(AhAd)Ks8c3c", dead, NULL, &r2);
/* ... pe_equity_range_vs_range(NULL, game_soko, r1, r2, board, dead, NULL, &res) */
```

A range string that cannot be expanded for Sökö raises an error naming the
game and the range instead of silently producing an empty (or standard-rule)
calculation. Complete Sökö hands are enumerated exactly; there is no phantom
board to sample.

## Describing a hand

The standard evaluators return a standard five-card `HandVal`, which cannot
encode a four-card flush or a four-card straight. `PokerEval.best()` therefore
reports those hands as `NoPair`, even though Sökö equity correctly ranks them
above a pair. `describe_soko_hand()` answers in Sökö terms instead:

```python
pe.describe_soko_hand(["9h", "Th", "Jh", "3h", "2s"])
# {'category': 'FourCardFlush', 'category_rank': 3,
#  'ranks': [11, 10, 9, 3], 'rank_names': ['J', 'T', '9', '3']}
```

`category_rank` is monotonic: a larger value always beats a smaller one. The
`ranks` list holds only the ranks that decide ties — four suited ranks for a
four-card flush, the high card for a four-card straight — expressed as
2..14. `value` is the Sökö `HandVal` the equity engine compares: larger
always wins, equal values tie across every category, so two descriptions can
be compared directly. It is the Sökö counterpart of the value `best()`
returns. The C entry point is `pe_soko_describe_hand()` in
`<poker_eval/games/soko.h>`.

## Street-aware equity

`pe_equity_soko_street()` (C) and `PokerEval.soko_street_equity()` (Python)
model a Sökö table at third, fourth or fifth street:

```python
pe.soko_street_equity(
    [
        {"up": ["Kh", "Qh"], "down": ["Ah"]},  # our hand, third street
        {"up": ["Kd", "Qd"]},                  # opponent, hole card unknown
    ],
    dead=["3c"],
    iterations=20000,
    seed=7,
)
```

The street is derived from the number of upcards: two is third street, three
fourth, four fifth. `seed` reseeds the sampling stream, so the same players,
iterations and seed reproduce the same equities; it has no effect on the
exact fourth- and fifth-street paths.

The result also carries the sampling statistics. `iterations` is the Monte
Carlo iterations per matchup (0 for an exact result), `stderr` the standard
error of each equity and `ci95` its 95% confidence half-width, so a sampled
equity lies in `hi[i] ± ci95[i]`. The standard error comes analytically from
the sampled pot-share histogram — a player paid `1/h` whenever `h` players
shared the pot — so it costs nothing extra; Python derives `ci95 = 1.96 ×
stderr`. Both are zero when the result is exact. Because that variance is
analytic, the budget needed for a target half-width is one division: passing
`target_ci` to `soko_street_equity()` reports `required_iterations`,
`ceil(iterations × (ci95 / target_ci)²)` per player, which is exact given the
`1/sqrt(iterations)` shrinkage of the half-width; it is 0 when the interval is
already empty. `samples` stays the number of
evaluated matchups, which is one for a single street matchup. A general range
combines several matchups and reports an interval too, but as a batch-means
estimate: each matchup is a batch whose mean is known to `s²/n`, and the
weighted mean of the batches adds those contributions with weights
`(w_m/W)²`, so the pooled histogram is never mistaken for the estimator's
variance. Hi/lo games still report none, because a low pot is a payoff
correlated with the high one that the share histograms cannot separate.
Upcards are visible, so they are removed from the deck for
every player; a missing downcard is enumerated together with the cards still
to come. Fifth street with both hole cards known is a single exact matchup.
This is a showdown-equity model — what the pot is worth given the information
now — not a betting model.

The C API is `pe_equity_soko_street()` in
`<poker_eval/games/soko_street.h>`. It builds one pocket per player from the
cards they are known to hold and lets the shared enumerator deal every other
card, so nothing is pre-expanded.

## Game names

`pe_game_from_name()` (`<poker_eval/core/game_names.h>`) is the single table
of game spellings used by `pokenum` and the Python binding. `soko`, `sökö`,
`SÖKÖ`, `canadian_stud` and `canadianstud` all resolve to `game_soko`, and
the historical `pokenum` flags (`-soko`, `-7s`, `-5d`, ...) keep working.

## Known limitations

- `PokerEval.best()` / `best_hand()` / `best_hand_value()` and
  `PokerEval.evaln()` always use the standard five-card evaluator, since they
  take no game name. They are **not** Sökö-aware; use
  `describe_soko_hand()` for a Sökö category and its comparable `value`, or
  `winners()` / `poker_eval(game="soko", ...)` for Sökö comparisons.
- `pe_compute_hand_distribution(game_soko)` uses the 52-card deck with the
  `soko` preset and five-card hands, so its category probabilities are the
  Sökö ones; the generic `enum` ordering histogram reports Sökö category ranks.
- Range equity expects five-card Sökö pockets. Shorter patterns are filled by
  the stud expander, but a range that expands to zero hands is an error.
- The enumerator deals five-card hands. A Sökö deal is one downcard plus four
  upcards, but the evaluator does not care which of the cards were dealt up.
  Street information is expressed as ranges (`soko_street.h`), not stored in
  the game enum.
- `best()` keeps its standard return contract; forcing Sökö categories into it
  would require a new value encoding, which is why `describe_soko_hand()` is a
  separate function.

## Validation status

The category order and the tie-breaks are pinned by an exhaustive oracle
(`tests/test_soko_oracle.c` counts all 2,598,960 five-card hands) and by the
direct comparisons in `tests/test_soko_game.c`. A cross-check against real
win2day showdowns is still pending: no native, anonymized Sökö hand history
is available yet (tracked in fpdb-3 #891, whose older samples predate
win2day), so the rules are verified against the published text, not against
played hands.

The standard error of a sampled street equity is pinned twice in
`tests/test_soko_street.c`: heads-up against the exact enumeration, and in a
three-player fourth-street spot where 40% of the deals are a three-way tie,
against the spread of the equities over 64 seeded runs. The three-player case
is the one heads-up cannot cover: a heads-up sample is paid 1 or 1/2, so only
a three-way share exercises the `1/h` term for `h = 3` in the variance.

## References

- win2day poker-room rules (§7.f.1–12, §7.f.14, §7.f.16.3):
  <https://www.win2day.at/fairplay/spielbedingungen/poker-room-spielbedingungen>
- PlayNow Sökö rules: <https://www.playnow.com/poker/learn/soko.html>
- Original configurable-ranking design: issue #160
- Downstream consumer: fpdb-3 #891
