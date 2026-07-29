#!/bin/bash

# Script simplifié pour générer un rapport de couverture de code

set -e

# Couleurs pour l'affichage
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Génération Simplifiée du Rapport de Couverture ===${NC}"
echo

# Nettoyer et créer le dossier build
rm -rf build
mkdir -p build
cd build

# Configuration CMake avec couverture activée
echo -e "${YELLOW}Configuration CMake avec couverture activée...${NC}"
cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug

# Compilation des tests Unity seulement
echo -e "${YELLOW}Compilation des tests Unity...${NC}"
make unity test_card_unity test_integration_example test_benchmark_example

# Nettoyage des données de couverture précédentes
echo -e "${YELLOW}Nettoyage des données de couverture précédentes...${NC}"
find . -name "*.gcda" -delete 2>/dev/null || true
find . -name "*.gcno" -delete 2>/dev/null || true

# Exécution des tests Unity
echo -e "${YELLOW}Exécution des tests Unity...${NC}"
./tests/test_card_unity
./tests/test_integration_example  
./tests/test_benchmark_example

# Compilation et exécution de quelques tests de base
echo -e "${YELLOW}Compilation et exécution de tests de base...${NC}"
make test_card test_cardconverter test_deck_operations 2>/dev/null || echo "Certains tests n'ont pas pu être compilés"

if [ -f "tests/test_card" ]; then
    ./tests/test_card || echo "test_card échoué"
fi

if [ -f "tests/test_cardconverter" ]; then
    ./tests/test_cardconverter || echo "test_cardconverter échoué"
fi

if [ -f "tests/test_deck_operations" ]; then
    ./tests/test_deck_operations || echo "test_deck_operations échoué"
fi

# Génération du rapport de couverture
echo -e "${YELLOW}Génération du rapport de couverture...${NC}"

# Créer le dossier de couverture
mkdir -p coverage

# Capturer les données de couverture
lcov --capture --directory . --output-file coverage/coverage.info --ignore-errors gcov

# Filtrer les fichiers système et de test
lcov --remove coverage/coverage.info '/usr/*' '*/tests/*' '*/unity/*' --output-file coverage/coverage.info --ignore-errors gcov

# Générer le rapport HTML
genhtml coverage/coverage.info --output-directory coverage/html --ignore-errors source

# Affichage des résultats
echo
echo -e "${GREEN}=== Rapport de Couverture Généré ===${NC}"
echo -e "Rapport HTML: ${BLUE}build/coverage/html/index.html${NC}"

# Extraction des statistiques principales
if [ -f "coverage/coverage.info" ]; then
    echo
    echo -e "${YELLOW}=== Statistiques de Couverture ===${NC}"
    lcov --summary coverage/coverage.info 2>/dev/null || echo "Impossible d'extraire les statistiques"
fi

echo
echo -e "${GREEN}✓ Rapport de couverture généré avec succès !${NC}"
echo -e "Ouvrez ${BLUE}build/coverage/html/index.html${NC} dans votre navigateur."
