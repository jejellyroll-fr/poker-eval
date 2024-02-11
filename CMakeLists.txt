cmake_minimum_required(VERSION 3.10)
project(poker_eval)

# Spécifier la version C standard
set(CMAKE_C_STANDARD 99)

# Inclure les répertoires des en-têtes
include_directories(include)

# Option pour choisir le mode de compilation
option(USE_FIVE_CARDS "Compile with five cards dealt" ON)

# Compiler la bibliothèque poker_eval
file(GLOB_RECURSE LIB_SOURCES "lib/*.c")
add_library(poker_lib ${LIB_SOURCES})

# Compiler les exemples en les liant avec la bibliothèque poker_lib
set(EXAMPLES eval fish five_card_hands getopt_w32 hcmp2 hcmpn pokenum seven_card_hands usedecks)
foreach(example ${EXAMPLES})
    add_executable("example_${example}" "examples/${example}.c")
    target_link_libraries("example_${example}" poker_lib)
endforeach()

# Compiler les outils de génération de tables en les liant avec la bibliothèque poker_lib
set(MKTAB_TOOLS astud basic evx joker lowball packed short)
foreach(tool ${MKTAB_TOOLS})
    add_executable("mktab_${tool}" "lib/mktab_${tool}.c")
    target_link_libraries("mktab_${tool}" poker_lib)
endforeach()

# Compiler evx_gen en tant qu'exécutable distinct
add_executable(evx_gen "lib/evx_generate.c")
if(USE_FIVE_CARDS)
    target_compile_definitions(evx_gen PRIVATE CARDS_DEALT=5)
else()
    target_compile_definitions(evx_gen PRIVATE CARDS_DEALT=7)
endif()

# Appliquer la même définition de CARDS_DEALT à la cible poker_lib si nécessaire
if(USE_FIVE_CARDS)
    target_compile_definitions(poker_lib PRIVATE CARDS_DEALT=5)
else()
    target_compile_definitions(poker_lib PRIVATE CARDS_DEALT=7)
endif()

# Ajouter le test enumtest7
add_executable(enumtest7 tests/enumtest7.c)
target_link_libraries(enumtest7 poker_lib)

# Ajouter le test joktest1
add_executable(joktest1 tests/joktest1.c)
target_link_libraries(joktest1 poker_lib)

# # Ajouter le test du shortdeck
# add_executable(shortdecktest tests/shortdecktest.c)
# target_link_libraries(shortdecktest poker_lib)

