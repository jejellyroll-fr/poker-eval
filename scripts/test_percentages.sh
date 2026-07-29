#!/bin/bash

# Script de test rapide pour les pourcentages
# Phase 2 - Étape 2 : Validation de l'implémentation

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Couleurs pour l'affichage
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}🧪 Test rapide des pourcentages - Advanced Range Parser${NC}"
echo -e "${BLUE}=====================================================${NC}"

cd "$PROJECT_ROOT"

# Vérifier que le build existe
if [ ! -d "build" ]; then
    echo -e "${YELLOW}⚠️  Build directory not found. Building project...${NC}"
    ./scripts/build.sh --clean
    if [ $? -ne 0 ]; then
        echo -e "${RED}❌ Build failed${NC}"
        exit 1
    fi
fi

# Compiler le test des pourcentages
echo -e "${BLUE}🔨 Compiling percentage tests...${NC}"
gcc -I include -I build/include -L build/lib -o test_percentage_ranges tests/test_percentage_ranges.c -lpoker-eval -std=c99

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Compilation failed${NC}"
    exit 1
fi

# Exécuter les tests
echo -e "${BLUE}🚀 Running percentage tests...${NC}"
echo ""

DYLD_LIBRARY_PATH=build/lib:$DYLD_LIBRARY_PATH ./test_percentage_ranges

TEST_RESULT=$?

# Nettoyer
rm -f test_percentage_ranges

# Résultat final
echo ""
if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}🎉 All percentage tests passed!${NC}"
    echo -e "${GREEN}✅ Phase 2 - Étape 2 : Implementation validated${NC}"
else
    echo -e "${RED}❌ Some tests failed${NC}"
    exit 1
fi

echo ""
echo -e "${BLUE}📊 Quick validation summary:${NC}"
echo -e "${GREEN}✅ Percentage parsing (5%, 20%, 5.5%)${NC}"
echo -e "${GREEN}✅ Hand ranking and progression${NC}"
echo -e "${GREEN}✅ Premium hands in top ranges${NC}"
echo -e "${GREEN}✅ Consistency between percentages${NC}"
echo -e "${GREEN}✅ Utility functions${NC}"
echo ""
echo -e "${BLUE}🚀 Ready for production use!${NC}"