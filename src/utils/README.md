# Module Utils

Ce module contient les utilitaires, fonctions d'aide, gestion des tables et calculatrices annexes de la bibliothèque.

## Fichiers Sources (`src/utils/`)

### Tables Alignées et Compressées
- `aligned_tables.c` - Tables d'évaluation alignées en mémoire
- `aligned_tables_dynamic.c` - Allocation et gestion dynamique des tables alignées
- `compressed_tables.c` - Tables d'évaluation compressées

### Génération de Tables et Preflop
- `generate_preflop_table.c` - Génération des tables d'équité préflop
- `mktable.c` - Programme/outil principal de création des tables de lookup
- `mktable_utils.c` - Utilitaires et fonctions d'aide pour la génération de tables (`mktable`)
- `mktab_astud.c` - Génération de tables pour Asian Stud
- `mktab_basic.c` - Génération des tables de base pour les jeux standards
- `mktab_evx.c` - Génération de tables pour les algorithmes EVX
- `mktab_joker.c` - Génération des tables supportant le Joker
- `mktab_lowball.c` - Génération des tables pour Lowball / Razz
- `mktab_packed.c` - Génération de tables sous format paqueté (packed)
- `mktab_short.c` - Génération des tables pour Short Deck

### Utilitaires, Calculatrices et Wrappers
- `combinations.c` - Calculs de combinaisons et d'indexation
- `icm_calculator.c` - Calculateur ICM (Independent Chip Model) pour tournois
- `poker_eval_modern.c` - Interface moderne d'évaluation et helpers
- `poker_wrapper.c` - Wrappers C haut niveau pour la bibliothèque
- `universal_deck.c` - Implémentation utilitaire du deck universel

### Tables statiques et générateurs d'appoint (`src/utils/tables/`)
- `tables/t_botfivecardsj.c`
- `tables/t_evx_flushcards.c`
- `tables/t_evx_pairval.c`
- `tables/t_evx_strval.c`
- `tables/t_evx_tripsval.c`
- `tables/t_jokerstraight.c`
- `tables/t_maskrank.c`
- `tables/t_nbitsandstr.c`
- `tables/t_topbit.c`
- `tables/t_topfivebits.c`
- `tables/t_toptwobits.c`