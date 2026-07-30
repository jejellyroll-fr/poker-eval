# Structure Modulaire de Poker-Eval

Cette nouvelle structure organise le code en modules logiques pour améliorer la maintenabilité et la réutilisabilité.

## Architecture

```
src/
├── benchmarks/    # Benchmarks de performance
├── core/          # Fonctionnalités de base
├── distributions/ # Gestion des distributions de mains
├── economics/     # Calculs économiques, pot odds et EV
├── engine/        # Moteur de jeu et machines d'état
├── equity/        # Calculs d'équité et énumération
├── examples/      # Exemples d'utilisation de l'API
├── games/         # Logique spécifique par jeu
├── gpu/           # Accélération GPU (CUDA / OpenCL)
├── ofc/           # Open-Face Chinese Poker (OFC)
├── range/         # Parsing et opérations sur les ranges
└── utils/         # Utilitaires et fonctions d'aide
```

## Modules

### Benchmarks (`src/benchmarks/`)
Le module benchmarks contient les tests de performance :
- Micro-benchmarks 7 cartes SIMD
- Validation d'évaluateurs et énumérateurs
- Benchmarks de solveurs CFR (Hold'em, Omaha, Stud, Razz, Short Deck)
- Benchmarks d'équité preflop/flop et GPU

### Core (`src/core/`)
Le module core contient les fonctionnalités fondamentales :
- Définitions de base (`poker_defs.h`)
- Gestion des jeux de cartes (`deck_*.h/c`)
- Évaluation de base (`handval.h`, `inlines/eval.h`, `eval_context.h`)
- Tables de lookup et cache

### Distributions (`src/distributions/`)
Le module distributions gère les ranges et distributions de mains :
- Distributions Omaha, Stud, Hold'em
- Parsing et génération de ranges
- Nomenclature PLO
- Conversion de cartes

### Economics (`src/economics/`)
Le module economics gère l'analyse économique et de tournoi :
- Modèle ICM (Independent Chip Model)
- Calculs de rake et de structure de pots
- Analyses d'EV et cotes de pot

### Engine (`src/engine/`)
Le module engine contient les composants du moteur de jeu :
- Gestion de l'état de jeu et des tours d'enchères
- Moteur de décision et solvers CFR
- Simulation de parties

### Equity (`src/equity/`)
Le module equity contient tous les calculs d'équité :
- Énumération exhaustive et Monte Carlo
- Calculs d'équité entre ranges
- Optimisations SIMD et multi-threading
- Accélération d'équité multiway

### Examples (`src/examples/`)
Le module examples fournit des programmes de démonstration :
- Exemples d'utilisation de l'API moderne `pe_*`
- Démos d'évaluation de mains et calcul d'équité
- Exemples d'intégration GPU et multithread

### Games (`src/games/`)
Le module games contient la logique spécifique à chaque variante :
- Règles de jeu (`rules_*.h/c`)
- Définitions de jeux (`game_*.h`)
- Évaluations spécialisées (`inlines/eval_*.h`)
- Algorithmes lowball (A-5, 2-7) et Hi/Lo

### GPU (`src/gpu/`)
Le module gpu gère l'accélération matérielle :
- Noyaux CUDA et OpenCL pour l'évaluation de mains
- Évaluation par lots (batched evaluation)
- Solver GPU CFR et benchmarks associés

### OFC (`src/ofc/`)
Le module ofc est dédié au Open-Face Chinese Poker :
- Évaluation rapide de mains OFC
- Calculs de royautés et règles Fantasyland
- Optimisations SIMD pour OFC

### Range (`src/range/`)
Le module range gère le traitement algébrique des ranges :
- Parsing de syntaxe Hold'em, Omaha, Stud
- Opérations de combinaison (union, intersection, différence)
- Pondération et filtrage par cartes mortes

### Utils (`src/utils/`)
Le module utils contient les utilitaires :
- Fonctions de combinaisons/permutations
- Micro-optimisations
- Génération de tables
- Wrappers et interfaces

## Compilation

Pour compiler avec la nouvelle structure modulaire :

```bash
mkdir build
cd build
cmake ..
make
```

## Utilisation

### Utilisation modulaire
```c
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/core/enumerate.h>
```

### Utilisation traditionnelle (compatibilité)
```c
#include "poker_defs.h"  // Redirige vers la nouvelle structure
```

## Avantages

1. **Modularité** : Chaque module peut être utilisé indépendamment
2. **Maintenabilité** : Code organisé logiquement
3. **Réutilisabilité** : Modules réutilisables dans d'autres projets
4. **Évolutivité** : Plus facile d'ajouter de nouvelles fonctionnalités
5. **Lisibilité** : Structure claire et intuitive

## Migration

La nouvelle structure maintient la compatibilité avec l'API existante grâce à :
- Alias de bibliothèques (`poker_eval` → `poker_eval_modular`)
- Redirections d'en-têtes
- Préservation des interfaces publiques

## Tests

Tous les tests existants continuent de fonctionner avec la nouvelle structure.
Les nouveaux tests peuvent cibler des modules spécifiques.
