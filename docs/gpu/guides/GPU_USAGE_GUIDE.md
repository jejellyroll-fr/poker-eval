# Guide d'Utilisation de l'Accélération GPU

## Vue d'Ensemble

L'accélération GPU pour l'évaluation de mains de poker offre des performances améliorées pour les gros volumes de calculs. Ce guide explique comment utiliser efficacement cette fonctionnalité.

## Installation et Build

### Prérequis

#### Pour OpenCL (Recommandé)
- **macOS** : Inclus par défaut
- **Linux** : `sudo apt-get install opencl-headers ocl-icd-opencl-dev`
- **Windows** : SDK OpenCL du fabricant GPU

#### Pour CUDA (GPU NVIDIA uniquement)
- CUDA Toolkit 10.0+
- GPU NVIDIA avec Compute Capability 3.5+

### Compilation

```bash
# Build avec support GPU automatique
mkdir build && cd build
cmake ..
make

# Ou utiliser le Makefile traditionnel
make gpu_eval_example test_gpu_acceleration
```

## API de Base

### Initialisation

```c
#include "eval_gpu.h"

// Vérifier la disponibilité GPU
int cuda_available = gpu_is_available(0);    // CUDA
int opencl_available = gpu_is_available(1);  // OpenCL

// Choisir le backend
int backend = cuda_available ? 0 : (opencl_available ? 1 : -1);
if (backend < 0) {
    printf("Aucun GPU disponible\n");
    return -1;
}

// Initialiser le contexte GPU
gpu_eval_context_t* ctx = gpu_eval_init(
    0,           // device_id (0 = premier GPU)
    100000,      // max_batch_size
    backend      // 0=CUDA, 1=OpenCL
);
```

### Évaluation par Batch

```c
// Préparer les données
int n_boards = 10000;
int n_players = 2;
StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));

// ... remplir boards et hole_cards ...

// Évaluer sur GPU
gpu_eval_result_t result = {0};
int ret = gpu_eval_batch_boards(ctx, boards, hole_cards, 
                               n_boards, n_players, &result);

if (ret == 0) {
    // Utiliser result.hand_values[i] pour chaque évaluation
    for (int i = 0; i < n_boards * n_players; i++) {
        printf("Hand %d: %u\n", i, result.hand_values[i]);
    }
    free(result.hand_values);
}
```

### Simulation Monte Carlo

```c
// Simulation d'équité
int n_players = 2;
int n_simulations = 1000000;
float equities[2];

int ret = gpu_monte_carlo_equity(ctx, NULL, n_players, 
                                n_simulations, equities);

if (ret == 0) {
    printf("Player 1: %.3f (%.1f%%)\n", equities[0], equities[0] * 100);
    printf("Player 2: %.3f (%.1f%%)\n", equities[1], equities[1] * 100);
}
```

### Nettoyage

```c
// Libérer les ressources
gpu_eval_cleanup(ctx);
```

## Optimisation des Performances

### Choix de la Taille de Batch

#### GPU Dédiés (NVIDIA/AMD)
```c
// Optimal pour GPU dédiés
if (n_evaluations >= 10000) {
    // Utiliser GPU - bon speedup attendu
    use_gpu = 1;
    batch_size = min(n_evaluations, 100000);
} else {
    // Utiliser CPU - overhead GPU trop important
    use_gpu = 0;
}
```

#### GPU Intégrés (Intel)
```c
// Plus conservateur pour GPU intégrés
if (n_evaluations >= 50000) {
    // GPU peut être bénéfique
    use_gpu = 1;
    batch_size = min(n_evaluations, 50000);
} else {
    // CPU probablement plus rapide
    use_gpu = 0;
}
```

### Réutilisation du Contexte

```c
// ✅ Bon : réutiliser le contexte
gpu_eval_context_t* ctx = gpu_eval_init(0, 100000, backend);

for (int batch = 0; batch < num_batches; batch++) {
    gpu_eval_batch_boards(ctx, ...);  // Réutilise le contexte
}

gpu_eval_cleanup(ctx);

// ❌ Mauvais : recréer le contexte à chaque fois
for (int batch = 0; batch < num_batches; batch++) {
    gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, backend);
    gpu_eval_batch_boards(ctx, ...);
    gpu_eval_cleanup(ctx);  // Overhead important
}
```

