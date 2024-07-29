#!/bin/bash

# Arrête le script si une commande échoue
set -e

# Fonction pour détecter l'OS
detect_os() {
    case "$(uname -s)" in
        Linux*)     echo "Linux";;
        Darwin*)    echo "MacOS";;
        CYGWIN*|MINGW32*|MSYS*|MINGW*) echo "Windows";;
        *)          echo "unknown"
    esac
}

OS=$(detect_os)
echo "Detected OS: $OS"

# Définir le chemin de base
BASE_PATH=$(pwd)

# Build poker-eval
echo "Building poker-eval..."
#cd "${BASE_PATH}/fpdb-3/pypoker-eval/poker-eval"
mkdir -p build
cd build
if [[ "$OS" == "Windows" ]]; then
    cmake .. -G "Visual Studio 17 2022"
    cmake --build .
elif [[ "$OS" == "Linux" || "$OS" == "MacOS" ]]; then
    cmake ..
    make
fi
# Retour au chemin de base pour éviter les confusions
cd "${BASE_PATH}"