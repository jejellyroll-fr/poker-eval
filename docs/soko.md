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

## Known limitations

- `PokerEval.best()` / `best_hand()` / `best_hand_value()` and
  `PokerEval.evaln()` always use the standard five-card evaluator, since they
  take no game name. They are **not** Sökö-aware; use `winners()` or
  `poker_eval(game="soko", ...)` for Sökö comparisons.
- `pe_compute_hand_distribution(game_soko)` uses the 52-card deck with the
  `soko` preset and five-card hands, so its category probabilities are the
  Sökö ones; the generic `enum` ordering histogram reports Sökö category ranks.
- Range equity expects five-card Sökö pockets. Shorter patterns are filled by
  the stud expander, but a range that expands to zero hands is an error.
- The enumerator deals five-card hands. A Sökö deal is one downcard plus four
  upcards, but the evaluator does not care which of the cards were dealt up.

## References

- win2day poker-room rules (§7.f.1–12, §7.f.14, §7.f.16.3):
  <https://www.win2day.at/fairplay/spielbedingungen/poker-room-spielbedingungen>
- PlayNow Sökö rules: <https://www.playnow.com/poker/learn/soko.html>
- Original configurable-ranking design: issue #160
- Downstream consumer: fpdb-3 #891
