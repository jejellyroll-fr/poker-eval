# Module Core

Ce module contient les fonctionnalités de base de l'évaluation de poker :

## Composants

### Évaluation de base
- `eval_core.h/c` - Fonctions d'évaluation principales
- `handval.h/c` - Gestion des valeurs de mains
- `handval_low.h/c` - Évaluation des mains basses

### Gestion des cartes et jeux
- `deck.h/c` - Interface générique des jeux de cartes
- `deck_std.h/c` - Jeu standard (52 cartes)
- `deck_joker.h/c` - Jeu avec joker (53 cartes)
- `deck_short.h/c` - Jeu court (36 cartes)
- `deck_astud.h/c` - Jeu Asian Stud (32 cartes)

### Définitions de base
- `poker_defs.h` - Définitions et types de base
- `poker_config.h` - Configuration du compilateur

### Tables de lookup
- `tables.h/c` - Gestion des tables de lookup
- `compressed_tables.h/c` - Tables compressées
- `aligned_tables.h/c` - Tables alignées pour optimisation

### Cache d'évaluation
- `eval_cache.h/c` - Système de cache pour les évaluations