#!/bin/bash

# Script pour générer un rapport de couverture de code pour poker-eval
# Utilisation: ./generate_coverage.sh

set -e

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Génération du rapport de couverture de code ===${NC}"

# Vérifier que les outils nécessaires sont installés
echo -e "${YELLOW}Vérification des dépendances...${NC}"
for tool in cmake gcc lcov genhtml; do
    if ! command -v $tool &> /dev/null; then
        echo -e "${RED}Erreur: $tool n'est pas installé${NC}"
        echo "Veuillez installer les outils nécessaires:"
        echo "  sudo apt-get install cmake gcc lcov"
        exit 1
    fi
done

# Nettoyer les anciennes données de couverture et les fichiers CMake du répertoire racine
echo -e "${YELLOW}Nettoyage des anciennes données de couverture...${NC}"
find . -name "*.gcda" -delete 2>/dev/null || true
find . -name "*.gcno" -delete 2>/dev/null || true
rm -rf coverage 2>/dev/null || true
# Nettoyer les fichiers CMake du répertoire racine pour éviter les conflits
rm -f CMakeCache.txt 2>/dev/null || true

# Créer le répertoire de build si nécessaire
BUILD_DIR="build-coverage"
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Création du répertoire de build...${NC}"
    mkdir -p "$BUILD_DIR"
fi

# Configurer avec CMake en activant la couverture
echo -e "${YELLOW}Configuration avec CMake (couverture activée)...${NC}"
cd "$BUILD_DIR"
cmake .. -DBUILD_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug

# Compiler le projet
echo -e "${YELLOW}Compilation du projet...${NC}"
# Détection du nombre de processeurs selon l'OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=$(nproc)
fi
make -j$JOBS

# Exécuter les tests avec CTest
echo -e "${YELLOW}Exécution des tests avec CTest...${NC}"
ctest --output-on-failure || echo -e "${RED}Certains tests ont échoué (voir ci-dessus)${NC}"

# Générer le rapport de couverture
echo -e "${YELLOW}Génération du rapport de couverture...${NC}"
make coverage-report

# Afficher le résumé
echo -e "${GREEN}=== Rapport de couverture généré ===${NC}"
echo "Le rapport HTML est disponible dans: $PWD/coverage/html/index.html"

# Afficher un résumé de la couverture
if [ -f "coverage/coverage.info" ]; then
    echo -e "\n${YELLOW}Résumé de la couverture:${NC}"
    lcov --summary coverage/coverage.info
fi

# Ouvrir le rapport dans le navigateur (optionnel)
if command -v xdg-open &> /dev/null; then
    echo -e "\n${YELLOW}Ouverture du rapport dans le navigateur...${NC}"
    xdg-open coverage/html/index.html
elif command -v open &> /dev/null; then
    open coverage/html/index.html
fi