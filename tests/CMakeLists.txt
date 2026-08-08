# Ensure OpenMP flag is honored for test gating
if(NOT ENABLE_OPENMP)
    set(OpenMP_FOUND FALSE)
endif()

# Some OpenMP runtimes fall back to /tmp for temp files and warn if it is not writable.
set(PE_TEST_TMPDIR "${CMAKE_BINARY_DIR}/tmp")
file(MAKE_DIRECTORY "${PE_TEST_TMPDIR}")

# Unity framework setup
# Check if Unity is available, if not download it
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unity/src/unity.c")
    message(STATUS "Unity not found, downloading...")
    find_package(Git REQUIRED)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} clone https://github.com/ThrowTheSwitch/Unity.git unity
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE git_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT git_result EQUAL "0")
        message(FATAL_ERROR "Failed to download Unity framework")
    endif()
    message(STATUS "Unity downloaded successfully")
endif()

add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Unity is vendored third-party code; keep project warnings visible without
    # making compiler-version-specific diagnostics fatal.
    target_compile_options(unity PRIVATE -Wno-error -Wno-float-equal)
endif()
# UNITY_SUPPORT_64 is auto-detected only when long or pointers are 64-bit, so a
# 32-bit target such as the armhf cross-build loses it. There the 64-bit
# assertion macros expand to an unconditional "64-bit support not available"
# failure that references neither of its arguments, which both breaks the build
# under -Werror=unused-variable and would fail every such test at runtime.
# Unity handles 64-bit on a 32-bit target through long long, and this library's
# card masks are 64-bit throughout, so ask for it explicitly. PUBLIC because the
# macros live in the header the tests include.
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_SUPPORT_64)

# Helper function to add Unity tests
function(add_unity_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_include_directories(${test_name} PRIVATE
        unity/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/inlines
        ${CMAKE_BINARY_DIR}/include
    )
    # -Wno-stack-usage is GCC-specific, Clang doesn't support it
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${test_name} PRIVATE -Wno-error -Wno-stack-usage)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(${test_name} PRIVATE -Wno-error)
    endif()
    target_link_libraries(${test_name} PRIVATE unity)
    if(TARGET poker_eval_modular)
        target_link_libraries(${test_name} PRIVATE poker_eval_modular)
    elseif(TARGET poker-eval)
        target_link_libraries(${test_name} PRIVATE poker-eval)
    else()
        target_link_libraries(${test_name} PRIVATE poker-eval-static)
    endif()
    if(NOT APPLE)
        target_link_libraries(${test_name} PRIVATE m)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)

    # Auto-label tests by name for easier filtering
    string(TOLOWER "${test_name}" _lname)
    set(_labels "")
    # range/equity detection
    string(FIND "${_lname}" "range_equity" _has_req)
    string(FIND "${_lname}" "equity" _has_eq)
    string(FIND "${_lname}" "enumerate" _has_enum)
    string(FIND "${_lname}" "range_parser" _has_rp)
    if(NOT _has_req EQUAL -1 OR NOT _has_eq EQUAL -1)
        list(APPEND _labels "equity")
    endif()
    if(NOT _has_enum EQUAL -1 OR NOT _has_rp EQUAL -1)
        list(APPEND _labels "range")
    endif()
    # engine detection
    string(FIND "${_lname}" "engine" _has_engine)
    string(FIND "${_lname}" "game_engine" _has_game_engine)
    if(NOT _has_engine EQUAL -1 OR NOT _has_game_engine EQUAL -1)
        list(APPEND _labels "engine")
    endif()
    # betting detection (conservative)
    string(FIND "${_lname}" "betting" _has_betting)
    string(FIND "${_lname}" "side_pot" _has_sidepot)
    if(NOT _has_betting EQUAL -1 OR NOT _has_sidepot EQUAL -1)
        list(APPEND _labels "betting")
    endif()
    # core detection
    string(FIND "${_lname}" "card" _has_card)
    string(FIND "${_lname}" "deck" _has_deck)
    string(FIND "${_lname}" "handval" _has_handval)
    string(FIND "${_lname}" "hand_evaluation" _has_handeval)
    string(FIND "${_lname}" "mask" _has_mask)
    string(FIND "${_lname}" "eval_cache" _has_evalcache)
    string(FIND "${_lname}" "eval_context" _has_evalctx)
    string(FIND "${_lname}" "wrapper" _has_wrapper)
    string(FIND "${_lname}" "rules" _has_rules)
    if(NOT _has_card EQUAL -1 OR NOT _has_deck EQUAL -1 OR NOT _has_handval EQUAL -1 OR
       NOT _has_handeval EQUAL -1 OR NOT _has_mask EQUAL -1 OR NOT _has_evalcache EQUAL -1 OR
       NOT _has_evalctx EQUAL -1 OR NOT _has_wrapper EQUAL -1 OR NOT _has_rules EQUAL -1)
        list(APPEND _labels "core")
    endif()
    if(_labels)
        list(JOIN _labels ";" _labels_joined)
        set_tests_properties(${test_name} PROPERTIES LABELS "${_labels_joined}")
    endif()
endfunction()

# Unity-based unit tests
add_unity_test(test_card_unity test_card_unity.c)

# Unity-based integration tests
add_unity_test(test_integration_example unity_tests/test_integration_example.c)

# Unity-based benchmark tests
add_unity_test(test_benchmark_example unity_tests/test_benchmark_example.c)

# Use the corrected versions (now in original files)
add_unity_test(test_pineapple_unity unity_tests/test_pineapple_unity.c)
add_unity_test(test_pineapple8_unity unity_tests/test_pineapple8_unity.c)
add_unity_test(test_pineapple_variants_unity unity_tests/test_pineapple_variants_unity.c)
add_unity_test(test_montecarlo_convergence_unity unity_tests/test_montecarlo_convergence_unity.c)
add_unity_test(test_drawmaha_unity unity_tests/test_drawmaha_unity.c)

