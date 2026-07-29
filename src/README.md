# Structure Modulaire de Poker-Eval

Cette nouvelle structure organise le code en modules logiques pour améliorer la maintenabilité et la réutilisabilité.

## Architecture

```
src/
├── core/          # Fonctionnalités de base
├── games/         # Logique spécifique par jeu
├── distributions/ # Gestion des distributions de mains
├── equity/        # Calculs d'équité et énumération
└── utils/         # Utilitaires et fonctions d'aide
```

## Modules

### Core (`src/core/`)
Le module core contient les fonctionnalités fondamentales :
- Définitions de base (`poker_defs.h`)
- Gestion des jeux de cartes (`deck_*.h/c`)
- Évaluation de base (`handval.h`, `inlines/eval.h`)
- Tables de lookup et cache

### Games (`src/games/`)
Le module games contient la logique spécifique à chaque variante :
- Règles de jeu (`rules_*.h/c`)
- Définitions de jeux (`game_*.h`)
- Évaluations spécialisées (`inlines/eval_*.h`)
- Algorithmes lowball

### Distributions (`src/distributions/`)
Le module distributions gère les ranges et distributions de mains :
- Distributions Omaha, Stud, Hold'em
- Parsing et génération de ranges
- Nomenclature PLO
- Conversion de cartes

### Equity (`src/equity/`)
Le module equity contient tous les calculs d'équité :
- Énumération exhaustive et Monte Carlo
- Calculs d'équité entre ranges
- Optimisations SIMD et multi-threading
- Accélération GPU

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
