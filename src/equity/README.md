# Module Equity

Ce module contient toutes les implémentations pour les calculs d'équité, l'analyse de board et les moteurs d'énumération de `poker-eval`.

## Fichiers sources (`src/equity/`)

### Équité de range & Multiway
- `MultiwayEquity.c` : Calcul d'équité multi-joueurs et gestion des pots secondaires (*side-pots*).
- `RangeEquity.c` : Calcul d'équité mono-thread entre ranges de mains (`CalculateEquityForRanges`).
- `RangeEquity_MT.c` : Implémentation multithread de base pour l'équité de ranges (`CalculateEquityForRanges_MT`).
- `RangeEquity_MT_v2.c`, `RangeEquity_MT_v3.c`, `RangeEquity_MT_v4.c`, `RangeEquity_MT_v5.c` : Variantes d'optimisation multithread.
- `RangeEquity_MT_Batched.c` : Implémentation multithread optimisée utilisant Monte Carlo par lots (*batched*).

### Équité préflop, flop & Board
- `preflop_equity.c` : Calculs d'équité préflop, gestion des mains canoniques (169), parsing de ranges et lookup tables.
- `preflop_table_blob.c` : Intégration binaire de la table d'équité préflop prédéfinie.
- `omaha_preflop.c` : Calculs et distributions d'équité préflop spécifiques à l'Omaha.
- `flop_equity.c` : Analyse de texture de flop (dry/wet/paired), probabilités d'amélioration et décompte d'outs.
- `board_stats.c` : Statistiques de tableau et propriétés des cartes communes.

### Moteurs d'énumération
- `enumerate.c` : Moteur d'énumération exhaustive général.
- `enumerate_dispatch.c` : Aiguillage et sélection automatique du moteur d'énumération adapté.
- `enumerate_eedc.c` : Énumération optimisée EEDC.
- `enumerate_eedc_omaha_opt.c` : Optimisations EEDC pour l'Omaha.
- `enumerate_doubleflop_fix.c` : Énumération pour les règles/variantes double-flop.
- `enumerate_doubleflop_simd.c` : Énumération double-flop optimisée via instructions vectorielles SIMD.
- `enumord.c` : Ordonnancement et indexation des combinaisons d'énumération.

### Monte Carlo, SIMD & Utilitaires
- `batched_montecarlo.c` : Simulation Monte Carlo vectorisée/par batch.
- `sampling_policies.c` : Politiques d'échantillonnage pour la simulation Monte Carlo.
- `simd_operations.c` : Operations vectorielles SIMD pour les calculs d'équité.
- `canonicalize.c` : Canonisation des mains et réduction d'isomorphismes de cartes.
- `pe_equity.c` : Interface d'équité poker-eval haut niveau (`pe_calculate_equity`).
- `range_combo_buffers.c` : Gestion des tampons mémoire pour les combinaisons de ranges.
- `run_it_twice.c` : Calcul d'équité sous la règle *Run It Twice* (RIT).
- `sidepots.c` : Calcul et répartition des pots principaux et secondaires.