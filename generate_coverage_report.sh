#!/bin/bash

# Script pour générer un rapport de couverture de code complet

set -e

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Génération du Rapport de Couverture de Code ===${NC}"
echo

# Vérifier les prérequis
echo -e "${YELLOW}Vérification des prérequis...${NC}"

# Vérifier lcov
if ! command -v lcov &> /dev/null; then
    echo -e "${RED}Erreur: lcov n'est pas installé${NC}"
    echo "Installation:"
    echo "  Ubuntu/Debian: sudo apt-get install lcov"
    echo "  macOS: brew install lcov"
    echo "  CentOS/RHEL: sudo yum install lcov"
    exit 1
fi

# Vérifier genhtml
if ! command -v genhtml &> /dev/null; then
    echo -e "${RED}Erreur: genhtml n'est pas installé (fait partie de lcov)${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Prérequis vérifiés${NC}"

# Créer le dossier build s'il n'existe pas
if [ ! -d "build" ]; then
    echo -e "${YELLOW}Création du dossier build...${NC}"
    mkdir -p build
fi

cd build

# Configuration CMake avec couverture activée
echo -e "${YELLOW}Configuration CMake avec couverture activée...${NC}"
cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug

# Compilation
echo -e "${YELLOW}Compilation du projet...${NC}"
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Nettoyage des données de couverture précédentes
echo -e "${YELLOW}Nettoyage des données de couverture précédentes...${NC}"
make coverage-clean 2>/dev/null || true

# Exécution des tests Unity
echo -e "${YELLOW}Exécution des tests Unity...${NC}"
if [ -f "tests/test_card_unity" ]; then
    ./tests/test_card_unity || echo "Test test_card_unity échoué"
fi

if [ -f "tests/test_integration_example" ]; then
    ./tests/test_integration_example || echo "Test test_integration_example échoué"
fi

if [ -f "tests/test_benchmark_example" ]; then
    ./tests/test_benchmark_example || echo "Test test_benchmark_example échoué"
fi

# Exécution des tests existants (sélection des plus importants)
echo -e "${YELLOW}Exécution des tests principaux...${NC}"

# Tests de base
test_files=(
    "test_card"
    "test_cardconverter"
    "test_deck_operations"
    "test_hand_evaluation_high"
    "test_lowball"
    "test_simple_equity"
    "test_enum_basic"
    "test_modern_api"
)

for test in "${test_files[@]}"; do
    if [ -f "tests/$test" ]; then
        echo "Exécution de $test..."
        ./tests/$test || echo "Test $test échoué"
    fi
done

# Génération du rapport de couverture
echo -e "${YELLOW}Génération du rapport de couverture...${NC}"
make coverage-report

# Affichage des résultats
echo
echo -e "${GREEN}=== Rapport de Couverture Généré ===${NC}"
echo -e "Rapport HTML: ${BLUE}build/coverage/html/index.html${NC}"
echo -e "Fichier de données: ${BLUE}build/coverage/coverage.info${NC}"

# Extraction des statistiques principales
if [ -f "coverage/coverage.info" ]; then
    echo
    echo -e "${YELLOW}=== Statistiques de Couverture ===${NC}"
    lcov --summary coverage/coverage.info 2>/dev/null | grep -E "(lines|functions|branches)" || echo "Impossible d'extraire les statistiques"
fi

echo
echo -e "${GREEN}✓ Rapport de couverture généré avec succès !${NC}"
echo -e "Ouvrez ${BLUE}build/coverage/html/index.html${NC} dans votre navigateur pour voir le rapport détaillé."