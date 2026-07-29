#!/bin/bash

# Script pour exécuter les tests avec CTest et générer un rapport de couverture
# Utilisation: ./run_tests_coverage.sh [options ctest]

set -e

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Exécution des tests avec CTest et génération de la couverture ===${NC}"

# Vérifier que nous sommes dans le bon répertoire
if [ ! -f "CMakeLists.txt" ]; then
    echo -e "${RED}Erreur: Ce script doit être exécuté depuis la racine du projet${NC}"
    exit 1
fi

# Créer le répertoire de build si nécessaire
BUILD_DIR="build-coverage"
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Création du répertoire de build...${NC}"
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Vérifier si le projet est configuré pour la couverture
if [ ! -f "CMakeCache.txt" ] || ! grep -q "BUILD_COVERAGE:BOOL=ON" CMakeCache.txt; then
    echo -e "${YELLOW}Configuration du projet avec la couverture activée...${NC}"
    cmake .. -DBUILD_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
fi

# Compiler si nécessaire
if [ ! -f "Makefile" ]; then
    echo -e "${RED}Erreur: Makefile non trouvé${NC}"
    exit 1
fi

echo -e "${YELLOW}Compilation du projet...${NC}"
make -j$(nproc)

# Nettoyer les anciennes données de couverture
echo -e "${YELLOW}Nettoyage des anciennes données de couverture...${NC}"
find . -name "*.gcda" -delete 2>/dev/null || true

# Exécuter les tests avec CTest
echo -e "${BLUE}=== Exécution des tests avec CTest ===${NC}"
if [ $# -eq 0 ]; then
    # Pas d'arguments, exécuter tous les tests
    ctest --output-on-failure
else
    # Passer les arguments à ctest
    ctest "$@"
fi

# Capturer le code de retour de ctest
CTEST_RESULT=$?

# Afficher un résumé des tests
echo -e "\n${BLUE}=== Résumé des tests ===${NC}"
ctest --quiet --no-tests=error || true

# Générer le rapport de couverture
echo -e "\n${YELLOW}Génération du rapport de couverture...${NC}"
mkdir -p coverage

# Capturer les données de couverture
lcov --capture --directory . --output-file coverage/coverage.info --ignore-errors gcov 2>/dev/null

# Filtrer les fichiers non pertinents
lcov --remove coverage/coverage.info '/usr/*' '*/tests/*' --output-file coverage/coverage_filtered.info --ignore-errors gcov,unused 2>/dev/null

# Générer le rapport HTML
genhtml coverage/coverage_filtered.info --output-directory coverage/html --ignore-errors source 2>/dev/null

# Afficher le résumé de la couverture
echo -e "\n${GREEN}=== Résumé de la couverture ===${NC}"
lcov --summary coverage/coverage_filtered.info 2>/dev/null || echo "Impossible d'afficher le résumé"

echo -e "\n${GREEN}=== Rapport généré ===${NC}"
echo "Rapport HTML disponible dans: $PWD/coverage/html/index.html"

# Retourner le code de sortie de ctest
exit $CTEST_RESULT