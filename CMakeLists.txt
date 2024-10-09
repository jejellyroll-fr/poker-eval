cmake_minimum_required(VERSION 3.10)
project(poker_eval)

# Disable warnings for non-secure functions
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

# Specify the standard C version
set(CMAKE_C_STANDARD 99)

# Include header directories
include_directories(include inlines)

# Option to choose the compilation mode
option(USE_FIVE_CARDS "Compile with five cards dealt" ON)

# Compile the poker_lib libraries (shared and static)
file(GLOB_RECURSE LIB_SOURCES "lib/*.c")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*mktab_.*|.*evx_generate.*|.*evx_gen[57].c$")

# Add Card.c and CardConverter.c to the list of sources
list(APPEND LIB_SOURCES "lib/Card.c" "lib/CardConverter.c" "lib/HoldemAgnosticHand.c")

# Library shared with -fPIC for code-independent positioning
add_library(poker_lib SHARED ${LIB_SOURCES})
target_compile_options(poker_lib PRIVATE -fPIC)

# add def for shared lib
target_compile_definitions(poker_lib PRIVATE POKERVAL_DLL POKEREVAL_LIBRARY)

# Static library
add_library(poker_lib_static STATIC ${LIB_SOURCES})

# Installing libraries
install(TARGETS poker_lib poker_lib_static
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

# Definitions for the shared and static library
foreach(lib poker_lib poker_lib_static)
    if(USE_FIVE_CARDS)
        target_compile_definitions(${lib} PRIVATE CARDS_DEALT=5)
    else()
        target_compile_definitions(${lib} PRIVATE CARDS_DEALT=7)
    endif()
endforeach()

# Compile the examples by linking them with the static poker_lib library
set(EXAMPLES eval fish hcmp2 hcmpn pokenum seven_card_hands usedecks)
foreach(example ${EXAMPLES})
    add_executable("${example}" "examples/${example}.c")
    # Link with the static library
    target_link_libraries("${example}" poker_lib_static)
    # Installing executables
    install(TARGETS "${example}" 
            RUNTIME DESTINATION bin
            CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
endforeach()

# Compile table generation tools as separate executables
set(MKTAB_TOOLS astud basic evx joker lowball packed short)
foreach(tool ${MKTAB_TOOLS})
    add_executable("mktab_${tool}" "lib/mktab_${tool}.c")
    # Link with the static library
    target_link_libraries("mktab_${tool}" poker_lib_static)
    # Installation of table generation tools
    install(TARGETS "mktab_${tool}" 
            RUNTIME DESTINATION bin
            CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
endforeach()

# Make sure that the evx_gen5.c and evx_gen7.c files are present and required before adding them as executables.
# If the evx_gen5.c and evx_gen7.c files are required and present, uncomment the following lines and ensure that the paths are correct
# add_executable(evx_gen5 lib/evx_gen5.c)
# target_link_libraries(evx_gen5 poker_lib_static)
# add_executable(evx_gen7 lib/evx_gen7.c)
# target_link_libraries(evx_gen7 poker_lib_static)

# Compile evx_gen as a separate executable, if the file exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/evx_generate.c")
    add_executable(evx_gen "lib/evx_generate.c")
    # Link with the static library
    target_link_libraries(evx_gen poker_lib_static)
    if(USE_FIVE_CARDS)
        target_compile_definitions(evx_gen PRIVATE CARDS_DEALT=5)
    else()
        target_compile_definitions(evx_gen PRIVATE CARDS_DEALT=7)
    endif()
    # Installing evx_gen
    install(TARGETS evx_gen 
            RUNTIME DESTINATION bin
            CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Compiling test executables
add_executable(test_card tests/test_card.c)
target_link_libraries(test_card poker_lib_static)

add_executable(test_cardconverter tests/test_cardconverter.c)
target_link_libraries(test_cardconverter poker_lib_static)

add_executable(test_handagnostichand tests/test_handagnostichand.c)
target_link_libraries(test_handagnostichand poker_lib_static)

# Installing test executables
install(TARGETS test_card test_cardconverter test_handagnostichand
        RUNTIME DESTINATION bin
        CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
