cmake_minimum_required(VERSION 3.10)
project(poker_eval)

# Spécifier la version C standard
set(CMAKE_C_STANDARD 99)

# Inclure les répertoires des en-têtes
include_directories(include)

# Option pour choisir le mode de compilation
option(USE_FIVE_CARDS "Compile with five cards dealt" ON)

# Compiler les bibliothèques poker_lib (partagée et statique)
file(GLOB_RECURSE LIB_SOURCES "lib/*.c")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*mktab_.*|.*evx_generate.c$")

# Bibliothèque partagée
add_library(poker_lib SHARED ${LIB_SOURCES})
# Bibliothèque statique
add_library(poker_lib_static STATIC ${LIB_SOURCES})

# Définitions pour la bibliothèque partagée et statique
foreach(lib poker_lib poker_lib_static)
    if(USE_FIVE_CARDS)
        target_compile_definitions(${lib} PRIVATE CARDS_DEALT=5)
    else()
        target_compile_definitions(${lib} PRIVATE CARDS_DEALT=7)
    endif()
endforeach()

# Compiler les exemples en les liant avec la bibliothèque poker_lib statique
set(EXAMPLES eval fish five_card_hands getopt_w32 hcmp2 hcmpn pokenum seven_card_hands usedecks)
foreach(example ${EXAMPLES})
    add_executable("example_${example}" "examples/${example}.c")
    # Lien avec la bibliothèque statique
    target_link_libraries("example_${example}" poker_lib_static)
endforeach()

# Compiler les outils de génération de tables comme exécutables distincts
set(MKTAB_TOOLS astud basic evx joker lowball packed short)
foreach(tool ${MKTAB_TOOLS})
    add_executable("mktab_${tool}" "lib/mktab_${tool}.c")
    # Lien avec la bibliothèque statique
    target_link_libraries("mktab_${tool}" poker_lib_static)
endforeach()

# Compiler evx_gen en tant qu'exécutable distinct
add_executable(evx_gen "lib/evx_generate.c")
# Lien avec la bibliothèque statique
target_link_libraries(evx_gen poker_lib_static)
if(USE_FIVE_CARDS)
    target_compile_definitions(evx_gen PRIVATE CARDS_DEALT=5)
else()
    target_compile_definitions(evx_gen PRIVATE CARDS_DEALT=7)
endif()

# Ajouter des tests si nécessaire, en les liant avec la bibliothèque poker_lib statique
# Exemple:
# add_executable(enumtest7 tests/enumtest7.c)
# target_link_libraries(enumtest7 poker_lib_static)

# Autres tests
# Répétez le schéma ci-dessus pour les autres tests que vous souhaitez ajouter
