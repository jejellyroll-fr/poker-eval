# Guide d'utilisation du multithreading pour CalculateEquityForRanges

## Vue d'ensemble

Ce guide explique comment utiliser les différentes versions multithreadées de `CalculateEquityForRanges` pour optimiser les calculs d'équité de ranges.

## Versions disponibles

### 1. Version single-thread (baseline)
```c
int CalculateEquityForRanges(...)
```
- Utilisation : Petites ranges (<100 matchups)
- Avantages : Pas d'overhead, simple
- Inconvénients : Lent pour grandes ranges

### 2. Version MT originale
```c
int CalculateEquityForRanges_MT(..., int num_threads)
```
- Utilisation : Ranges moyennes (100-10k matchups)
- Avantages : Bonne performance générale
- Inconvénients : Utilise beaucoup de mémoire pour grandes ranges

### 3. Version MT v2 (optimisée Phase 1)
```c
int CalculateEquityForRanges_MT_v2(..., int num_threads)
```
- Utilisation : Ranges moyennes à grandes
- Avantages : Meilleur scheduling, prefetching
- Optimisations : Chunk size adaptatif, guided scheduling

### 4. Version MT v3 (optimisée Phase 2)
```c
int CalculateEquityForRanges_MT_v3(..., int num_threads)
```
- Utilisation : Très grandes ranges (>10k matchups)
- Avantages : Faible utilisation mémoire, scalable
- Optimisations : Génération paresseuse des matchups

### 5. Version Auto
```c
int CalculateEquityForRanges_Auto(...)
```
- Utilisation : Choix automatique de la meilleure version
- Avantages : Optimal pour tous les cas

## Exemples d'utilisation

### Exemple 1 : Calcul simple
```c
#include "RangeEquity.h"
#include "RangeEquity_MT.h"

// Définir les ranges
StdDeck_CardMask hands1[10], hands2[10];
// ... remplir les mains ...

PlayerRange ranges[2] = {
    {hands1, 10},
    {hands2, 10}
};

// Préparer le board et les cartes mortes
StdDeck_CardMask board, dead;
StdDeck_CardMask_RESET(board);
StdDeck_CardMask_RESET(dead);

// Calculer l'équité
enum_result_t result;
enumResultAlloc(&result, 2, enum_ordering_mode_hi);

int matchups = CalculateEquityForRanges_MT_v3(
    game_holdem,    // Type de jeu
    ranges,         // Ranges des joueurs
    2,              // Nombre de joueurs
    board,          // Board actuel
    dead,           // Cartes mortes
    5,              // Cartes à distribuer
    0,              // Mode exhaustif (pas Monte Carlo)
    0,              // Iterations (non utilisé en exhaustif)
    0,              // Order flag
    &result,        // Résultats
    4               // Nombre de threads
);

// Afficher les résultats
printf("Joueur 1: %.2f%%\n", result.ev[0] / matchups * 100);
printf("Joueur 2: %.2f%%\n", result.ev[1] / matchups * 100);

enumResultFree(&result);
```

### Exemple 2 : Choix automatique
```c
// Utiliser la version Auto pour un choix optimal
int matchups = CalculateEquityForRanges_Auto(
    game_holdem, ranges, 2, board, dead,
    5, 0, 0, 0, &result
);
```

### Exemple 3 : Grande range avec v3
```c
// Pour des ranges très larges (ex: 200+ mains chacune)
// Utiliser v3 pour économiser la mémoire

// Configurer le nombre de threads optimal
int num_threads = omp_get_max_threads();
if (num_threads > 8) num_threads = 8; // Limiter à 8 threads

int matchups = CalculateEquityForRanges_MT_v3(
    game_holdem, large_ranges, 2, board, dead,
    5, 0, 0, 0, &result, num_threads
);
```

## Recommandations de performance

### Choix de la version

| Taille de range | Version recommandée | Threads |
|----------------|-------------------|---------|
| < 100 matchups | Single-thread | 1 |
| 100-1000 | MT ou MT_v2 | 2-4 |
| 1000-10000 | MT_v2 | 4-8 |
| > 10000 | MT_v3 | 4-8 |
| Variable | Auto | Auto |

### Nombre de threads

```c
// Détection automatique
int threads = 0; // 0 = auto-detect

// Basé sur les cores physiques
int threads = omp_get_num_procs() / 2;

// Fixe
int threads = 4; // Bon compromis général
```

### Optimisation mémoire

Pour les très grandes ranges :
- Utilisez MT_v3 (génération paresseuse)
- Limitez le nombre de threads si mémoire limitée
- Considérez le mode Monte Carlo pour >100k matchups

## Compilation

```bash
# Avec OpenMP
gcc -O3 -fopenmp myapp.c -lpoker-eval-mt -lm

# Flags recommandés
CFLAGS = -O3 -march=native -fopenmp
```

## Debugging et profiling

### Activer les traces
```c
#define TRACE_RE(...) fprintf(stderr, __VA_ARGS__)
```

### Mesurer les performances
```c
#include <sys/time.h>

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

double start = get_time();
// ... calcul ...
double elapsed = get_time() - start;
printf("Temps: %.3f secondes\n", elapsed);
```

### Variables d'environnement
```bash
# Contrôler OpenMP
export OMP_NUM_THREADS=4
export OMP_PROC_BIND=true
export OMP_PLACES=cores

# Profiling
export OMP_DISPLAY_ENV=true
```

## Résolution de problèmes

### Performance décevante
1. Vérifiez la taille des ranges (trop petites ?)
2. Testez différents nombres de threads
3. Utilisez la version appropriée
4. Compilez avec -O3

### Résultats incorrects
1. Vérifiez l'initialisation des structures
2. Assurez-vous d'appeler enumResultAlloc
3. N'oubliez pas enumResultFree
4. Divisez ev[] par matchups pour %

### Utilisation mémoire élevée
1. Passez à MT_v3 pour grandes ranges
2. Réduisez le nombre de threads
3. Utilisez Monte Carlo si approprié

## Exemples complets

Voir dans `examples/` :
- `test_mt_optimization.c` : Test simple
- `benchmark_all_versions.c` : Comparaison complète
- `benchmark_large_ranges.c` : Test grandes ranges
- `range_equity_mt_example.c` : Exemples variés