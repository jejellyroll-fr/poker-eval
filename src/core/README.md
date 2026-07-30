# Module Core

Ce module contient les fonctionnalités de base de l'évaluation de poker et la manipulation des cartes, des masques et des combinaisons.

## Fichiers Sources (`src/core/`)

### Évaluation et Hand Value
- `CardConverter.c` - Conversion entre représentations de cartes et chaînes
- `canonical_5card.c` - Canonisation et équivalence des mains à 5 cartes
- `card.c` - Représentation de base des cartes et des masques
- `combo_7to5.c` - Énumération des combinaisons 7 cartes vers 5 cartes
- `combo_7to5_hilo.c` - Énumération des combinaisons 7 cartes Hi-Lo (high/low)
- `eval_7c_simd.c` - Évaluation optimisée SIMD à 7 cartes
- `eval_cache.c` - Cache d'évaluation des mains
- `eval_context.c` - Contexte et état d'évaluation
- `evx.c` - Algorithme et évaluation EVX
- `low_eval.c` - Évaluation des mains basses (Lowball, Razz, 8-or-better)
- `low_qualifier.c` - Qualification des mains basses (ex: 8-or-better)
- `modern_cardmask.c` - Masques de cartes modernes et masquage d'indexation

### Gestion des Decks (Jeux de Cartes)
- `deck.c` - Interface générique des jeux de cartes
- `deck_std.c` - Deck standard (52 cartes)
- `deck_joker.c` - Deck avec Joker (53 cartes)
- `deck_short.c` - Short Deck / Six-Plus Hold'em (36 cartes)
- `deck_astud.c` - Deck Asian Stud (32 cartes)
- `universal_deck.c` - Abstraction de deck universel pour différents formats de jeux
- `joker_expansion_controlled.c` - Gestion et expansion contrôlée des cartes Joker

### Combinaisons et Statut
- `modern_combinations.c` - Calculs modernes de combinaisons
- `omaha_combinations.c` - Combinaisons et sous-ensembles spécifiques à l'Omaha
- `status.c` - Codes de statut, diagnostics et erreurs du module core
- `deterministic_benchmark.c` - Harness pour benchmarks déterministes core

### Tables de Lookup et Générateurs
- `t_astudcardmasks.c` - Générateur/table de masques pour Asian Stud
- `t_botcard.c` - Générateur/table pour la carte la plus basse
- `t_botfivecards.c` - Générateur/table pour les 5 cartes les plus basses
- `t_cardmasks.c` - Générateur/table de masques de cartes standards
- `t_jokercardmasks.c` - Générateur/table de masques de cartes avec Joker
- `t_nbits.c` - Table et calculs du nombre de bits activés (popcount)
- `t_shortdeckcardmasks.c` - Générateur/table de masques pour Short Deck
- `t_straight.c` - Générateur/table pour la détection des quintes (straights)
- `t_topcard.c` - Générateur/table pour la carte la plus haute
- `t_topfivecards.c` - Générateur/table pour les 5 cartes les plus hautes