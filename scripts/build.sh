#!/bin/bash

# Script de build pour poker-eval avec CMake

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Couleurs pour l'affichage
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --clean      Clean build directory before building"
    echo "  --debug      Build in debug mode"
    echo "  --release    Build in release mode (default)"
    echo "  --tests      Build and run tests"
    echo "  --help       Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build in release mode"
    echo "  $0 --clean --debug   # Clean build in debug mode"
    echo "  $0 --tests           # Build and run tests"
}

# Variables par défaut
BUILD_TYPE="Release"
CLEAN_BUILD=false
RUN_TESTS=false

# Traitement des arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --tests)
            RUN_TESTS=true
            shift
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

echo -e "${BLUE}🔨 Building poker-eval project${NC}"
echo -e "${BLUE}================================${NC}"

cd "$PROJECT_ROOT"

# Nettoyer si demandé
if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${YELLOW}🧹 Cleaning build directory...${NC}"
    rm -rf build
fi

# Créer le dossier build
echo -e "${BLUE}📁 Creating build directory...${NC}"
mkdir -p build
cd build

# Configuration CMake
echo -e "${BLUE}⚙️  Configuring with CMake (${BUILD_TYPE})...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ CMake configuration failed${NC}"
    exit 1
fi

# Build
echo -e "${BLUE}🔨 Building project...${NC}"
make -j$(nproc 2>/dev/null || echo 4)

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Build failed${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Build completed successfully${NC}"

# Exécuter les tests si demandé
if [ "$RUN_TESTS" = true ]; then
    echo -e "${BLUE}🧪 Running tests...${NC}"
    echo -e "${BLUE}==================${NC}"
    
    # Test des pourcentages
    if [ -f "tests/test_advanced_range_parser" ]; then
        echo -e "${YELLOW}Testing Advanced Range Parser...${NC}"
        ./tests/test_advanced_range_parser
        echo ""
    fi
    
    # Autres tests disponibles
    if [ -f "tests/test_enum_basic" ]; then
        echo -e "${YELLOW}Testing basic enumeration...${NC}"
        ./tests/test_enum_basic
        echo ""
    fi
    
    echo -e "${GREEN}✅ Tests completed${NC}"
fi

echo -e "${GREEN}🎉 All done!${NC}"
echo -e "${BLUE}Build artifacts are in: ${PROJECT_ROOT}/build${NC}"