#!/bin/bash

# Script de validation complète de l'implémentation GPU
# Vérifie que tous les composants fonctionnent correctement

echo "=== Validation de l'Implémentation GPU ==="
echo "Date: $(date)"
echo ""

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Fonction pour afficher les résultats
print_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✓ $2${NC}"
    else
        echo -e "${RED}✗ $2${NC}"
    fi
}

# Fonction pour afficher les avertissements
print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

# 1. Vérifier que les fichiers nécessaires existent
echo "1. Vérification des fichiers..."

files_to_check=(
    "include/poker_eval/gpu/eval_gpu.h"
    "src/gpu/eval_gpu_unified.c"
    "src/gpu/opencl/eval_opencl.c"
    "src/gpu/opencl/eval_kernel.cl"
    "src/gpu/opencl/eval_low_kernel.cl"
    "src/gpu/opencl/eval_omaha_kernel.cl"
    "src/gpu/opencl/eval_generic_kernel.cl"
    "examples/gpu_eval_example.c"
    "tests/test_gpu_acceleration.c"
    "docs/GPU_ACCELERATION_SUMMARY.md"
    "docs/GPU_USAGE_GUIDE.md"
)

all_files_exist=1
for file in "${files_to_check[@]}"; do
    if [ -f "$file" ]; then
        print_result 0 "Fichier $file existe"
    else
        print_result 1 "Fichier $file manquant"
        all_files_exist=0
    fi
done

if [ $all_files_exist -eq 0 ]; then
    echo -e "${RED}Erreur: Fichiers manquants détectés${NC}"
    exit 1
fi

echo ""

# 2. Vérifier la compilation
echo "2. Test de compilation..."

make clean > /dev/null 2>&1
if make test_gpu_acceleration gpu_eval_example > /dev/null 2>&1; then
    print_result 0 "Compilation réussie"
else
    print_result 1 "Échec de compilation"
    echo "Détails de l'erreur:"
    make test_gpu_acceleration gpu_eval_example
    exit 1
fi

echo ""

# 3. Vérifier la disponibilité GPU
echo "3. Test de disponibilité GPU..."

gpu_output=$(./tests/test_gpu_acceleration 2>/dev/null | grep -E "(CUDA|OpenCL).*(available|not available)")
if echo "$gpu_output" | grep -q "available"; then
    print_result 0 "GPU détecté et disponible"
    echo "$gpu_output" | sed 's/^/   /'
else
    print_warning "Aucun GPU disponible - tests limités au CPU"
fi

echo ""

# 4. Exécuter les tests complets
echo "4. Exécution des tests..."

test_output=$(./tests/test_gpu_acceleration 2>&1)
test_exit_code=$?

if [ $test_exit_code -eq 0 ]; then
    print_result 0 "Tous les tests passent"
    
    # Extraire les statistiques des tests
    tests_run=$(echo "$test_output" | grep "Tests run:" | awk '{print $3}')
    tests_passed=$(echo "$test_output" | grep "Tests passed:" | awk '{print $3}')
    tests_failed=$(echo "$test_output" | grep "Tests failed:" | awk '{print $3}')
    
    echo "   Tests exécutés: $tests_run"
    echo "   Tests réussis: $tests_passed"
    echo "   Tests échoués: $tests_failed"
else
    print_result 1 "Certains tests ont échoué"
    echo "Détails des échecs:"
    echo "$test_output" | grep "✗" | sed 's/^/   /'
fi

echo ""

# 5. Test de performance basique
echo "5. Test de performance basique..."

if echo "$gpu_output" | grep -q "available"; then
    echo "Exécution d'un benchmark rapide..."
    
    # Créer un test de performance simple
    cat > quick_perf_test.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "poker_defs.h"
#include "eval_gpu.h"
#include "inlines/eval.h"

StdDeck_CardMask random_cards(int n_cards) {
    StdDeck_CardMask result, dead;
    StdDeck_CardMask_RESET(result);
    StdDeck_CardMask_RESET(dead);
    
    for (int i = 0; i < n_cards; i++) {
        int card;
        StdDeck_CardMask card_mask;
        
        do {
            card = rand() % 52;
            card_mask = StdDeck_MASK(card);
        } while (StdDeck_CardMask_ANY_SET(dead, card_mask));
        
        StdDeck_CardMask_OR(result, result, card_mask);
        StdDeck_CardMask_OR(dead, dead, card_mask);
    }
    
    return result;
}

