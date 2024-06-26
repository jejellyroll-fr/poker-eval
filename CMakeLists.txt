cmake_minimum_required(VERSION 3.10)
project(poker_eval)

# Désactiver les avertissements pour les fonctions non sécurisées
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

# Spécifier la version C standard
set(CMAKE_C_STANDARD 99)

# Inclure les répertoires des en-têtes
include_directories(include)

# Option pour choisir le mode de compilation
option(USE_FIVE_CARDS "Compile with five cards dealt" ON)

# Compiler les bibliothèques poker_lib (partagée et statique)
file(GLOB_RECURSE LIB_SOURCES "lib/*.c")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*mktab_.*|.*evx_generate.*|.*evx_gen[57].c$")

# Bibliothèque partagée avec -fPIC pour la position indépendante du code
add_library(poker_lib SHARED ${LIB_SOURCES})
target_compile_options(poker_lib PRIVATE -fPIC)

# Bibliothèque statique
add_library(poker_lib_static STATIC ${LIB_SOURCES})

# Installation des bibliothèques
install(TARGETS poker_lib poker_lib_static
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

# Définitions pour la bibliothèque partagée et statique
foreach(lib poker_lib poker_lib_static)
    if(USE_FIVE_CARDS)
        target_compile_definitions(${lib} PRIVATE CARDS_DEALT=5)
    else()
        target_compile_definitions(${lib} PRIVATE CARDS_DEALT=7)
    endif()
endforeach()

# Compiler les exemples en les liant avec la bibliothèque poker_lib statique
set(EXAMPLES eval fish hcmp2 hcmpn pokenum seven_card_hands usedecks)
foreach(example ${EXAMPLES})
    add_executable("example_${example}" "examples/${example}.c")
    # Lien avec la bibliothèque statique
    target_link_libraries("example_${example}" poker_lib_static)
    # Installation des exécutables
    install(TARGETS "example_${example}" 
            RUNTIME DESTINATION bin
            CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
endforeach()

# Compiler les outils de génération de tables comme exécutables distincts
set(MKTAB_TOOLS astud basic evx joker lowball packed short)
foreach(tool ${MKTAB_TOOLS})
    add_executable("mktab_${tool}" "lib/mktab_${tool}.c")
    # Lien avec la bibliothèque statique
    target_link_libraries("mktab_${tool}" poker_lib_static)
    # Installation des outils de génération de tables
    install(TARGETS "mktab_${tool}" 
            RUNTIME DESTINATION bin
            CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
endforeach()

# Assurez-vous que les fichiers evx_gen5.c et evx_gen7.c sont présents et nécessaires avant de les ajouter comme exécutables
# Si les fichiers evx_gen5.c et evx_gen7.c sont nécessaires et présents, décommentez les lignes suivantes et assurez-vous que les chemins d'accès sont corrects
# add_executable(evx_gen5 lib/evx_gen5.c)
# target_link_libraries(evx_gen5 poker_lib_static)
# add_executable(evx_gen7 lib/evx_gen7.c)
# target_link_libraries(evx_gen7 poker_lib_static)

# Compiler evx_gen en tant qu'exécutable distinct, si le fichier existe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/evx_generate.c")
    add_executable(evx_gen "lib/evx_generate.c")
    # Lien avec la bibliothèque statique
    target_link_libraries(evx_gen poker_lib_static)
    if(USE_FIVE_CARDS)
        target_compile_definitions(evx_gen PRIVATE CARDS_DEALT=5)
    else()
        target_compile_definitions(evx_gen PRIVATE CARDS_DEALT=7)
    endif()
    # Installation de evx_gen
    install(TARGETS evx_gen 
            RUNTIME DESTINATION bin
            CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
endif()
