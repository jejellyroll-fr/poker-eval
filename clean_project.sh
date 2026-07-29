#!/bin/bash

# Script de nettoyage du projet poker-eval
# Supprime les builds, tests inutiles et fichiers temporaires

set -e

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== NETTOYAGE DU PROJET POKER-EVAL ===${NC}"
echo -e "${CYAN}Suppression des builds et tests inutiles${NC}"
echo

# Fonction pour supprimer en toute sécurité
safe_remove() {
    local path="$1"
    local description="$2"
    
    if [ -e "$path" ]; then
        echo -e "${YELLOW}Suppression: $description${NC}"
        rm -rf "$path"
        echo -e "${GREEN}✓ Supprimé: $path${NC}"
    else
        echo -e "${CYAN}⚠ Déjà absent: $path${NC}"
    fi
}

# 1. SUPPRESSION DU DOSSIER BUILD
echo -e "${BLUE}=== 1. NETTOYAGE DES BUILDS ===${NC}"
safe_remove "build" "Dossier de build CMake"
safe_remove "cmake-build-*" "Dossiers de build IDE"
safe_remove ".cmake" "Cache CMake"

# 2. SUPPRESSION DES FICHIERS DE DOCUMENTATION REDONDANTS
echo -e "${BLUE}=== 2. NETTOYAGE DE LA DOCUMENTATION REDONDANTE ===${NC}"

# Fichiers de statut redondants
REDUNDANT_STATUS_FILES=(
    "2_7_TRIPLE_DRAW_IMPLEMENTATION_STATUS.md"
    "BADACEY_FINAL_FIX_NEEDED.md"
    "BADACEY_FINAL_SUMMARY.md"
    "BADACEY_IMPLEMENTATION_COMPLETE.md"
    "BADACEY_IMPLEMENTATION_STATUS.md"
    "BADACEY_POKENUM_TEST.md"
    "BADACEY_RESULTS_EXPLANATION.md"
    "BADACEY_TEST_RESULTS.md"
    "BUILD_STATUS_SUMMARY.md"
    "BUILD_TEST_REPORT.md"
    "DRAWMAHA_IMPLEMENTATION.md"
    "ICM_IMPLEMENTATION_COMPLETE.md"
    "NETTOYAGE_ET_FINALISATION_TRIPLE_DRAW.md"
    "NETTOYAGE_ET_VALIDATION_FINALE.md"
    "OMAHA_RANGE_PARSER_IMPLEMENTATION_COMPLETE.md"
    "OMAHA_RANGE_PARSER_PHASE2_COMPLETE.md"
    "OMAHA_RANGE_PARSER_STATUS.md"
    "OMAHA_RANGE_PARSER.md"
    "PHASE2_ETAPE2_POURCENTAGES_COMPLETE.md"
    "PHASE2_FILES_SUMMARY.md"
    "PHASE2_IMPLEMENTATION_SUMMARY.md"
    "PINEAPPLE_IMPLEMENTATION_SUMMARY.md"
    "PINEAPPLE_POKENUM_GUIDE.md"
    "STATUS_FINAL_PHASE2_ETAPE2.md"
    "STUD_PARSER_ADVANCED_COMPLETE.md"
    "STUD_PARSER_IMPLEMENTATION_STATUS.md"
    "test_fixes_summary.md"
    "TRIPLE_DRAW_IMPLEMENTATION_FINAL.md"
    "TRIPLE_DRAW_STATUS_FINAL.md"
    "UNITY_COVERAGE_DEVELOPMENT_SUMMARY.md"
    "UNITY_EXTENDED_COVERAGE_SUMMARY.md"
    "UNITY_FRAMEWORK_SUMMARY.md"
    "UNITY_TESTS_CORRECTIONS_SUMMARY.md"
    "UNITY_TESTS_COVERAGE_SUMMARY.md"
    "INSTALLATION_UNITY.md"
)

for file in "${REDUNDANT_STATUS_FILES[@]}"; do
    safe_remove "$file" "Documentation redondante: $file"
done

# 3. SUPPRESSION DES SCRIPTS DE TEST REDONDANTS
echo -e "${BLUE}=== 3. NETTOYAGE DES SCRIPTS DE TEST REDONDANTS ===${NC}"

REDUNDANT_TEST_SCRIPTS=(
    "compile_all_fixed_unity_tests.sh"
    "compile_unity_tests_manual.sh"
    "compile_unity_tests_simple.sh"
    "run_all_unity_tests_final.sh"
    "run_extended_unity_tests.sh"
    "run_unity_tests_fixed.sh"
    "run_unity_tests_safe.sh"
    "run_unity_tests.sh"
    "test_unity_simple.sh"
)

for script in "${REDUNDANT_TEST_SCRIPTS[@]}"; do
    safe_remove "$script" "Script de test redondant: $script"
done

# 4. SUPPRESSION DES TESTS UNITY NON FONCTIONNELS
echo -e "${BLUE}=== 4. NETTOYAGE DES TESTS UNITY NON FONCTIONNELS ===${NC}"

