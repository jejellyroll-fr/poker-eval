# Module Distributions

Ce module gère la génération et la manipulation des distributions de mains pour les différents variants de poker.

## Fichiers sources (`src/distributions/`)

- `HoldemAgnosticHand.c` - Implémentation de la génération de distributions pour Hold'em
- `holdem_distributions.c` - Fonctions complémentaires de distributions Hold'em
- `omaha_distributions.c` - Génération et instantiation des distributions de mains Omaha (PLO)
- `plo_integration.c` - Intégration avancée et expansion des patterns PLO
- `plo_nomenclature.c` - Parsing et classification des catégories de mains PLO (21 catégories)
- `stud_distributions.c` - Génération des distributions de mains pour les variants Stud (7-Card Stud, Razz)

## En-têtes (`include/poker_eval/distributions/`)

- `HoldemAgnosticHand.h` - Directives et prototypes pour les distributions Hold'em
- `card_converter.h` - Fonctions utilitaires de conversion de cartes
- `hand_distributions.h` - Interface générale pour les distributions de mains
- `holdem_distributions.h` - En-tête pour les distributions Hold'em
- `omaha_distributions.h` - En-tête et structures pour les distributions Omaha (`OmahaHandQuery`, `OmahaHandList`)
- `plo_integration.h` - Prototypes pour l'intégration des patterns PLO
- `plo_nomenclature.h` - Structures et enums pour la nomenclature PLO (`PLOHandCategory`)
- `stud_distributions.h` - En-tête pour les distributions Stud