# New extended Unity tests for 80% coverage
add_unity_test(test_range_equity_per_matchup_budget unity_tests/test_range_equity_per_matchup_budget.c)
add_unity_test(test_enumerate_unity unity_tests/test_enumerate_unity.c)
add_unity_test(test_enumerate_comprehensive unity_tests/test_enumerate_comprehensive.c)
add_unity_test(test_enumerate_games_coverage unity_tests/test_enumerate_games_coverage.c)
add_unity_test(test_enumerate_extended unity_tests/test_enumerate_extended.c)
add_unity_test(test_range_equity_games_coverage unity_tests/test_range_equity_games_coverage.c)
add_unity_test(test_stud_draw_games unity_tests/test_stud_draw_games.c)
add_unity_test(test_lowball_eval unity_tests/test_lowball_eval.c)
add_unity_test(test_card_converter unity_tests/test_card_converter.c)
add_unity_test(test_universal_deck unity_tests/test_universal_deck.c)
add_unity_test(test_modern_combinations unity_tests/test_modern_combinations.c)
add_unity_test(test_deck_unity unity_tests/test_deck_unity.c)
add_unity_test(test_handval_unity unity_tests/test_handval_unity.c)
# This Unity test relies on MT auto-selection symbols; skip when OpenMP is disabled
if(ENABLE_OPENMP)
    add_unity_test(test_range_equity_unity unity_tests/test_range_equity_unity.c)
    set_tests_properties(test_range_equity_unity PROPERTIES TIMEOUT 180)
    set_property(TEST test_range_equity_unity APPEND PROPERTY LABELS "slow")

    # Additional comprehensive range equity tests
    add_unity_test(test_range_equity_comprehensive unity_tests/test_range_equity_comprehensive.c)
    set_tests_properties(test_range_equity_comprehensive PROPERTIES TIMEOUT 180)
    set_property(TEST test_range_equity_comprehensive APPEND PROPERTY LABELS "slow")
    
    # Edge case tests for RangeEquity.c
    add_unity_test(test_range_equity_edge_cases unity_tests/test_range_equity_edge_cases.c)
else()
    message(WARNING "Skipping test_range_equity_unity: OpenMP not enabled")
endif()
add_unity_test(test_omaha_unity unity_tests/test_omaha_unity.c)

# Comprehensive tests for distributions and game variants
add_unity_test(test_distributions_comprehensive unity_tests/test_distributions_comprehensive.c)
add_unity_test(test_game_variants_coverage unity_tests/test_game_variants_coverage.c)

# Section 7.2: Pre-flop/Flop equity tests
add_unity_test(test_preflop_equity test_preflop_equity.c)
set(_preflop_timeout 180)
if(MSVC)
    set(_preflop_timeout 300)
endif()
set_tests_properties(test_preflop_equity PROPERTIES TIMEOUT ${_preflop_timeout})