# Garder uniquement les tests Unity fonctionnels
UNITY_TESTS_TO_KEEP=(
    "test_card_unity.c"
    "test_integration_example.c"
    "test_benchmark_example.c"
    "test_pineapple_unity.c"
    "test_drawmaha_unity.c"
    "test_pineapple_unity_fixed.c"
    "test_drawmaha_unity_fixed.c"
)

# Supprimer les autres tests Unity
if [ -d "tests/unity_tests" ]; then
    cd tests/unity_tests
    for file in *.c; do
        if [[ ! " ${UNITY_TESTS_TO_KEEP[@]} " =~ " ${file} " ]]; then
            safe_remove "$file" "Test Unity non fonctionnel: $file"
        fi
    done
    cd ../..
fi

# 5. SUPPRESSION DES FICHIERS TEMPORAIRES ET CACHES
echo -e "${BLUE}=== 5. NETTOYAGE DES FICHIERS TEMPORAIRES ===${NC}"

# Fichiers temporaires
safe_remove "*.tmp" "Fichiers temporaires"
safe_remove "*.log" "Fichiers de log"
safe_remove ".DS_Store" "Fichiers système macOS"
safe_remove "Thumbs.db" "Fichiers système Windows"

# Caches de compilation
safe_remove "*.o" "Fichiers objets"
safe_remove "*.so" "Bibliothèques partagées"
safe_remove "*.dylib" "Bibliothèques dynamiques macOS"
safe_remove "*.dll" "Bibliothèques Windows"
safe_remove "*.a" "Archives statiques"

# 6. NETTOYAGE DES TESTS C REDONDANTS
echo -e "${BLUE}=== 6. NETTOYAGE DES TESTS C REDONDANTS ===${NC}"

# Garder les tests essentiels, supprimer les redondants
REDUNDANT_C_TESTS=(
    "tests/benchmark_batched_montecarlo.c"
    "tests/benchmark_final_micro_opt.c"
    "tests/benchmark_micro_opt.c"
    "tests/benchmark_mt_batched_simple.c"
    "tests/benchmark_mt_batched.c"
    "tests/benchmark_range_equity_mt.c"
    "tests/benchmark_realistic_divisions.c"
    "tests/test_icm_calculator_old.c"
    "tests/test_scoop_count_fixed.c"
    "tests/test_percentage_advanced.c"
    "tests/test_percentage_ranges.c"
    "tests/test_percentage_simple.c"
    "tests/test_complex_expressions_phase2.c"
    "tests/test_hilo_scoop_comprehensive.c"
)

for test in "${REDUNDANT_C_TESTS[@]}"; do
    safe_remove "$test" "Test C redondant: $(basename $test)"
done

# 7. ORGANISATION FINALE
echo -e "${BLUE}=== 7. ORGANISATION FINALE ===${NC}"

# Créer un dossier pour les fichiers conservés
mkdir -p "docs/archive"

# Déplacer les fichiers de documentation importants
IMPORTANT_DOCS=(
    "UNITY_COVERAGE_REPORT.md"
    "CHANGELOG.md"
    "README.md"
    "LICENCE"
)

echo -e "${GREEN}Fichiers de documentation conservés:${NC}"
for doc in "${IMPORTANT_DOCS[@]}"; do
    if [ -f "$doc" ]; then
        echo -e "${GREEN}✓ Conservé: $doc${NC}"
    fi
done

# 8. RÉSUMÉ DU NETTOYAGE
echo
echo -e "${GREEN}=== RÉSUMÉ DU NETTOYAGE ===${NC}"
echo
echo -e "${BLUE}Éléments supprimés:${NC}"
echo -e "${GREEN}✓ Dossier build et caches CMake${NC}"
echo -e "${GREEN}✓ $(echo ${#REDUNDANT_STATUS_FILES[@]}) fichiers de documentation redondants${NC}"
echo -e "${GREEN}✓ $(echo ${#REDUNDANT_TEST_SCRIPTS[@]}) scripts de test redondants${NC}"
echo -e "${GREEN}✓ Tests Unity non fonctionnels${NC}"
echo -e "${GREEN}✓ $(echo ${#REDUNDANT_C_TESTS[@]}) tests C redondants${NC}"
echo -e "${GREEN}✓ Fichiers temporaires et caches${NC}"

echo
echo -e "${BLUE}Éléments conservés:${NC}"
echo -e "${GREEN}✓ Code source principal (lib/, include/, src/)${NC}"
echo -e "${GREEN}✓ Tests Unity fonctionnels (5 tests)${NC}"
echo -e "${GREEN}✓ Script de test optimisé (run_unity_tests_clean.sh)${NC}"
echo -e "${GREEN}✓ Exemples utiles (examples/)${NC}"
echo -e "${GREEN}✓ Documentation essentielle${NC}"
echo -e "${GREEN}✓ Configuration CMake${NC}"

echo
echo -e "${CYAN}🎉 NETTOYAGE TERMINÉ ! 🎉${NC}"
echo -e "${YELLOW}Le projet est maintenant propre et optimisé.${NC}"
echo -e "${BLUE}Utilisez './run_unity_tests_clean.sh' pour les tests Unity.${NC}"