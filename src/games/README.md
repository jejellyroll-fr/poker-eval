# Module Games

Ce module contient la logique spécifique à chaque variante de poker :

## Composants

### Règles de jeu
- `rules_std.h/c` - Règles standard
- `rules_joker.h/c` - Règles avec joker
- `rules_short.h/c` - Règles short deck
- `rules_astud.h/c` - Règles Asian Stud

### Jeux spécifiques
- `game_std.h/c` - Texas Hold'em, Omaha standard
- `game_joker.h/c` - Variantes avec joker
- `game_short.h/c` - Short Deck Hold'em
- `game_astud.h/c` - Asian Stud variants

### Évaluations spécialisées
- `eval_omaha.h/c` - Évaluation Omaha
- `eval_stud.h/c` - Évaluation Stud
- `eval_lowball.h/c` - Évaluations lowball
- `eval_joker.h/c` - Évaluations avec joker

### Algorithmes spéciaux
- `lowball_algorithm.h/c` - Algorithmes lowball spécialisés