# Pre-flop table validation (standalone executable)
add_executable(validate_preflop_table validate_preflop_table.c)
target_include_directories(validate_preflop_table PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(validate_preflop_table PRIVATE
    poker_equity
    poker_core
    poker_utils
    poker_distributions
)
if(NOT APPLE)
    target_link_libraries(validate_preflop_table PRIVATE m)
endif()
set_target_properties(validate_preflop_table PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Test programs
set(TEST_SOURCES
    # Core functionality tests
    test_card.c
    test_cardconverter.c
    test_deck_operations.c
    test_hand_evaluation_high.c
    test_lowball.c
    test_handagnostichand.c
    test_cfr_metrics.c
    test_mpf_export.c
    test_mpf_perf_stats.c

    # Distribution tests
    test_hand_distribution.c
    omaha_distribution_test.c
    stud_distribution_test.c

    # Equity tests
    test_simple_equity.c
    test_range_equity_mt.c
    test_range_equity_aavs_kk.c
    test_multiway_equity.c
    test_range_equity_weighted.c
    test_low_eval.c
    test_low_qualifiers.c
    test_low_7card_best5.c
    test_flop_texture.c

    # Optimization tests
    test_eval_cache.c
    test_eval_cache_realistic.c
    test_simd_operations.c
    test_batched_montecarlo.c
    test_micro_optimizations.c
    test_aligned_tables.c

    # Game-specific tests
    test_omaha_simple.c
    test_omaha_dynamic_allocation.c
    test_PLONomenclature.c
    test_hilo_scoop_comprehensive.c
    test_scoop_count.c
    test_scoop_count_fixed.c

    # Joker tests
    test_joker_support.c
    test_joker_conversion.c
    test_joker_enum.c
    test_joker_games.c
    test_joker_hands.c
    test_joker_macro.c
    test_joker_mask_ops.c
    test_joker_multiplayer.c
    test_pokenum_joker.c
    joker_eval_test.c
    joktest1.c

    # Enumeration tests
    enumtest1.c
    enumtest2.c
    enumtest3.c
    enumtest5.c
    enumtest7.c
    test_enum_basic.c

    # Other tests
    razz.c
    shortdecktest.c
    test_short_deck_simple.c
    test_badugi.c
    test_cardmask_layout.c
    test_cache_demo.c
    simple_alignment_test.c
    test_mpf_tree_parser.c

    # Betting API tests
    test_betting_api.c

    # Modern API tests
    test_modern_api.c

    # Drawmaha tests
    test_drawmaha_basic.c
    test_drawmaha_enum.c
    test_drawmaha_ties.c

    test_mixed_game.c

    # Open Face Chinese Poker tests
    test_ofc_basic.c
    test_ofc_scoring.c
    test_ofc_comprehensive.c
    test_ofc_final.c
    test_ofc_foul_risk.c
    test_ismcts_basic.c

    # Range parser tests
    test_advanced_range_parser.c
    test_omaha_range_parser.c
    test_stud_range_parser.c
    test_range_cache.c
    test_range_utils.c
    test_range_integration.c

    # ICM Calculator tests
    test_icm_calculator.c

    # Section 7.3: Run It Twice tests
    test_run_it_twice.c

    # New Implementation tests
    test_omaha5.c
    test_omaha6.c
    test_courchevel.c
    test_rake.c
    test_icm.c
    test_a5_triple_draw.c
)
# Benchmark tests (separate category)
set(BENCHMARK_TESTS
    benchmark_mt_batched.c
    benchmark_mt_batched_simple.c
    benchmark_batched_montecarlo.c
    benchmark_micro_opt.c
    benchmark_final_micro_opt.c
    benchmark_realistic_divisions.c
    quick_benchmark.c
    benchmark_plo_preflop.c
)

# Helper function to add a test
function(add_poker_test name)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}.c")
        # If OpenMP is not available, skip heavy MT/benchmark tests that rely on it
        set(_skip_omp FALSE)
        if(NOT OpenMP_FOUND)
            string(FIND "${name}" "mt" _MT_POS)
            string(FIND "${name}" "range_equity" _RE_POS)
            string(FIND "${name}" "benchmark" _BM_POS)
            if(NOT _MT_POS EQUAL -1 OR NOT _RE_POS EQUAL -1 OR NOT _BM_POS EQUAL -1)
                set(_skip_omp TRUE)
            endif()
        endif()

        if(_skip_omp)
            message(WARNING "Skipping ${name}: OpenMP not available on this platform")
            return()
        endif()
        add_executable(${name} ${name}.c)
        # -Wno-stack-usage is GCC-specific, Clang doesn't support it
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${name} PRIVATE -Wno-stack-usage)
        endif()
        # Tests are not shipped code; do not hold them to -Werror (consistent with add_unity_test).
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${name} PRIVATE -Wno-error)
        elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_compile_options(${name} PRIVATE -Wno-error)
        endif()
        target_include_directories(${name} PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/inlines
            ${CMAKE_BINARY_DIR}/include
        )
        # Link with appropriate library
        if(TARGET poker_eval_modular)
            target_link_libraries(${name} PRIVATE poker_eval_modular)
        elseif(TARGET poker-eval)
            target_link_libraries(${name} PRIVATE poker-eval)
        else()
            target_link_libraries(${name} PRIVATE poker_equity poker-eval-range poker_core poker_distributions poker_utils poker_games)
            # Some environments might try to link non-existent aliased names if passed indirectly,
            # but here we explicitly list the target names defined in src/*/CMakeLists.txt.
            # The error 'cannot find -lpoker-eval-core' suggests that something is trying to link the *alias* or *output name*
            # but failing to find the file because it expects a library file named libpoker-eval-core.a
            # whereas the target output name might be libpoker_core.a.
            # However, CMake usually handles target names correctly.
            # If the user environment has issues, we might need to check how they are exported.
            # But the error message '/usr/bin/ld: cannot find -lpoker-eval-core' implies a linker flag -lpoker-eval-core was passed.
            # Check if any imported targets or variables are used incorrectly.
            # We are using direct target names: poker_core, poker_distributions.

            # Wait, the error is strange if we use target names.
            # Perhaps `poker-eval-static` logic elsewhere affects this?
            # Let's try to stick to the exact target names found in the src directories.
        endif()

        # Add math library
        if(NOT APPLE)
            target_link_libraries(${name} PRIVATE m)
        endif()

        # Add OpenMP if needed
        if(OpenMP_FOUND)
            string(FIND "${name}" "mt" MT_POS)
            string(FIND "${name}" "range_equity" RE_POS)
            string(FIND "${name}" "benchmark" BM_POS)

            if(NOT MT_POS EQUAL -1 OR NOT RE_POS EQUAL -1 OR NOT BM_POS EQUAL -1)
                target_link_libraries(${name} PRIVATE OpenMP::OpenMP_C)
            endif()
        endif()

        # Special compiler flags for specific tests
        if("${name}" STREQUAL "test_range_equity_mt")
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${name} PRIVATE -Wno-redundant-decls)
            endif()
        elseif("${name}" STREQUAL "test_joker_hands")
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${name} PRIVATE -Wno-conversion -Wno-inline)
            endif()
        elseif("${name}" STREQUAL "test_joker_macro")
            # -Wno-maybe-uninitialized is GCC-specific
            if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
                target_compile_options(${name} PRIVATE -Wno-maybe-uninitialized)
            endif()
        elseif("${name}" STREQUAL "joktest1")
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${name} PRIVATE -Wno-conversion)
            endif()
        elseif("${name}" STREQUAL "enumtest5")
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${name} PRIVATE -Wno-inline)
            endif()
        endif()

        # Set output directory
        set_target_properties(${name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )

        # Add as CTest
        add_test(NAME ${name} COMMAND ${name})

        # Set test properties with appropriate timeout based on test type
        # Slow equity tests need longer timeouts
        set(_timeout 30)
        string(FIND "${name}" "range_equity" _has_range_equity)
        string(FIND "${name}" "modern_api" _has_modern_api)
        string(FIND "${name}" "preflop_equity" _has_preflop)
        if(NOT _has_range_equity EQUAL -1 OR NOT _has_modern_api EQUAL -1 OR NOT _has_preflop EQUAL -1)
            set(_timeout 120)
        endif()

        set_tests_properties(${name} PROPERTIES
            TIMEOUT ${_timeout}
            ENVIRONMENT "MALLOC_CHECK_=3"
        )

        # Auto-label tests by name for easier filtering
        string(TOLOWER "${name}" _lname)
        set(_labels "")
        # range/equity detection
        string(FIND "${_lname}" "range_equity" _has_req)
        string(FIND "${_lname}" "equity" _has_eq)
        string(FIND "${_lname}" "enumerate" _has_enum)
        string(FIND "${_lname}" "range_parser" _has_rp)
        string(FIND "${_lname}" "omaha_range_parser" _has_orp)
        string(FIND "${_lname}" "advanced_range_parser" _has_arp)
        if(NOT _has_req EQUAL -1 OR NOT _has_eq EQUAL -1)
            list(APPEND _labels "equity")
        endif()
        if(NOT _has_enum EQUAL -1 OR NOT _has_rp EQUAL -1 OR NOT _has_orp EQUAL -1 OR NOT _has_arp EQUAL -1)
            list(APPEND _labels "range")
        endif()
        # engine detection
        string(FIND "${_lname}" "engine" _has_engine)
        string(FIND "${_lname}" "game_engine" _has_game_engine)
        if(NOT _has_engine EQUAL -1 OR NOT _has_game_engine EQUAL -1)
            list(APPEND _labels "engine")
        endif()
        # betting detection (conservative)
        string(FIND "${_lname}" "betting" _has_betting)
        string(FIND "${_lname}" "side_pot" _has_sidepot)
        if(NOT _has_betting EQUAL -1 OR NOT _has_sidepot EQUAL -1)
            list(APPEND _labels "betting")
        endif()
        # core detection
        string(FIND "${_lname}" "card" _has_card)
        string(FIND "${_lname}" "deck" _has_deck)
        string(FIND "${_lname}" "handval" _has_handval)
        string(FIND "${_lname}" "hand_evaluation" _has_handeval)
        string(FIND "${_lname}" "mask" _has_mask)
        string(FIND "${_lname}" "eval_cache" _has_evalcache)
        string(FIND "${_lname}" "eval_context" _has_evalctx)
        string(FIND "${_lname}" "wrapper" _has_wrapper)
        string(FIND "${_lname}" "rules" _has_rules)
        if(NOT _has_card EQUAL -1 OR NOT _has_deck EQUAL -1 OR NOT _has_handval EQUAL -1 OR
           NOT _has_handeval EQUAL -1 OR NOT _has_mask EQUAL -1 OR NOT _has_evalcache EQUAL -1 OR
           NOT _has_evalctx EQUAL -1 OR NOT _has_wrapper EQUAL -1 OR NOT _has_rules EQUAL -1)
            list(APPEND _labels "core")
        endif()
        if(_labels)
            list(JOIN _labels ";" _labels_joined)
            set_tests_properties(${name} PROPERTIES LABELS "${_labels_joined}")
        endif()
    else()
        message(STATUS "Skipping test ${name}: ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c not found")
        return()
    endif()
endfunction()

# Add all tests
foreach(test_source ${TEST_SOURCES})
    string(REPLACE ".c" "" test_name ${test_source})
    add_poker_test(${test_name})
endforeach()

# Quick smoke tests for ctest -L fast
get_property(_all_tests DIRECTORY PROPERTY TESTS)
set(_fast_tests
    test_card_unity
    test_deck_unity
    test_handval_unity
    test_card
    test_cardconverter
    test_deck_operations
    test_hand_evaluation_high
)
foreach(_fast_test IN LISTS _fast_tests)
    list(FIND _all_tests "${_fast_test}" _fast_index)
    if(NOT _fast_index EQUAL -1)
        set_property(TEST ${_fast_test} APPEND PROPERTY LABELS "fast")
    endif()
endforeach()
unset(_all_tests)
unset(_fast_tests)
unset(_fast_index)

# Section 7.1: PLO5/PLO6 tests
add_poker_test(test_plo5_basic)
add_poker_test(test_plo6_basic)
set_tests_properties(test_plo5_basic PROPERTIES LABELS "games;plo;section7")
set_tests_properties(test_plo6_basic PROPERTIES LABELS "games;plo;section7")

if(OpenMP_FOUND)
    set_tests_properties(test_range_equity_mt PROPERTIES TIMEOUT 180)
    set_property(TEST test_range_equity_mt APPEND PROPERTY LABELS "slow")
endif()

# Section 5.5: Economics tests
set_tests_properties(test_rake PROPERTIES LABELS "economics")
set_tests_properties(test_icm PROPERTIES LABELS "economics")

set_tests_properties(test_a5_triple_draw PROPERTIES LABELS "games;draw")

# Keeps the OpenCL low evaluator in step with the CPU one. The kernel is
# compiled for the host, so this needs neither a GPU nor an OpenCL runtime and
# runs everywhere.
add_poker_test(test_opencl_low_parity)
if(TARGET test_opencl_low_parity)
    target_sources(test_opencl_low_parity PRIVATE opencl_low_kernel_host.c)
    set_tests_properties(test_opencl_low_parity PROPERTIES LABELS "core;gpu")
endif()

# The kernel files duplicate their shared macro blocks behind one #ifndef each,
# so a macro added to a single copy disappears from the combined program. This
# reads the sources, no GPU or OpenCL compiler involved.
add_poker_test(test_opencl_kernel_guards)
if(TARGET test_opencl_kernel_guards)
    target_compile_definitions(test_opencl_kernel_guards PRIVATE
        OPENCL_KERNEL_DIR="${CMAKE_SOURCE_DIR}/src/gpu/opencl")
    set_tests_properties(test_opencl_kernel_guards PROPERTIES LABELS "core;gpu")
endif()

# Additional modern tests (non-Unity)
add_poker_test(test_canonical_regression)
add_poker_test(test_canonical_invariants)
add_poker_test(test_omaha_invariants)
add_poker_test(test_stud_exhaustive_multi)
add_poker_test(test_doubleflop_mc_small)
add_poker_test(test_27_triple_draw)
add_poker_test(test_triple_draw_exhaustive_multi)
add_poker_test(test_5draw_exhaustive_multi)
set_tests_properties(test_5draw_exhaustive_multi PROPERTIES TIMEOUT 300)
add_poker_test(test_5draw_mc_small)
add_poker_test(test_lowball_mc_small)
add_poker_test(test_5draw8_exhaustive_multi)
set_tests_properties(test_5draw8_exhaustive_multi PROPERTIES TIMEOUT 300)
add_poker_test(test_5drawnsq_exhaustive_multi)
set_tests_properties(test_5drawnsq_exhaustive_multi PROPERTIES TIMEOUT 300)
add_poker_test(test_5draw8_mc_small)
add_poker_test(test_5drawnsq_mc_small)

# Phase 4.1 advanced engine tests (Unity-based)
add_unity_test(test_game_engine_advanced test_game_engine_advanced.c)
# Link engine and betting libraries for test_game_engine_advanced
if(TARGET poker_engine)
    target_link_libraries(test_game_engine_advanced PRIVATE poker_engine)
endif()
if(TARGET poker_betting)
    target_link_libraries(test_game_engine_advanced PRIVATE poker_betting)
endif()
set_tests_properties(test_game_engine_advanced PROPERTIES LABELS "engine;phase4")

# Engine Hi/Lo side pots test (non-Unity)
add_poker_test(test_engine_sidepots_hilo)
set_tests_properties(test_engine_sidepots_hilo PROPERTIES LABELS "engine;betting")

# Omaha8 Hi/Lo side pots (non-Unity)
add_poker_test(test_engine_omaha8_hilo)
set_tests_properties(test_engine_omaha8_hilo PROPERTIES LABELS "engine;betting;equity")

add_poker_test(test_cfr_hilo_adapters)
set_tests_properties(test_cfr_hilo_adapters PROPERTIES LABELS "engine;cfr;hilo")
add_poker_test(test_cfr_checkpoint)
set_tests_properties(test_cfr_checkpoint PROPERTIES LABELS "engine;cfr;checkpoint")
add_poker_test(test_cfr_multiway)
set_tests_properties(test_cfr_multiway PROPERTIES LABELS "engine;cfr;multiway")
add_poker_test(test_cfr_get_actions_clamp)
set_tests_properties(test_cfr_get_actions_clamp PROPERTIES LABELS "engine;cfr;bug07")
add_poker_test(test_ismcts_poker)
set_tests_properties(test_ismcts_poker PROPERTIES LABELS "engine;ismcts;solver")

if(TARGET multiway_equity_cli)
    add_test(NAME multiway_equity_cli_basic
        COMMAND $<TARGET_FILE:multiway_equity_cli>
            --range AA
            --range KK
            --range QQ
            --invested 120,80,60
            --method mc
            --iterations 5000)
    set_tests_properties(multiway_equity_cli_basic PROPERTIES
        LABELS "equity;cli"
        TIMEOUT 180
        PASS_REGULAR_EXPRESSION "#1")
    set_property(TEST multiway_equity_cli_basic APPEND PROPERTY LABELS "fast")
endif()

if(TARGET bench_cfr_holdem_multi)
    add_test(NAME bench_cfr_holdem_multi_smoke
        COMMAND $<TARGET_FILE:bench_cfr_holdem_multi>
            --players 3
            --iters 1
            --street river
            --board AhKs7d2c5s
            --bet-sizes 1.5
            --progress 0)
    set_tests_properties(bench_cfr_holdem_multi_smoke PROPERTIES
        LABELS "engine;cfr;multiway"
        TIMEOUT 120)
    set_property(TEST bench_cfr_holdem_multi_smoke APPEND PROPERTY LABELS "fast")

    add_test(NAME bench_cfr_holdem_multi_plo4_smoke
        COMMAND $<TARGET_FILE:bench_cfr_holdem_multi>
            --game plo4
            --players 3
            --iters 1
            --street river
            --board AhKs7d2c5s
            --seed 42
            --progress 0)
    set_tests_properties(bench_cfr_holdem_multi_plo4_smoke PROPERTIES
        LABELS "engine;cfr;multiway"
        TIMEOUT 120)
    set_property(TEST bench_cfr_holdem_multi_plo4_smoke APPEND PROPERTY LABELS "fast")
endif()

# Copy regression corpus into build tree for runtime
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/canonical_regression.txt
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data)

