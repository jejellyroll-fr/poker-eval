#!/bin/bash

# Script pour exécuter tous les tests Unity et générer un rapport de couverture

set -e

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Tests Unity avec Couverture de Code - poker-eval ===${NC}"
echo

# Nettoyer le dossier build précédent
if [ -d "build-unity-coverage" ]; then
    echo -e "${YELLOW}Nettoyage du build précédent...${NC}"
    rm -rf build-unity-coverage
fi

# Créer le dossier de build pour la couverture
echo -e "${YELLOW}Création du dossier build-unity-coverage...${NC}"
mkdir -p build-unity-coverage
cd build-unity-coverage

# Configuration CMake avec couverture activée
echo -e "${YELLOW}Configuration CMake avec couverture de code...${NC}"
cmake .. \
    -DBUILD_TESTS=ON \
    -DBUILD_COVERAGE=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="--coverage -fprofile-arcs -ftest-coverage -O0 -g" \
    -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage -O0 -g"

# Compilation de tous les tests Unity
echo -e "${YELLOW}Compilation de tous les tests Unity...${NC}"
make -j4

echo
echo -e "${GREEN}=== Exécution des Tests Unity ===${NC}"
echo

# Liste des tests Unity à exécuter
UNITY_TESTS=(
    "test_card_unity"
    "test_integration_example"
    "test_benchmark_example"
    "test_cardconverter_unity"
    "test_deck_std"
    "test_deck_mask"
    "test_deck_generic"
    "test_eval_basic"
    "test_handval_utils_fixed"
    "test_eval_tables"
    "test_card_extended"
    "test_enumerate_basic"
    "test_deck_operations_unity"
    "test_lowball_comprehensive"
    "test_range_equity_basic"
    "test_eval_cache_unity"
    "test_eval_comprehensive"
    "test_omaha_unity"
    "test_joker_unity"
    "test_short_deck_unity"
)

# Compteurs pour les statistiques
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Exécuter chaque test Unity
for test in "${UNITY_TESTS[@]}"; do
    echo -e "${BLUE}Exécution de $test...${NC}"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [ -f "tests/$test" ]; then
        if ./tests/$test; then
            echo -e "${GREEN}✓ $test PASSED${NC}"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo -e "${RED}✗ $test FAILED${NC}"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    else
        echo -e "${YELLOW}⚠ $test NOT FOUND (skipping)${NC}"
        TOTAL_TESTS=$((TOTAL_TESTS - 1))
    fi
    echo
done

# Exécuter aussi les tests CTest pour comparaison
echo -e "${BLUE}Exécution des tests CTest Unity...${NC}"
ctest -R "unity|test_card_extended|test_enumerate_basic|test_deck_operations|test_lowball_comprehensive|test_range_equity_basic|test_eval_cache_unity" --output-on-failure || true

echo
echo -e "${GREEN}=== Génération du Rapport de Couverture ===${NC}"

# Vérifier si lcov est disponible
if command -v lcov >/dev/null 2>&1; then
    echo -e "${YELLOW}Génération des données de couverture avec lcov...${NC}"
    
    # Capturer les données de couverture
    lcov --capture --directory . --output-file coverage.info --ignore-errors gcov
    
    # Filtrer les fichiers système et de test
    lcov --remove coverage.info '/usr/*' '*/tests/*' '*/unity/*' --output-file coverage_filtered.info --ignore-errors gcov
    
    # Générer le rapport HTML
    if command -v genhtml >/dev/null 2>&1; then
        echo -e "${YELLOW}Génération du rapport HTML...${NC}"
        genhtml coverage_filtered.info --output-directory coverage_html --ignore-errors source
        
        echo -e "${GREEN}Rapport de couverture généré dans: coverage_html/index.html${NC}"
        
        # Ouvrir le rapport automatiquement (macOS)
        if [[ "$OSTYPE" == "darwin"* ]]; then
            open coverage_html/index.html
        fi
    else
        echo -e "${YELLOW}genhtml non trouvé, rapport HTML non généré${NC}"
    fi
    
    # Afficher un résumé de la couverture
    echo -e "${BLUE}Résumé de la couverture:${NC}"
    lcov --summary coverage_filtered.info --ignore-errors gcov
    
else
    echo -e "${YELLOW}lcov non trouvé, utilisation de gcov basique...${NC}"
    
    # Utiliser gcov directement
    find . -name "*.gcda" -exec gcov {} \; > /dev/null 2>&1 || true
    
    echo -e "${GREEN}Fichiers de couverture .gcov générés${NC}"
fi

echo
echo -e "${GREEN}=== Résumé des Tests Unity ===${NC}"
echo -e "Total des tests: $TOTAL_TESTS"
echo -e "Tests réussis: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Tests échoués: ${RED}$FAILED_TESTS${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}✓ Tous les tests Unity ont réussi !${NC}"
else
    echo -e "${YELLOW}⚠ $FAILED_TESTS test(s) ont échoué${NC}"
fi

# Calculer le pourcentage de réussite
if [ $TOTAL_TESTS -gt 0 ]; then
    SUCCESS_RATE=$((PASSED_TESTS * 100 / TOTAL_TESTS))
    echo -e "Taux de réussite: ${GREEN}$SUCCESS_RATE%${NC}"
fi

echo
echo -e "${BLUE}=== Tests Unity avec Couverture Terminés ===${NC}"

# Retourner le code d'erreur approprié
if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
else
    exit 0
fi