### Gestion Mémoire

```c
// Pré-allouer pour éviter les allocations répétées
StdDeck_CardMask* boards = malloc(MAX_BATCH_SIZE * sizeof(StdDeck_CardMask));
StdDeck_CardMask* hole_cards = malloc(MAX_BATCH_SIZE * MAX_PLAYERS * sizeof(StdDeck_CardMask));

// Réutiliser les buffers pour plusieurs batches
for (int batch = 0; batch < num_batches; batch++) {
    // Remplir boards et hole_cards pour ce batch
    fill_batch_data(boards, hole_cards, batch);
    
    // Évaluer
    gpu_eval_batch_boards(ctx, boards, hole_cards, batch_size, n_players, &result);
    
    // Traiter les résultats
    process_results(&result);
    
    // result.hand_values est réutilisé automatiquement
}

free(boards);
free(hole_cards);
```

## Exemples Pratiques

### Exemple 1 : Évaluation de Range vs Range

```c
#include "eval_gpu.h"
#include "poker_eval/poker_defs.h"

void evaluate_range_vs_range() {
    // Initialiser GPU
    int backend = gpu_is_available(1) ? 1 : 0;  // Préférer OpenCL
    gpu_eval_context_t* ctx = gpu_eval_init(0, 50000, backend);
    
    if (!ctx) {
        printf("GPU non disponible, utiliser CPU\n");
        return;
    }
    
    // Générer toutes les combinaisons de boards possibles
    int n_boards = 10000;
    StdDeck_CardMask* boards = generate_random_boards(n_boards);
    
    // Range 1: AA (6 combinaisons)
    // Range 2: KK (6 combinaisons)  
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    generate_pocket_pairs(RANK_ACE, aa_hands);
    generate_pocket_pairs(RANK_KING, kk_hands);
    
    int total_wins_aa = 0, total_wins_kk = 0, total_ties = 0;
    
    // Évaluer chaque combinaison
    for (int aa = 0; aa < 6; aa++) {
        for (int kk = 0; kk < 6; kk++) {
            StdDeck_CardMask hole_cards[2] = {aa_hands[aa], kk_hands[kk]};
            
            gpu_eval_result_t result = {0};
            int ret = gpu_eval_batch_boards(ctx, boards, hole_cards, 
                                          n_boards, 2, &result);
            
            if (ret == 0) {
                // Compter les victoires
                for (int i = 0; i < n_boards; i++) {
                    HandVal aa_val = result.hand_values[i * 2];
                    HandVal kk_val = result.hand_values[i * 2 + 1];
                    
                    if (aa_val > kk_val) total_wins_aa++;
                    else if (kk_val > aa_val) total_wins_kk++;
                    else total_ties++;
                }
                free(result.hand_values);
            }
        }
    }
    
    int total_hands = 6 * 6 * n_boards;
    printf("AA vs KK sur %d boards:\n", total_hands);
    printf("AA gagne: %.2f%%\n", (float)total_wins_aa / total_hands * 100);
    printf("KK gagne: %.2f%%\n", (float)total_wins_kk / total_hands * 100);
    printf("Égalités: %.2f%%\n", (float)total_ties / total_hands * 100);
    
    gpu_eval_cleanup(ctx);
    free(boards);
}
```

### Exemple 2 : Benchmark Adaptatif

```c
void adaptive_gpu_benchmark() {
    int backend = gpu_is_available(1) ? 1 : 0;
    
    // Tester différentes tailles pour trouver le point d'équilibre
    int test_sizes[] = {100, 1000, 5000, 10000, 50000};
    int n_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    printf("Recherche du point d'équilibre GPU/CPU...\n");
    
    for (int i = 0; i < n_sizes; i++) {
        int batch_size = test_sizes[i];
        
        // Test CPU
        clock_t cpu_start = clock();
        run_cpu_evaluation(batch_size);
        clock_t cpu_end = clock();
        double cpu_time = (double)(cpu_end - cpu_start) / CLOCKS_PER_SEC;
        
        // Test GPU
        clock_t gpu_start = clock();
        run_gpu_evaluation(batch_size, backend);
        clock_t gpu_end = clock();
        double gpu_time = (double)(gpu_end - gpu_start) / CLOCKS_PER_SEC;
        
        double speedup = cpu_time / gpu_time;
        printf("Batch %d: CPU=%.4fs, GPU=%.4fs, Speedup=%.2fx %s\n",
               batch_size, cpu_time, gpu_time, speedup,
               speedup > 1.0 ? "✓" : "✗");
    }
}
```