# Add benchmark tests (not added to CTest by default)
foreach(bench_source ${BENCH_TESTS})
    string(REPLACE ".c" "" bench_name ${bench_source})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${bench_source}")
        add_executable(${bench_name} ${bench_source})

        # Add include directories
        target_include_directories(${bench_name} PRIVATE
            ${CMAKE_BINARY_DIR}/include
            ${CMAKE_SOURCE_DIR}/include)

        if(TARGET poker_eval_modular)
            target_link_libraries(${bench_name} PRIVATE poker_eval_modular)
        elseif(TARGET poker-eval)
            target_link_libraries(${bench_name} PRIVATE poker-eval)
        else()
            target_link_libraries(${bench_name} PRIVATE poker-eval-static)
        endif()

        target_link_libraries(${bench_name} PRIVATE m)

        if(OpenMP_FOUND)
            target_link_libraries(${bench_name} PRIVATE OpenMP::OpenMP_C)
        endif()

        set_target_properties(${bench_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/benchmarks
        )
    endif()
endforeach()

# Standalone PLO preflop benchmark with OpenMP
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_plo_preflop.c")
    add_executable(benchmark_plo_preflop benchmark_plo_preflop.c)

    target_include_directories(benchmark_plo_preflop PRIVATE
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/include)

    if(TARGET poker_eval_modular)
        target_link_libraries(benchmark_plo_preflop PRIVATE poker_eval_modular)
    elseif(TARGET poker-eval)
        target_link_libraries(benchmark_plo_preflop PRIVATE poker-eval)
    else()
        target_link_libraries(benchmark_plo_preflop PRIVATE poker-eval-static)
    endif()

    target_link_libraries(benchmark_plo_preflop PRIVATE m)

    if(OpenMP_FOUND)
        target_link_libraries(benchmark_plo_preflop PRIVATE OpenMP::OpenMP_C)
    endif()

    set_target_properties(benchmark_plo_preflop PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/benchmarks)
endif()

# GPU tests (if GPU support is enabled)
if(BUILD_GPU AND TARGET poker_gpu)
    option(POKER_FORCE_GPU_TESTS "Force GPU tests even if hardware detection fails" OFF)
    set(GPU_HARDWARE_AVAILABLE FALSE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/DetectGPU.cmake")
        include(${CMAKE_SOURCE_DIR}/cmake/DetectGPU.cmake)
        detect_gpu_hardware(GPU_HARDWARE_AVAILABLE)
    endif()
    if(POKER_FORCE_GPU_TESTS)
        set(GPU_HARDWARE_AVAILABLE TRUE)
    endif()
    if(GPU_HARDWARE_AVAILABLE)
        message(STATUS "GPU hardware detected - enabling GPU tests")
    else()
        message(STATUS "No GPU hardware detected - GPU tests will be skipped")
    endif()

    set(GPU_SKIP_RETURN_CODE 77)

    add_poker_test(test_gpu_acceleration)
    target_link_libraries(test_gpu_acceleration PRIVATE poker_gpu)
    target_compile_definitions(test_gpu_acceleration PRIVATE HAVE_CUDA)

    set_tests_properties(test_gpu_acceleration PROPERTIES
        SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
    set_property(TEST test_gpu_acceleration APPEND PROPERTY LABELS "gpu;optional")
    if(NOT GPU_HARDWARE_AVAILABLE)
        set_tests_properties(test_gpu_acceleration PROPERTIES DISABLED ON)
    endif()

    add_poker_test(test_gpu_cfr_converter)
    target_link_libraries(test_gpu_cfr_converter PRIVATE poker_gpu)
    set_tests_properties(test_gpu_cfr_converter PROPERTIES
        LABELS "gpu;cfr"
        SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
    set_property(TEST test_gpu_cfr_converter APPEND PROPERTY LABELS "optional")
    if(NOT GPU_HARDWARE_AVAILABLE)
        set_tests_properties(test_gpu_cfr_converter PROPERTIES DISABLED ON)
    endif()

    # GPU multi-game tests (requires CUDA, uses gpu_eval_init_game)
    if(TARGET poker_cuda)
        add_poker_test(test_gpu_omaha)
        target_link_libraries(test_gpu_omaha PRIVATE poker_gpu)
        target_compile_definitions(test_gpu_omaha PRIVATE HAVE_CUDA)
        set_tests_properties(test_gpu_omaha PROPERTIES
            LABELS "gpu;multi-game;omaha"
            SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
        set_property(TEST test_gpu_omaha APPEND PROPERTY LABELS "optional")
        if(NOT GPU_HARDWARE_AVAILABLE)
            set_tests_properties(test_gpu_omaha PROPERTIES DISABLED ON)
        endif()
        add_custom_target(run_gpu_multi_game_validation
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/run_gpu_multigame_validation.sh
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_dependencies(run_gpu_multi_game_validation test_gpu_omaha)

        # Equity comparison framework (CPU/SIMD/GPU)
        add_poker_test(test_equity_comparison_framework)
        target_link_libraries(test_equity_comparison_framework PRIVATE poker_gpu)
        target_compile_definitions(test_equity_comparison_framework PRIVATE HAVE_CUDA)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(test_equity_comparison_framework PRIVATE -Wno-switch-enum)
        endif()
        set_tests_properties(test_equity_comparison_framework PROPERTIES
            LABELS "gpu;equity;comparison"
            SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
        set_property(TEST test_equity_comparison_framework APPEND PROPERTY LABELS "optional")
        if(NOT GPU_HARDWARE_AVAILABLE)
            set_tests_properties(test_equity_comparison_framework PROPERTIES DISABLED ON)
        endif()
    else()
        message(STATUS "CUDA target not built - skipping test_gpu_omaha and test_equity_comparison_framework (require CUDA-specific functions)")
    endif()

    # OpenCL multi-game validation test (CPU vs OpenCL)
    if(TARGET poker_opencl)
        add_poker_test(test_opencl_vs_cpu)
        target_link_libraries(test_opencl_vs_cpu PRIVATE poker_gpu)
        target_compile_definitions(test_opencl_vs_cpu PRIVATE HAVE_OPENCL)
        target_include_directories(test_opencl_vs_cpu PRIVATE ${CMAKE_SOURCE_DIR}/src/gpu)
        set_tests_properties(test_opencl_vs_cpu PROPERTIES
            LABELS "gpu;opencl;validation"
            TIMEOUT 180
            SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
        set_property(TEST test_opencl_vs_cpu APPEND PROPERTY LABELS "optional")
        if(NOT GPU_HARDWARE_AVAILABLE)
            set_tests_properties(test_opencl_vs_cpu PROPERTIES DISABLED ON)
        endif()
        message(STATUS "OpenCL backend available - adding test_opencl_vs_cpu validation test")
    endif()

    # GPU multi-game batch evaluation tests (Stud/Razz/HiLo/Omaha8)
    add_unity_test(test_gpu_multigame test_gpu_multigame.c)
    target_link_libraries(test_gpu_multigame PRIVATE poker_gpu)
    target_include_directories(test_gpu_multigame PRIVATE ${CMAKE_SOURCE_DIR}/src/gpu)
    set_tests_properties(test_gpu_multigame PROPERTIES
        LABELS "gpu;multigame;phase5"
        TIMEOUT 120
        SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
    set_property(TEST test_gpu_multigame APPEND PROPERTY LABELS "optional")
    if(NOT GPU_HARDWARE_AVAILABLE)
        set_tests_properties(test_gpu_multigame PROPERTIES DISABLED ON)
    endif()

    # GPU-CFR OpenCL backend test
    if(TARGET poker_gpu_cfr_opencl)
        add_executable(test_gpu_cfr_opencl test_gpu_cfr_opencl.c)
        target_include_directories(test_gpu_cfr_opencl PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/unity/src
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src/gpu
        )
        target_link_libraries(test_gpu_cfr_opencl PRIVATE
            unity
            poker_gpu_cfr_opencl
            poker_gpu_cfr_base
            poker_core
        )
        target_compile_definitions(test_gpu_cfr_opencl PRIVATE HAVE_OPENCL)
        add_test(NAME test_gpu_cfr_opencl COMMAND test_gpu_cfr_opencl)
        set_tests_properties(test_gpu_cfr_opencl PROPERTIES
            LABELS "gpu;opencl;cfr"
            TIMEOUT 120
            SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
        set_property(TEST test_gpu_cfr_opencl APPEND PROPERTY LABELS "optional")
        if(NOT GPU_HARDWARE_AVAILABLE)
            set_tests_properties(test_gpu_cfr_opencl PROPERTIES DISABLED ON)
        endif()
        message(STATUS "GPU-CFR OpenCL backend available - adding test_gpu_cfr_opencl")
    endif()
    add_executable(test_ofc_gpu_basic test_ofc_gpu_basic.c)
    # Disable stack-usage warnings for GPU tests (large test arrays) - GCC only
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set_source_files_properties(test_ofc_gpu_basic.c PROPERTIES
            COMPILE_FLAGS "-Wno-stack-usage")
    endif()
    target_link_libraries(test_ofc_gpu_basic
        poker_eval_modular
        poker_gpu
        poker_ofc_gpu
        ${CMAKE_THREAD_LIBS_INIT}
    )
    add_test(NAME OFC_GPU_Basic COMMAND test_ofc_gpu_basic)
    set_tests_properties(OFC_GPU_Basic PROPERTIES
        SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
    set_property(TEST OFC_GPU_Basic APPEND PROPERTY LABELS "gpu;ofc;optional")
    if(NOT GPU_HARDWARE_AVAILABLE)
        set_tests_properties(OFC_GPU_Basic PROPERTIES DISABLED ON)
    endif()

    # Comprehensive GPU test suite
    add_executable(test_ofc_gpu_comprehensive test_ofc_gpu_comprehensive.c)
    # Disable stack-usage and stringop warnings for GPU tests (large test arrays) - GCC only
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set_source_files_properties(test_ofc_gpu_comprehensive.c PROPERTIES
            COMPILE_FLAGS "-Wno-stack-usage -Wno-stringop-truncation")
    endif()
    target_link_libraries(test_ofc_gpu_comprehensive
        poker_eval_modular
        poker_gpu
        poker_ofc_gpu
        ${CMAKE_THREAD_LIBS_INIT}
    )
    if(NOT APPLE)
        target_link_libraries(test_ofc_gpu_comprehensive m)
    endif()
    add_test(NAME OFC_GPU_Comprehensive COMMAND test_ofc_gpu_comprehensive)
    set_tests_properties(OFC_GPU_Comprehensive PROPERTIES
        LABELS "gpu;ofc;comprehensive"
        TIMEOUT 300
        SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
    set_property(TEST OFC_GPU_Comprehensive APPEND PROPERTY LABELS "optional")
    if(NOT GPU_HARDWARE_AVAILABLE)
        set_tests_properties(OFC_GPU_Comprehensive PROPERTIES DISABLED ON)
    endif()
endif()

# Special test executables that might need different handling
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/poker_wrapper.c")
    add_executable(test_poker_wrapper poker_wrapper.c)

    # Add include directories
    target_include_directories(test_poker_wrapper PRIVATE
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/include)

    if(TARGET poker_eval_modular)
        target_link_libraries(test_poker_wrapper PRIVATE poker_eval_modular)
    elseif(TARGET poker-eval)
        target_link_libraries(test_poker_wrapper PRIVATE poker-eval)
    else()
        target_link_libraries(test_poker_wrapper PRIVATE poker-eval-static)
    endif()
    add_test(NAME test_poker_wrapper COMMAND test_poker_wrapper)
endif()

# Bounded *_toString_n variants
add_executable(test_handval_tostring_bounds test_handval_tostring_bounds.c)
target_include_directories(test_handval_tostring_bounds PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/include)
if(TARGET poker_eval_modular)
    target_link_libraries(test_handval_tostring_bounds PRIVATE poker_eval_modular)
else()
    target_link_libraries(test_handval_tostring_bounds PRIVATE poker_games poker_core)
endif()
add_test(NAME test_handval_tostring_bounds COMMAND test_handval_tostring_bounds)

# Custom test targets
add_custom_target(test-quick
    COMMAND ${CMAKE_CTEST_COMMAND} -R "test_card|test_deck|test_hand_evaluation" --output-on-failure
    COMMENT "Running quick tests..."
)

add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests..."
)

add_custom_target(test-verbose
    COMMAND ${CMAKE_CTEST_COMMAND} -V
    COMMENT "Running all tests with verbose output..."
)

# Memory check target (if valgrind is available)
find_program(VALGRIND_EXECUTABLE valgrind)
if(VALGRIND_EXECUTABLE)
    add_custom_target(test-memcheck
        COMMAND ${CMAKE_CTEST_COMMAND} -T memcheck
        COMMENT "Running tests with memory checking..."
    )
endif()

# Test scripts
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/run.in
    ${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh
    @ONLY
)

# Make test scripts executable
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.19")
    file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
else()
    execute_process(COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh ERROR_QUIET)
endif()

# Copy any test data files
file(GLOB TEST_DATA_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.txt" "${CMAKE_CURRENT_SOURCE_DIR}/*.dat")
foreach(data_file ${TEST_DATA_FILES})
    configure_file(${data_file} ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
endforeach()

# Add deterministic benchmarking test
add_executable(test_deterministic_benchmarks
    test_deterministic_benchmarks.c
)
target_include_directories(test_deterministic_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    unity/src
)
target_link_libraries(test_deterministic_benchmarks PRIVATE unity)
target_compile_definitions(test_deterministic_benchmarks PRIVATE UNITY_INCLUDE_DOUBLE)
if(POKER_EVAL_WITH_OPENMP)
    target_link_libraries(test_deterministic_benchmarks PRIVATE OpenMP::OpenMP_C)
endif()
if(UNIX)
    target_link_libraries(test_deterministic_benchmarks PRIVATE m)
endif()

add_test(NAME test_deterministic_benchmarks COMMAND test_deterministic_benchmarks)

# Add Phase 3 integration test
add_executable(test_phase3_integration
    test_phase3_integration.c
)
target_include_directories(test_phase3_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    unity/src
)
target_link_libraries(test_phase3_integration PRIVATE unity)
target_compile_definitions(test_phase3_integration PRIVATE UNITY_INCLUDE_DOUBLE)
if(TARGET poker_eval_modular)
    target_link_libraries(test_phase3_integration PRIVATE poker_eval_modular)
elseif(TARGET poker-eval)
    target_link_libraries(test_phase3_integration PRIVATE poker-eval)
else()
    target_link_libraries(test_phase3_integration PRIVATE poker-eval-static)
endif()
if(UNIX)
    target_link_libraries(test_phase3_integration PRIVATE m)
endif()

add_test(NAME test_phase3_integration COMMAND test_phase3_integration)


# Add performance regression test
add_executable(test_performance_regression
    test_performance_regression.c
)
target_include_directories(test_performance_regression PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    unity/src
)
target_link_libraries(test_performance_regression PRIVATE unity)
target_compile_definitions(test_performance_regression PRIVATE UNITY_INCLUDE_DOUBLE)
if(TARGET poker_eval_modular)
    target_link_libraries(test_performance_regression PRIVATE poker_eval_modular)
elseif(TARGET poker-eval)
    target_link_libraries(test_performance_regression PRIVATE poker-eval)
else()
    target_link_libraries(test_performance_regression PRIVATE poker-eval-static)
endif()
if(UNIX)
    target_link_libraries(test_performance_regression PRIVATE m)
endif()

add_test(NAME test_performance_regression COMMAND test_performance_regression)

# Add modern/legacy compatibility test
add_executable(test_modern_legacy_compatibility
    test_modern_legacy_compatibility.c
)
target_include_directories(test_modern_legacy_compatibility PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    unity/src
)
target_link_libraries(test_modern_legacy_compatibility PRIVATE unity)
target_compile_definitions(test_modern_legacy_compatibility PRIVATE UNITY_INCLUDE_DOUBLE)
if(TARGET poker_eval_modular)
    target_link_libraries(test_modern_legacy_compatibility PRIVATE poker_eval_modular)
elseif(TARGET poker-eval)
    target_link_libraries(test_modern_legacy_compatibility PRIVATE poker-eval)
else()
    target_link_libraries(test_modern_legacy_compatibility PRIVATE poker-eval-static)
endif()
if(UNIX)
    target_link_libraries(test_modern_legacy_compatibility PRIVATE m)
endif()

add_test(NAME test_modern_legacy_compatibility COMMAND test_modern_legacy_compatibility)

# Add modern equity API test (only meaningful when experimental API is enabled)
if(POKER_EVAL_EXPERIMENTAL)
    add_executable(test_modern_equity
        test_modern_equity.c
    )
    target_include_directories(test_modern_equity PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    if(TARGET poker_eval_modular)
        target_link_libraries(test_modern_equity PRIVATE poker_eval_modular)
    elseif(TARGET poker-eval)
        target_link_libraries(test_modern_equity PRIVATE poker-eval)
    else()
        target_link_libraries(test_modern_equity PRIVATE poker-eval-static)
    endif()
    if(UNIX)
        target_link_libraries(test_modern_equity PRIVATE m)
    endif()

    add_test(NAME test_modern_equity COMMAND test_modern_equity)
endif()

# Create a summary of test results
add_custom_target(test-summary
    COMMAND ${CMAKE_COMMAND} -E echo "Running test summary..."
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure | grep -E "Test|tests passed"
    COMMENT "Generating test summary..."
)
add_poker_test(test_canonicalize_extensions)
set_tests_properties(test_canonicalize_extensions PROPERTIES LABELS "core;canonicalize")

add_poker_test(test_plo6_integration)
add_poker_test(test_plo5_integration)
add_unity_test(test_flop_equity_full test_flop_equity_full.c)
add_unity_test(test_flop_equity test_flop_equity.c)

# Section 5.5: Economics tests
add_unity_test(test_tournament test_tournament.c)
set_tests_properties(test_tournament PROPERTIES LABELS "economics;tournament")
add_unity_test(test_rake_extended test_rake_extended.c)
set_tests_properties(test_rake_extended PROPERTIES LABELS "economics;rake")

# Phase 4 Range Equity Tests
add_poker_test(test_range_equity)
if(OpenMP_FOUND)
    set_tests_properties(test_range_equity PROPERTIES LABELS "equity;phase4" TIMEOUT 180)
endif()

# Phase 1: New Range Parser Tests
add_poker_test(test_pe_range_holdem)
set_tests_properties(test_pe_range_holdem PROPERTIES LABELS "range;phase1")

# Phase 2: Range Ops Tests
add_poker_test(test_pe_range_ops)
set_tests_properties(test_pe_range_ops PROPERTIES LABELS "range;phase2")

# Phase 3: Equity Engine Tests
add_poker_test(test_pe_equity_engine)
set_tests_properties(test_pe_equity_engine PROPERTIES LABELS "equity;phase3")

# Phase 4: Stud Range Parser Tests
set_tests_properties(test_stud_range_parser PROPERTIES LABELS "range;stud;phase4")

# Phase 4: Integration Tests (range + equity cross-game)
set_tests_properties(test_range_integration PROPERTIES LABELS "range;equity;integration;phase4" TIMEOUT 180)

# Phase 5: Code Coverage Tests
add_unity_test(test_enumerate_multigame test_enumerate_multigame.c)
set_tests_properties(test_enumerate_multigame PROPERTIES
    LABELS "equity;coverage;phase5" TIMEOUT 120)

add_unity_test(test_enumerate_dispatch test_enumerate_dispatch.c)
set_tests_properties(test_enumerate_dispatch PROPERTIES
    LABELS "equity;coverage;phase5" TIMEOUT 30)

add_unity_test(test_range_equity_multigame test_range_equity_multigame.c)
set_tests_properties(test_range_equity_multigame PROPERTIES
    LABELS "equity;coverage;phase5" TIMEOUT 180)

add_unity_test(test_pe_equity_coverage test_pe_equity_coverage.c)
set_tests_properties(test_pe_equity_coverage PROPERTIES
    LABELS "equity;coverage;phase5" TIMEOUT 120)

add_unity_test(test_distributions_coverage test_distributions_coverage.c)
set_tests_properties(test_distributions_coverage PROPERTIES
    LABELS "range;coverage;phase5" TIMEOUT 60)

add_unity_test(test_pokenum_integration test_pokenum_integration.c)
set_tests_properties(test_pokenum_integration PROPERTIES
    LABELS "equity;integration;coverage;phase5" TIMEOUT 120)

add_unity_test(test_enumerate_edge_cases test_enumerate_edge_cases.c)
set_tests_properties(test_enumerate_edge_cases PROPERTIES
    LABELS "enumerate;coverage;phase5" TIMEOUT 120)

add_unity_test(test_range_equity_coverage test_range_equity_coverage.c)
set_tests_properties(test_range_equity_coverage PROPERTIES
    LABELS "equity;coverage;phase5" TIMEOUT 120)

# Ensure tests have a writable temp dir to avoid OpenMP /tmp warnings in sandboxed runs.
get_property(_pe_all_tests DIRECTORY PROPERTY TESTS)
foreach(_pe_test ${_pe_all_tests})
    set_property(TEST ${_pe_test} APPEND PROPERTY ENVIRONMENT "TMPDIR=${PE_TEST_TMPDIR}")
    set_property(TEST ${_pe_test} APPEND PROPERTY ENVIRONMENT "OMP_TMPDIR=${PE_TEST_TMPDIR}")
    set_property(TEST ${_pe_test} APPEND PROPERTY ENVIRONMENT "KMP_TMPDIR=${PE_TEST_TMPDIR}")
endforeach()