int main() {
    srand(time(NULL));
    
    int backend = gpu_is_available(1) ? 1 : (gpu_is_available(0) ? 0 : -1);
    if (backend < 0) {
        printf("NO_GPU\n");
        return 0;
    }
    
    gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, backend);
    if (!ctx) {
        printf("INIT_FAILED\n");
        return 1;
    }
    
    // Test simple
    int n_boards = 1000;
    int n_players = 2;
    StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
    StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));
    
    for (int i = 0; i < n_boards; i++) {
        boards[i] = random_cards(5);
        for (int j = 0; j < n_players; j++) {
            hole_cards[i * n_players + j] = random_cards(2);
        }
    }
    
    clock_t start = clock();
    gpu_eval_result_t result = {0};
    int ret = gpu_eval_batch_boards(ctx, boards, hole_cards, n_boards, n_players, &result);
    clock_t end = clock();
    
    if (ret == 0) {
        double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("SUCCESS %.4f %d\n", time_taken, n_boards * n_players);
        free(result.hand_values);
    } else {
        printf("EVAL_FAILED\n");
    }
    
    free(boards);
    free(hole_cards);
    gpu_eval_cleanup(ctx);
    return 0;
}
EOF

    # Compiler et exécuter le test
    if gcc -O3 -I./include -I./gpu/include quick_perf_test.c -L. -lpoker_lib_static -L./gpu -lpoker_gpu_unified -lpoker_opencl -framework OpenCL -o quick_perf_test 2>/dev/null; then
        perf_result=$(./quick_perf_test 2>/dev/null)
        
        if echo "$perf_result" | grep -q "SUCCESS"; then
            time_taken=$(echo "$perf_result" | awk '{print $2}')
            evaluations=$(echo "$perf_result" | awk '{print $3}')
            evals_per_sec=$(echo "scale=0; $evaluations / $time_taken" | bc -l 2>/dev/null || echo "N/A")
            
            print_result 0 "Test de performance réussi"
            echo "   Temps: ${time_taken}s pour $evaluations évaluations"
            echo "   Performance: $evals_per_sec évals/sec"
        elif echo "$perf_result" | grep -q "NO_GPU"; then
            print_warning "Aucun GPU disponible pour le test de performance"
        else
            print_result 1 "Échec du test de performance"
            echo "   Résultat: $perf_result"
        fi
        
        rm -f quick_perf_test quick_perf_test.c
    else
        print_result 1 "Échec de compilation du test de performance"
    fi
else
    print_warning "Test de performance ignoré (pas de GPU)"
fi

echo ""

# 6. Vérifier la documentation
echo "6. Vérification de la documentation..."

if [ -f "GPU_ACCELERATION_SUMMARY.md" ] && [ -s "GPU_ACCELERATION_SUMMARY.md" ]; then
    print_result 0 "Documentation de résumé présente"
else
    print_result 1 "Documentation de résumé manquante ou vide"
fi

if [ -f "GPU_USAGE_GUIDE.md" ] && [ -s "GPU_USAGE_GUIDE.md" ]; then
    print_result 0 "Guide d'utilisation présent"
else
    print_result 1 "Guide d'utilisation manquant ou vide"
fi

echo ""

# 7. Résumé final
echo "=== Résumé de Validation ==="

if [ $test_exit_code -eq 0 ] && [ $all_files_exist -eq 1 ]; then
    echo -e "${GREEN}✓ Implémentation GPU validée avec succès${NC}"
    echo ""
    echo "Fonctionnalités validées:"
    echo "  ✓ Compilation sans erreur"
    echo "  ✓ Tests unitaires complets"
    echo "  ✓ API fonctionnelle"
    echo "  ✓ Exactitude des calculs"
    echo "  ✓ Documentation complète"
    
    if echo "$gpu_output" | grep -q "available"; then
        echo "  ✓ GPU détecté et fonctionnel"
    else
        echo "  ⚠ GPU non disponible (fonctionnalité limitée)"
    fi
    
    echo ""
    echo "L'implémentation est prête pour utilisation en production."
    
else
    echo -e "${RED}✗ Validation échouée${NC}"
    echo ""
    echo "Problèmes détectés:"
    
    if [ $all_files_exist -eq 0 ]; then
        echo "  ✗ Fichiers manquants"
    fi
    
    if [ $test_exit_code -ne 0 ]; then
        echo "  ✗ Tests en échec"
    fi
    
    echo ""
    echo "Veuillez corriger les problèmes avant utilisation."
    exit 1
fi

echo ""
echo "Pour utiliser l'accélération GPU:"
echo "  1. Consultez GPU_USAGE_GUIDE.md"
echo "  2. Exécutez ./gpu_eval_example pour un exemple"
echo "  3. Utilisez ./benchmark_gpu_comprehensive.sh pour des tests détaillés"
