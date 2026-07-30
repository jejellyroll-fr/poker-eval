# Games Module

This module contains variant-specific logic for each poker game variant.

> **Note**: Corresponding header files are located in `include/poker_eval/games/`.

## Source Files (`src/games/`)

- `badugi_eval.c` - Specialized Badugi evaluation algorithms
- `deck_manila.c` - Manila card deck management
- `lowball.c` - Lowball rules and evaluation interface (A-5, 2-7, Razz)
- `lowball_algorithm.c` - Lowball ranking algorithms and rank selection logic
- `mixed_game.c` - Mixed game rotation and rules management (H.O.R.S.E., 8-Game, etc.)
- `rules_astud.c` - Asian Stud game rules
- `rules_badugi.c` - Base Badugi game rules
- `rules_drawmaha.c` - Drawmaha game rules
- `rules_fusion.c` - Irish / Fusion Poker game rules
- `rules_joker.c` - Game rules for variants with Jokers
- `rules_manila.c` - Manila Poker game rules
- `rules_omaha5.c` - 5-Card Omaha game rules
- `rules_omaha6.c` - 6-Card Omaha game rules
- `rules_pineapple.c` - Pineapple Hold'em game rules
- `rules_short.c` - Short Deck (6+ Hold'em) game rules
- `rules_std.c` - Standard Hold'em / Stud / Omaha base game rules
- `triple_draw.c` - Triple Draw rules and evaluation (2-7, A-5)