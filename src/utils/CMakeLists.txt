# Utils module CMakeLists.txt

# Utils source files
set(UTILS_SOURCES
    combinations.c
    icm_calculator.c
    universal_deck.c
    poker_wrapper.c
    poker_eval_modern.c
    aligned_tables.c
    aligned_tables_dynamic.c
    compressed_tables.c
    mktable.c
    mktable_utils.c
    mktab_astud.c
    mktab_basic.c
    mktab_evx.c
    mktab_joker.c
    mktab_lowball.c
    mktab_packed.c
    mktab_short.c
)

# Table generation source files
file(GLOB TABLE_SOURCES "tables/t_*.c")
set(UTILS_SOURCES ${UTILS_SOURCES} ${TABLE_SOURCES})

# Utils header files
set(UTILS_HEADERS
    ${CMAKE_SOURCE_DIR}/include/combinations.h
    ${CMAKE_SOURCE_DIR}/include/universal_deck.h
    ${CMAKE_SOURCE_DIR}/include/micro_optimizations.h
    ${CMAKE_SOURCE_DIR}/include/poker_wrapper.h
    ${CMAKE_SOURCE_DIR}/include/pokereval_export.h
    ${CMAKE_SOURCE_DIR}/include/mktable.h
    ${CMAKE_SOURCE_DIR}/include/poker/utils/aligned_tables.h
    ${CMAKE_SOURCE_DIR}/include/poker/utils/aligned_tables_dynamic.h
    ${CMAKE_SOURCE_DIR}/include/poker/utils/compressed_tables.h
)

# Inline headers
file(GLOB INLINE_HEADERS "inlines/*.h")
set(UTILS_HEADERS ${UTILS_HEADERS} ${INLINE_HEADERS})

# Add utils library
add_library(poker_utils STATIC ${UTILS_SOURCES})
set_property(TARGET poker_utils PROPERTY POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(poker_utils PRIVATE -Wno-error=conversion -Wno-error=array-bounds -Wno-error=float-equal)
endif()

# Link with core module
target_link_libraries(poker_utils poker_core)

# Set include directories
target_include_directories(poker_utils PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/inlines
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/inlines
)

# Install headers
install(FILES ${UTILS_HEADERS} DESTINATION include/poker/utils)
install(DIRECTORY inlines/ DESTINATION include/poker/utils/inlines
        FILES_MATCHING PATTERN "*.h")

# Install library
install(TARGETS poker_utils
        EXPORT poker-eval-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

# Pre-flop table generator utility
add_executable(generate_preflop_table generate_preflop_table.c)
target_link_libraries(generate_preflop_table
    poker_equity
    poker_core
    poker_distributions
    poker_utils
    poker_games
)
target_include_directories(generate_preflop_table PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
