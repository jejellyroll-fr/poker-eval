# Equity module CMakeLists.txt

# Equity source files
set(EQUITY_SOURCES
    pcg_rng.c
    enumerate.c
    enumerate_dispatch.c
    canonicalize.c
    board_stats.c
    enumerate_eedc.c
    enumerate_eedc_omaha_opt.c
    range_combo_buffers.c
    enumerate_doubleflop_fix.c
    enumerate_doubleflop_simd.c
    enumord.c
    RangeEquity.c
    MultiwayEquity.c
    batched_montecarlo.c
    sidepots.c
    simd_operations.c
    sampling_policies.c
    preflop_equity.c
    omaha_preflop.c
    pineapple_preflop.c
    flop_equity.c
    run_it_twice.c
    pe_equity.c
    poker_eval_modern_equity.c
    draw_optimizer.c
)

# Equity header files
set(EQUITY_HEADERS
    enumerate.h
    canonicalize.h
    enumerate_eedc.h
    enumerate_simd.h
    enumdefs.h
    enumord.h
    RangeEquity.h
    RangeEquity_MT.h
    sidepots.h
    batched_montecarlo.h
    simd_operations.h
    sampling_policies.h
    preflop_equity.h
    omaha_preflop.h
    pineapple_preflop.h
    flop_equity.h
    run_it_twice.h
)

# Multithreaded sources (guarded by OpenMP availability)
set(EQUITY_MT_SOURCES
    RangeEquity_MT.c
    RangeEquity_MT_v2.c
    RangeEquity_MT_v3.c
    RangeEquity_MT_v4.c
    RangeEquity_MT_v5.c
    RangeEquity_MT_Batched.c
)

# OpenMP is looked up once by the root CMakeLists, which also decides whether it
# is actually usable. Calling find_package(OpenMP) again here would repopulate
# OpenMP_C_FOUND in this scope and silently override that decision, so the module
# reads POKER_EVAL_WITH_OPENMP instead.

# CalculateEquityForRanges_Auto (defined in RangeEquity_MT.c) is part of the public
# API and is called unconditionally by tests and downstream code. Always compile it;
# omp_compat.h provides single-threaded fallback stubs when OpenMP is unavailable.
list(APPEND EQUITY_SOURCES RangeEquity_MT.c)

# The remaining MT variants rely on real OpenMP for any benefit, so only compile
# them when OpenMP is available. This must use the same decision as the link step
# below: compiling them without OpenMP::OpenMP_C would fall back to the
# omp_compat.h stubs and make explicitly multithreaded APIs run sequentially.
if(POKER_EVAL_WITH_OPENMP)
    list(APPEND EQUITY_SOURCES
        RangeEquity_MT_v2.c
        RangeEquity_MT_v3.c
        RangeEquity_MT_v4.c
        RangeEquity_MT_v5.c
        RangeEquity_MT_Batched.c)
endif()

# Add equity library
add_library(poker_equity ${EQUITY_SOURCES})
set_property(TARGET poker_equity PROPERTY POSITION_INDEPENDENT_CODE ON)

# Disable treating warnings as errors for equity module (has many optimization-related warnings)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(poker_equity PRIVATE -Wno-error)
endif()

# Link with core, games, and distributions modules
# The declared dependency on poker_distributions was backwards: equity defines
# CalculateEquityForRanges, which distributions consumes, not the reverse. The
# false edge is what made the module graph look circular.
target_link_libraries(poker_equity poker_core poker_games poker_utils m)

# Set include directories
target_include_directories(poker_equity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/inlines
    ${CMAKE_SOURCE_DIR}/games
    ${CMAKE_SOURCE_DIR}/games/inlines
    ${CMAKE_SOURCE_DIR}/distributions
)

if(POKER_EVAL_WITH_OPENMP)
    target_link_libraries(poker_equity OpenMP::OpenMP_C)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(poker_equity PRIVATE -Wno-error=pedantic -Wno-pedantic -Wno-undef)
    endif()
endif()

# Install library
install(TARGETS poker_equity
        EXPORT poker-eval-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
