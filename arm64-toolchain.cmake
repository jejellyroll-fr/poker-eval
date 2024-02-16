# arm64-toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Chemin vers les compilateurs croisés
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Désactivez la recherche de programmes dans le chemin du système hôte
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Recherche uniquement dans le chemin de racine pour les bibliothèques et les en-têtes
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
