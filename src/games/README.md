# Module Games

Ce module contient la logique spécifique à chaque variante de poker.

> **Remarque** : Les fichiers d'en-tête (headers) correspondants se trouvent dans `include/poker_eval/games/`.

## Fichiers source (`src/games/`)

- `badugi_eval.c` - Évaluation spécialisée Badugi
- `chinese_poker.c` - Règles et évaluation du Chinese Poker (OFC)
- `deck_manila.c` - Gestion du paquet Manila
- `joker_wild.c` - Gestion des variantes avec jokers et wild cards
- `lowball.c` - Évaluation et règles lowball (A-5, 2-7, Razz)
- `mixed_game.c` - Gestion des jeux mixtes (H.O.R.S.E., 8-Game, etc.)
- `omaha.c` - Évaluation et règles Omaha (High / Hi-Lo)
- `pineapple.c` - Évaluation et règles Pineapple Hold'em
- `rules_badugi.c` - Règles de base Badugi
- `rules_drawmaha.c` - Règles Drawmaha
- `rules_fusion.c` - Règles Irish / Fusion Poker
- `rules_manila.c` - Règles Manila Poker
- `rules_omaha5.c` - Règles Omaha 5 cartes
- `rules_omaha6.c` - Règles Omaha 6 cartes
- `rules_pineapple.c` - Règles Pineapple
- `short_deck.c` - Règles et évaluation Short Deck (6+ Hold'em)
- `stud.c` - Évaluation et règles 7-Card Stud / Razz / Stud Hi-Lo
- `triple_draw.c` - Règles et évaluation Triple Draw (2-7, A-5)