## Dépannage

### Problèmes Courants

#### 1. GPU Non Détecté
```c
if (!gpu_is_available(0) && !gpu_is_available(1)) {
    printf("Aucun GPU détecté. Vérifiez:\n");
    printf("- Drivers GPU installés\n");
    printf("- OpenCL/CUDA runtime disponible\n");
    printf("- Permissions d'accès au GPU\n");
}
```

#### 2. Erreur de Compilation Kernel
```c
gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, 1);
if (!ctx) {
    printf("Échec d'initialisation GPU.\n");
    printf("Vérifiez les logs de compilation OpenCL.\n");
    // Fallback vers CPU
    use_cpu_evaluation();
}
```

#### 3. Performance Décevante
```c
// Vérifier la taille de batch
if (batch_size < 10000) {
    printf("Batch trop petit pour GPU (overhead important)\n");
    printf("Recommandation: batch_size >= 10000\n");
}

// Vérifier le type de GPU
char device_name[256];
gpu_get_device_info(0, device_name, NULL, NULL);
if (strstr(device_name, "Intel")) {
    printf("GPU intégré détecté: %s\n", device_name);
    printf("Performance limitée, considérer CPU pour petits batches\n");
}
```

### Validation des Résultats

```c
void validate_gpu_accuracy() {
    // Générer données de test
    StdDeck_CardMask boards[100];
    StdDeck_CardMask hole_cards[200];  // 100 boards * 2 players
    generate_test_data(boards, hole_cards, 100, 2);
    
    // Évaluation CPU (référence)
    HandVal cpu_results[200];
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 2; j++) {
            StdDeck_CardMask combined;
            StdDeck_CardMask_OR(combined, boards[i], hole_cards[i*2+j]);
            cpu_results[i*2+j] = StdDeck_StdRules_EVAL_N(combined, 7);
        }
    }
    
    // Évaluation GPU
    gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, 1);
    gpu_eval_result_t gpu_result = {0};
    gpu_eval_batch_boards(ctx, boards, hole_cards, 100, 2, &gpu_result);
    
    // Comparaison
    int mismatches = 0;
    for (int i = 0; i < 200; i++) {
        if (cpu_results[i] != gpu_result.hand_values[i]) {
            mismatches++;
            printf("Mismatch %d: CPU=%u, GPU=%u\n", 
                   i, cpu_results[i], gpu_result.hand_values[i]);
        }
    }
    
    printf("Validation: %d/%d correct (%.1f%%)\n", 
           200-mismatches, 200, (200-mismatches)/200.0*100);
    
    free(gpu_result.hand_values);
    gpu_eval_cleanup(ctx);
}
```

## Recommandations Finales

### Utilisation Optimale

1. **Taille de Batch** : ≥ 10,000 évaluations pour GPU dédiés, ≥ 50,000 pour GPU intégrés
2. **Réutilisation** : Garder le contexte GPU actif pour plusieurs batches
3. **Fallback** : Toujours implémenter un fallback CPU
4. **Validation** : Tester l'exactitude sur votre hardware spécifique

### Cas d'Usage Recommandés

- **Analyse de ranges** : Millions d'évaluations
- **Simulations Monte Carlo** : ≥ 100,000 itérations
- **Preprocessing** : Calcul de tables de lookup
- **Batch processing** : Traitement de logs de parties

### Cas d'Usage Non Recommandés

- **Évaluations individuelles** : Overhead trop important
- **Applications temps réel** : Latence imprévisible
- **Systèmes embarqués** : Consommation énergétique
- **Petits volumes** : CPU plus efficace
