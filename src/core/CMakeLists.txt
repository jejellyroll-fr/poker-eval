# Core module CMakeLists.txt

# Core source files
set(CORE_SOURCES
    deck.c
    deck_std.c
    deck_joker.c
    deck_short.c
    deck_astud.c
    generalized_deck.c
    evx.c
    # Core helpers and modern implementations
    card.c
    CardConverter.c
    modern_cardmask.c
    eval_context.c
    canonical_5card.c
    modern_combinations.c
    omaha_combinations.c
    combo_7to5.c
    combo_7to5_hilo.c
    eval_7c_simd.c
    eval_cache.c
    status.c
    deterministic_benchmark.c
    joker_expansion_controlled.c
    configurable_ranking.c
    ../games/lowball_algorithm.c
    low_eval.c
    low_qualifier.c
    # Lowball lookup tables
    t_botcard.c
    t_botfivecards.c
    t_topcard.c
    t_topfivecards.c
    # Common lookup tables
    t_nbits.c
    t_straight.c
    # Card masks tables for all deck types
    t_cardmasks.c
    t_shortdeckcardmasks.c
    t_jokercardmasks.c
    t_astudcardmasks.c
    universal_deck.c
)

# Core header files
set(CORE_HEADERS
    poker_defs.h
    poker_config.h
    handval.h
    handval_low.h
    configurable_ranking.h
    deck.h
    deck_std.h
    deck_joker.h
    deck_short.h
    deck_astud.h
    generalized_deck.h
    deck_undef.h
    pokereval_export.h
    evx_defs.h
    combinations.h
    low_eval.h
    low_qualifier.h
    inlines/eval.h
    inlines/eval_type.h
    inlines/eval_cached.h
    inlines/eval_compressed.h
)

# Add core library
add_library(poker_core ${CORE_SOURCES})
set_property(TARGET poker_core PROPERTY POSITION_INDEPENDENT_CODE ON)

# A shared library has to resolve libm itself on Linux; macOS gets it from
# libSystem, which is why a static-only build never needed this. Windows has a
# no-op m target created by the root CMakeLists.
target_link_libraries(poker_core m)

# Set include directories
target_include_directories(poker_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/inlines
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/poker_eval
    ${CMAKE_SOURCE_DIR}/include/poker_eval/core
    ${CMAKE_SOURCE_DIR}/src/games
    ${CMAKE_SOURCE_DIR}/src/utils
)

# Install library
install(TARGETS poker_core
        EXPORT poker-eval-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

# Enable target-specific SIMD flags for eval_7c_simd.c
if(HAS_AVX512)
    set_source_files_properties(eval_7c_simd.c PROPERTIES COMPILE_OPTIONS "-mavx512f")
elseif(HAS_AVX2)
    set_source_files_properties(eval_7c_simd.c PROPERTIES COMPILE_OPTIONS "-mavx2")
endif()

# Disable false positive warning for deterministic_benchmark.c with GCC-12
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0)
    set_source_files_properties(deterministic_benchmark.c PROPERTIES COMPILE_OPTIONS "-Wno-error=maybe-uninitialized")
endif()
