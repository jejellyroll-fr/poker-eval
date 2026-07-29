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
)

# Standalone table generators. Each defines its own main(), so linking them into
# the library produces seven duplicate _main symbols in a shared build - harmless
# in a static archive only because the objects are never pulled in. They are
# built on demand (cmake --build . --target poker_table_generators) to regenerate
# the committed tables under tables/ and src/core.
set(TABLE_GENERATORS
    mktab_astud
    mktab_basic
    mktab_evx
    mktab_joker
    mktab_lowball
    mktab_packed
    mktab_short
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
add_library(poker_utils ${UTILS_SOURCES})
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

# Table generators, excluded from the default build (see TABLE_GENERATORS above).
add_custom_target(poker_table_generators)
foreach(generator IN LISTS TABLE_GENERATORS)
    add_executable(${generator} EXCLUDE_FROM_ALL ${generator}.c mktable.c mktable_utils.c)
    target_include_directories(${generator} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/inlines
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/core/inlines
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${generator} PRIVATE -Wno-error)
    endif()
    add_dependencies(poker_table_generators ${generator})
endforeach()
