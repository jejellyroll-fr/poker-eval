# Ensure OpenMP flag is honored for test gating
if(NOT ENABLE_OPENMP)
    set(OpenMP_FOUND FALSE)
endif()

# Keep assert() live in test binaries whatever the build type.
#
# Release configures -DNDEBUG (both from CMAKE_C_FLAGS_RELEASE and from the
# top-level add_compile_options), which expands assert() to nothing. The
# "Release Build & Tests" CI job runs the whole suite in that configuration, so
# every assertion-based test silently degraded into a no-op that always passed
# regardless of what the code did (see issue #211).
#
# This is a directory-level option, so it applies to every target defined in
# tests/ and lands after the inherited -DNDEBUG on the command line, where the
# last -D/-U wins. The library itself is untouched and still ships with
# assertions disabled in Release.
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
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
    if("${test_name}" STREQUAL "test_montecarlo_convergence_unity")
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 600)
    elseif("${test_name}" STREQUAL "test_mpf_stack_index")
        # Includes a 6-player asym/sym memory-ratio benchmark that is slow under ASan/UBSan.
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 300)
    else()
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
    endif()

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
add_unity_test(test_pineapple_batched_unity unity_tests/test_pineapple_batched_unity.c)
add_unity_test(test_montecarlo_convergence_unity unity_tests/test_montecarlo_convergence_unity.c)
add_unity_test(test_drawmaha_unity unity_tests/test_drawmaha_unity.c)
add_unity_test(test_draw_optimizer test_draw_optimizer.c)
add_unity_test(test_draw_optimizer_fn test_draw_optimizer_fn.c)
add_unity_test(test_video_poker_strategy test_video_poker_strategy.c)

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
add_unity_test(test_generalized_deck unity_tests/test_generalized_deck.c)
add_unity_test(test_modern_combinations unity_tests/test_modern_combinations.c)
add_unity_test(test_deck_unity unity_tests/test_deck_unity.c)
add_unity_test(test_handval_unity unity_tests/test_handval_unity.c)
# ISSUE-04 (#160): configurable hand ranking rules
add_unity_test(test_configurable_ranking test_configurable_ranking.c)
# ISSUE-02 (#158): automated hand distribution & combinatorial probability engine
add_unity_test(test_hand_distribution test_hand_distribution.c)
add_unity_test(test_transition_distribution test_transition_distribution.c)
# ISSUE-05 (#161): generalized wildcard policy
add_unity_test(test_wildcard_policy test_wildcard_policy.c)
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
    omaha_distribution_test.c
    stud_distribution_test.c

    # Equity tests
    test_simple_equity.c
    test_range_equity_mt.c
    test_range_equity_seed_determinism.c
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
    test_spot_filter.c

    # FEAT-10 (#146): sparse stack-config indexer + compact reach map
    test_mpf_stack_index.c

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
    test_paytable_ev.c
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

        # The legacy Razz regression is a backend correctness check.
        if("${name}" STREQUAL "razz")
            set(_timeout 120)
        elseif("${name}" STREQUAL "test_montecarlo_convergence_unity" OR
               "${name}" STREQUAL "test_omaha_range_parser")
            set(_timeout 600)
        endif()

        set(_test_environment "MALLOC_CHECK_=3")

        set_tests_properties(${name} PROPERTIES
            TIMEOUT ${_timeout}
            ENVIRONMENT "${_test_environment}"
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
    set_tests_properties(test_range_equity_seed_determinism PROPERTIES TIMEOUT 180)
    set_property(TEST test_range_equity_seed_determinism APPEND PROPERTY LABELS "slow")
endif()

# Section 5.5: Economics tests
set_tests_properties(test_rake PROPERTIES LABELS "economics")
set_tests_properties(test_icm PROPERTIES LABELS "economics")
set_tests_properties(test_paytable_ev PROPERTIES LABELS "economics;paytable")
set_tests_properties(test_draw_optimizer_fn PROPERTIES LABELS "economics;paytable")
set_tests_properties(test_video_poker_strategy PROPERTIES LABELS "economics;paytable;fast")

# ISSUE-07 step 3 (#214): the full optimal-strategy derivation takes minutes
# with OpenMP and hours without it, so it is only registered when OpenMP is
# available and carries the "slow" label.
if(OpenMP_FOUND)
    add_poker_test(test_video_poker_derivation)
    set_tests_properties(test_video_poker_derivation PROPERTIES TIMEOUT 3600)
    set_tests_properties(test_video_poker_derivation PROPERTIES
        LABELS "economics;paytable;slow")
else()
    message(WARNING "Skipping test_video_poker_derivation: OpenMP not enabled")
endif()

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
add_poker_test(test_cfr_storage_grow)
set_tests_properties(test_cfr_storage_grow PROPERTIES LABELS "engine;cfr;storage")
add_poker_test(test_cfr_storage_entry_grow)
set_tests_properties(test_cfr_storage_entry_grow PROPERTIES LABELS "engine;cfr;storage")
add_poker_test(test_cfr_selective_memory)
set_tests_properties(test_cfr_selective_memory PROPERTIES LABELS "engine;cfr;storage")
add_poker_test(test_cfr_state_release)
set_tests_properties(test_cfr_state_release PROPERTIES LABELS "engine;cfr;leak")
add_poker_test(test_cfr_checkpoint_cap)
set_tests_properties(test_cfr_checkpoint_cap PROPERTIES LABELS "engine;cfr;checkpoint;bug04")
add_poker_test(test_cfr_checkpoint_header)
set_tests_properties(test_cfr_checkpoint_header PROPERTIES LABELS "engine;cfr;checkpoint;bug05")
add_poker_test(test_cfr_depth_guard)
set_tests_properties(test_cfr_depth_guard PROPERTIES LABELS "engine;cfr;bug08")
add_poker_test(test_cfr_multiway)
add_poker_test(test_cfr_icm_utility)
set_tests_properties(test_cfr_icm_utility PROPERTIES LABELS "engine;cfr;icm;feat17")
add_poker_test(test_board_canonical)
set_tests_properties(test_board_canonical PROPERTIES LABELS "engine;cfr;canonicalize")
add_poker_test(test_pe_isomorphism)
set_tests_properties(test_pe_isomorphism PROPERTIES LABELS "engine;cfr;canonicalize;iso01")
add_poker_test(test_hand_clustering)
set_tests_properties(test_hand_clustering PROPERTIES LABELS "engine;cfr;cluster" TIMEOUT 180)
add_poker_test(test_board_texture)
set_tests_properties(test_board_texture PROPERTIES LABELS "engine;cfr;texture;feat13" TIMEOUT 120)
add_poker_test(test_strength_bucketing)
set_tests_properties(test_strength_bucketing PROPERTIES LABELS "engine;cfr;bucketing;feat13" TIMEOUT 180)
add_poker_test(test_draw_abstraction)
set_tests_properties(test_draw_abstraction PROPERTIES LABELS "engine;cfr;draw;abstraction;feat15")
add_poker_test(test_cfr_chance_nodes)
set_tests_properties(test_cfr_chance_nodes PROPERTIES LABELS "engine;cfr;chance" TIMEOUT 180)
add_poker_test(test_cfr_bunching)
set_tests_properties(test_cfr_bunching PROPERTIES LABELS "engine;cfr;bunching;feat14" TIMEOUT 180)
add_poker_test(test_cfr_compact_storage)
set_tests_properties(test_cfr_compact_storage PROPERTIES LABELS "engine;cfr;compact")
set_tests_properties(test_cfr_multiway PROPERTIES LABELS "engine;cfr;multiway")
add_poker_test(test_cfr_strategy_mode)
add_poker_test(test_cfr_telemetry)
add_poker_test(test_cfr_dcfr_discount)
add_poker_test(test_cfr_legacy_plan)
set_tests_properties(test_cfr_legacy_plan PROPERTIES LABELS "engine;cfr;api02")
set_tests_properties(test_cfr_dcfr_discount PROPERTIES LABELS "engine;cfr;dcfr;ext07")
set_tests_properties(test_cfr_telemetry PROPERTIES LABELS "engine;cfr;telemetry;ext03")
set_tests_properties(test_cfr_strategy_mode PROPERTIES LABELS "engine;cfr;strategy-mode;ext01")
add_poker_test(test_cfr_get_actions_clamp)
set_tests_properties(test_cfr_get_actions_clamp PROPERTIES LABELS "engine;cfr;bug07")
add_poker_test(test_cfr_node_lock)
set_tests_properties(test_cfr_node_lock PROPERTIES LABELS "engine;cfr;lock")
add_poker_test(test_cfr_opponent_model)
set_tests_properties(test_cfr_opponent_model PROPERTIES LABELS "engine;cfr;lock;feat12")
add_poker_test(test_cfr_periodic_relock)
set_tests_properties(test_cfr_periodic_relock PROPERTIES LABELS "engine;cfr;lock;feat11")
add_poker_test(test_cfr_periodic_relock_multiway)
add_poker_test(test_cfr_relock_ev_loss)
set_tests_properties(test_cfr_relock_ev_loss PROPERTIES LABELS "engine;cfr;lock;feat11;ext08")
set_tests_properties(test_cfr_periodic_relock_multiway PROPERTIES LABELS "engine;cfr;lock;feat11;multiway")
add_poker_test(test_cfr_subgame_resolve)
set_tests_properties(test_cfr_subgame_resolve PROPERTIES LABELS "engine;cfr;resolve;feat05" TIMEOUT 180)

# ISSUE-14 (#170): generic multi-utility guardrail of terminal payoffs through
# pe_utility_fn / pe_cfr_set_utility_function (non-linear, non-zero-sum).
add_poker_test(test_cfr_issue170_utility)
set_tests_properties(test_cfr_issue170_utility PROPERTIES LABELS "engine;cfr;utility;issue170" TIMEOUT 120)
add_poker_test(test_mpf_sidepots)
set_tests_properties(test_mpf_sidepots PROPERTIES LABELS "engine;cfr;multiway;sidepot")
add_poker_test(test_mpf_rake)
set_tests_properties(test_mpf_rake PROPERTIES LABELS "engine;cfr;rake")
add_poker_test(test_mpf_tree)
set_tests_properties(test_mpf_tree PROPERTIES LABELS "engine;cfr;multiway;feat06" TIMEOUT 180)
add_poker_test(test_bet_sizing)
set_tests_properties(test_bet_sizing PROPERTIES LABELS "engine;cfr;bet-sizing;issue166")
# Architecture v3 solver (docs/project/SOLVER_ARCHITECTURE_V3.md). These tests
# exercise the hexagon's domain and its ports; they are labelled "solver-v3" so
# the whole layer can be run on its own as it grows.
add_poker_test(test_pe_capabilities)
set_tests_properties(test_pe_capabilities PROPERTIES LABELS "solver;solver-v3;ctr02")
add_poker_test(test_pe_actions)
set_tests_properties(test_pe_actions PROPERTIES LABELS "solver;solver-v3;rules;ugs01")
add_poker_test(test_pe_betting_state)
set_tests_properties(test_pe_betting_state PROPERTIES LABELS "solver;solver-v3;rules;ugs02")
add_poker_test(test_pe_betting_vector)
set_tests_properties(test_pe_betting_vector PROPERTIES LABELS "solver;solver-v3;rules;vector;ugs03")
add_poker_test(test_pe_holdem_river)
set_tests_properties(test_pe_holdem_river PROPERTIES LABELS "solver;solver-v3;holdem;vector;ugs04")
add_poker_test(test_pe_holdem_chance)
set_tests_properties(test_pe_holdem_chance PROPERTIES LABELS "solver;solver-v3;holdem;chance;ugs06")
add_poker_test(test_pe_holdem_deals)
set_tests_properties(test_pe_holdem_deals PROPERTIES LABELS "solver;solver-v3;holdem;ranges;card-removal;ugs04b")
add_poker_test(test_pe_holdem_streets)
set_tests_properties(test_pe_holdem_streets PROPERTIES LABELS "solver;solver-v3;holdem;chance;streets;ugs07")
add_poker_test(test_pe_omaha_deals)
set_tests_properties(test_pe_omaha_deals PROPERTIES LABELS "solver;solver-v3;omaha;plo5;plo6;card-removal;ugs08")
add_poker_test(test_pe_pots)
set_tests_properties(test_pe_pots PROPERTIES LABELS "solver;solver-v3;multiway;sidepots;ugs05")
add_poker_test(test_pe_solver_config)
set_tests_properties(test_pe_solver_config PROPERTIES LABELS "solver;solver-v3;ctr03")
add_poker_test(test_pe_ports)
set_tests_properties(test_pe_ports PROPERTIES LABELS "solver;solver-v3;ctr04")
add_poker_test(test_pe_rng)
set_tests_properties(test_pe_rng PROPERTIES LABELS "solver;solver-v3;ctr05")
add_poker_test(test_pe_registry)
set_tests_properties(test_pe_registry PROPERTIES LABELS "solver;solver-v3;ctr06")
add_poker_test(test_pe_regret_legacy_exp)
set_tests_properties(test_pe_regret_legacy_exp PROPERTIES LABELS "solver;solver-v3;alg05")
add_poker_test(test_pe_batch)
set_tests_properties(test_pe_batch PROPERTIES LABELS "solver;solver-v3;par01")
add_poker_test(test_pe_compute_cpu_par)
set_tests_properties(test_pe_compute_cpu_par PROPERTIES LABELS "solver;solver-v3;par03")
add_poker_test(test_compute_cpu_ref)
set_tests_properties(test_compute_cpu_ref PROPERTIES LABELS "solver;solver-v3;gpu01")
add_poker_test(test_pe_compute_gpu_updates)
target_include_directories(test_pe_compute_gpu_updates PRIVATE
    ${CMAKE_SOURCE_DIR}/src/solver/adapters)
set_tests_properties(test_pe_compute_gpu_updates PROPERTIES LABELS "solver;solver-v3;gpu06")
add_poker_test(test_pe_compute_layout)
set_tests_properties(test_pe_compute_layout PROPERTIES LABELS "solver;solver-v3;gpu06")
add_poker_test(test_compute_cuda)
set_tests_properties(test_compute_cuda PROPERTIES LABELS "solver;solver-v3;gpu03")
add_poker_test(test_compute_opencl)
set_tests_properties(test_compute_opencl PROPERTIES LABELS "solver;solver-v3;gpu04")
add_poker_test(test_solver_gpu_parity)
set_tests_properties(test_solver_gpu_parity PROPERTIES
    LABELS "solver;solver-v3;gpu05"
    SKIP_RETURN_CODE 77)
add_poker_test(test_rbp)
set_tests_properties(test_rbp PROPERTIES LABELS "solver;solver-v3;rbp01")
add_poker_test(test_pe_solver_api)
set_tests_properties(test_pe_solver_api PROPERTIES LABELS "solver;solver-v3;api01")
add_poker_test(test_pe_checkpoint_v2)
set_tests_properties(test_pe_checkpoint_v2 PROPERTIES LABELS "solver;solver-v3;api04")
add_poker_test(test_abstraction_port)
set_tests_properties(test_abstraction_port PROPERTIES LABELS "solver;solver-v3;abs01")
add_poker_test(test_mpf_abstraction)
set_tests_properties(test_mpf_abstraction PROPERTIES LABELS "engine;cfr;multiway;solver-v3;abs02;abs03" TIMEOUT 180)
add_poker_test(test_pe_storage_precision)
set_tests_properties(test_pe_storage_precision PROPERTIES LABELS "solver;solver-v3;abs04;storage" TIMEOUT 120)
add_poker_test(test_external_mccfr)
set_tests_properties(test_external_mccfr PROPERTIES LABELS "solver;solver-v3;lnb01;mccfr" TIMEOUT 120)

add_poker_test(test_outcome_mccfr)
set_tests_properties(test_outcome_mccfr PROPERTIES LABELS "solver;solver-v3;out01;mccfr" TIMEOUT 120)
add_poker_test(test_evaluator_port)
set_tests_properties(test_evaluator_port PROPERTIES LABELS "solver;solver-v3;gpu02")
add_poker_test(test_backend_parity)
set_tests_properties(test_backend_parity PROPERTIES LABELS "solver;solver-v3;par04")
add_poker_test(test_monker_tree)
set_tests_properties(test_monker_tree PROPERTIES LABELS "solver;solver-v3;monker;mkr01")
add_poker_test(test_monker_layout)
set_tests_properties(test_monker_layout PROPERTIES LABELS "solver;solver-v3;monker;holdem;plo5;plo6;mkr02")

if(TARGET pe-vector-sim)
    add_test(NAME pe_vector_sim_holdem
        COMMAND $<TARGET_FILE:pe-vector-sim>
            --game holdem
            --board "2c 7d Th Js Qc"
            --range0 AsKs
            --range1 9c8c)
    set_tests_properties(pe_vector_sim_holdem PROPERTIES LABELS "solver;cli;vector;holdem")
    add_test(NAME pe_vector_sim_plo5
        COMMAND $<TARGET_FILE:pe-vector-sim>
            --game plo5
            --board "2c 7d Th Js Qc"
            --range0 AsKsQd3c3d
            --range1 9c8d5c6c7c)
    set_tests_properties(pe_vector_sim_plo5 PROPERTIES LABELS "solver;cli;vector;plo5")
    add_test(NAME pe_vector_sim_plo6
        COMMAND $<TARGET_FILE:pe-vector-sim>
            --game plo6
            --board "2c 7d Th Js Qc"
            --range0 AsKsQd3c3d2d
            --range1 9c8d5c6c7c8c)
    set_tests_properties(pe_vector_sim_plo6 PROPERTIES LABELS "solver;cli;vector;plo6")
endif()
add_poker_test(test_monker_mkr)
set_tests_properties(test_monker_mkr PROPERTIES LABELS "solver;solver-v3;monker;mkr04;mkr05")
add_poker_test(test_monker_filters)
set_tests_properties(test_monker_filters PROPERTIES LABELS "solver;solver-v3;monker;mkr06")
add_poker_test(test_range_ppt)
set_tests_properties(test_range_ppt PROPERTIES LABELS "solver;solver-v3;monker;mkr07")
add_poker_test(test_pe_storage_ids)
add_poker_test(test_pe_traversal_vector)
add_poker_test(test_pe_vector_cfr)
add_poker_test(test_pe_storage_port)
add_poker_test(test_pe_estimate)
add_poker_test(test_pe_range)
add_poker_test(test_mpf_ranges)
add_poker_test(test_pe_vector)
add_poker_test(test_pe_blockers)
add_poker_test(test_pe_showdown)
add_poker_test(test_pe_holdem_river_ranges)
add_poker_test(test_pe_combinations)
add_poker_test(test_mpf_flop_chance)
add_poker_test(test_monker_classes)
add_poker_test(test_monker_strategy)
add_poker_test(test_mpf_infoset_privacy)
set_tests_properties(test_pe_combinations PROPERTIES LABELS "solver;solver-v3;chn02")
set_tests_properties(test_mpf_flop_chance PROPERTIES LABELS "engine;cfr;multiway;chance;chn02" TIMEOUT 300)
set_tests_properties(test_monker_classes PROPERTIES LABELS "solver;monker;classes" TIMEOUT 300)
set_tests_properties(test_monker_strategy PROPERTIES LABELS "solver;monker;strategy" TIMEOUT 300)
set_tests_properties(test_mpf_infoset_privacy PROPERTIES LABELS "engine;cfr;multiway;infoset;inf01")
set_tests_properties(test_pe_blockers PROPERTIES LABELS "solver;solver-v3;rng05")
set_tests_properties(test_pe_vector PROPERTIES LABELS "solver;solver-v3;rng04")
set_tests_properties(test_mpf_ranges PROPERTIES LABELS "engine;cfr;multiway;ranges;rng02" TIMEOUT 120)
set_tests_properties(test_pe_range PROPERTIES LABELS "solver;solver-v3;rng01")
set_tests_properties(test_pe_estimate PROPERTIES LABELS "solver;solver-v3;sto05" TIMEOUT 120)
set_tests_properties(test_pe_storage_port PROPERTIES LABELS "solver;solver-v3;sto03")
set_tests_properties(test_pe_storage_ids PROPERTIES LABELS "solver;solver-v3;sto01" TIMEOUT 120)
set_tests_properties(test_pe_showdown PROPERTIES LABELS "solver;solver-v3;vec06")
set_tests_properties(test_pe_holdem_river_ranges PROPERTIES LABELS "solver;solver-v3;vec10")
set_tests_properties(test_pe_traversal_vector PROPERTIES LABELS "solver;solver-v3;vec02")
set_tests_properties(test_pe_vector_cfr PROPERTIES LABELS "solver;solver-v3;vec03;vec08")

add_poker_test(test_ismcts_poker)
set_tests_properties(test_ismcts_poker PROPERTIES LABELS "engine;ismcts;solver")
add_poker_test(test_ismcts_pool_cap)
set_tests_properties(test_ismcts_pool_cap PROPERTIES LABELS "engine;ismcts;solver;bug11")
add_poker_test(test_ismcts_pool_leak)
set_tests_properties(test_ismcts_pool_leak PROPERTIES LABELS "engine;ismcts;solver;bug09")
add_poker_test(test_ismcts_children_leak)
set_tests_properties(test_ismcts_children_leak PROPERTIES LABELS "engine;ismcts;solver;bug10")

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

        # Regression test for BUG-13 (OpenCL hilo eval overflow/alloc guards)
        add_poker_test(test_gpu_opencl_alloc_guards)
        target_link_libraries(test_gpu_opencl_alloc_guards PRIVATE poker_gpu)
        target_compile_definitions(test_gpu_opencl_alloc_guards PRIVATE HAVE_OPENCL)
        target_include_directories(test_gpu_opencl_alloc_guards PRIVATE ${CMAKE_SOURCE_DIR}/src/gpu)
        set_tests_properties(test_gpu_opencl_alloc_guards PROPERTIES
            LABELS "gpu;opencl"
            TIMEOUT 180
            SKIP_RETURN_CODE ${GPU_SKIP_RETURN_CODE})
        set_property(TEST test_gpu_opencl_alloc_guards APPEND PROPERTY LABELS "optional")
        if(NOT GPU_HARDWARE_AVAILABLE)
            set_tests_properties(test_gpu_opencl_alloc_guards PROPERTIES DISABLED ON)
        endif()
        message(STATUS "OpenCL backend available - adding test_gpu_opencl_alloc_guards regression test")
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
add_poker_test(test_bankroll)
set_tests_properties(test_bankroll PROPERTIES LABELS "economics;bankroll;issue169")

# Phase 4 Range Equity Tests
add_poker_test(test_range_equity)
if(OpenMP_FOUND)
    set_tests_properties(test_range_equity PROPERTIES LABELS "equity;phase4" TIMEOUT 180)
endif()

# Phase 1: New Range Parser Tests
add_poker_test(test_pe_range_holdem)
add_poker_test(test_range_bayesian)
set_tests_properties(test_range_bayesian PROPERTIES LABELS "range;bayesian;issue167")
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
add_poker_test(test_missing_game_variants)
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

# These tests are correct but too slow under ASan/UBSan instrumentation to
# finish inside the sanitizer job's per-test timeout (INFRA-01 #97 follow-up).
# They remain in the normal (unsanitized) suite; the sanitizer workflow filters
# them out with -LE sanitizer-skip so the memory-check signal stays useful.
foreach(_slow_san_test
        test_montecarlo_convergence_unity
        test_omaha_range_parser
        test_cfr_chance_nodes
        test_mpf_stack_index)
    if(TARGET ${_slow_san_test} OR TEST ${_slow_san_test})
        set_property(TEST ${_slow_san_test} APPEND PROPERTY LABELS "sanitizer-skip")
    endif()
endforeach()

# ============================================================================
# ISSUE-09 (#165): Analytical Game Benchmark Suite & External Oracles
# Solver qualification layer. Tests live in tests/game_theory/ and exercise
# the CFR core against Chen & Ankenman closed-form equilibria, an independent
# brute-force tree enumeration oracle, and a self-contained sequence-form LP
# (Gambit-style) exact solver.
# ============================================================================
function(add_game_theory_test test_name source_file)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game_theory/${source_file}")
        message(STATUS "Skipping ${test_name}: game_theory/${source_file} not found")
        return()
    endif()
    add_executable(${test_name} game_theory/${source_file} game_theory/analytical_oracles.c)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${test_name} PRIVATE -Wno-error -Wno-stack-usage)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(${test_name} PRIVATE -Wno-error)
    endif()
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/game_theory
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/inlines
        ${CMAKE_BINARY_DIR}/include
    )
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
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        TIMEOUT 180
        LABELS "engine;cfr;game_theory;feat09"
        ENVIRONMENT "MALLOC_CHECK_=3")
    if(TEST ${test_name})
        set_property(TEST ${test_name} APPEND PROPERTY LABELS "sanitizer-skip")
    endif()
endfunction()

add_game_theory_test(test_akq_game          test_akq_game.c)
add_game_theory_test(test_kuhn_openspiel    test_kuhn_openspiel.c)
add_game_theory_test(test_kuhn_multiway_openspiel
                     test_kuhn_multiway_openspiel.c)
add_game_theory_test(test_leduc_openspiel   test_leduc_openspiel.c)
add_game_theory_test(test_vector_vs_scalar  test_vector_vs_scalar.c)
add_game_theory_test(test_best_response_ii  test_best_response_ii.c)
add_game_theory_test(test_best_response_ii_multiway
                     test_best_response_ii_multiway.c)
add_game_theory_test(test_best_response_ii_leduc
                     test_best_response_ii_leduc.c)
add_game_theory_test(test_cfr_plus test_cfr_plus.c)
if(TEST test_cfr_plus)
    set_tests_properties(test_cfr_plus PROPERTIES
        LABELS "solver;solver-v3;game_theory;alg01"
        TIMEOUT 180)
endif()
add_game_theory_test(test_cfr_dcfr test_cfr_dcfr.c)
if(TEST test_cfr_dcfr)
    set_tests_properties(test_cfr_dcfr PROPERTIES
        LABELS "solver;solver-v3;game_theory;alg03"
        TIMEOUT 180)
endif()
add_game_theory_test(test_gambit_exact_lp   test_gambit_exact_lp.c)
add_game_theory_test(test_extended_analytical_games
                     test_extended_analytical_games.c)

# ISSUE-12 (#168): Best-Response & Exploitability engine audit. Validates
# cfr_best_response_value/cfr_exploitability (and the multiway variants)
# against independent recursive oracles, deliberately exploitable policies,
# and an exact Gambit-LP perfect-information equilibrium (e ~ 0).
add_game_theory_test(test_best_response_exploitability
                     test_best_response_exploitability.c)
if(TEST test_best_response_exploitability)
    set_tests_properties(test_best_response_exploitability PROPERTIES
        LABELS "engine;cfr;game_theory;feat09;feat12"
        TIMEOUT 300)
endif()

if(TEST test_vector_vs_scalar)
    set_tests_properties(test_vector_vs_scalar PROPERTIES
        LABELS "solver;solver-v3;game_theory;vec09"
        TIMEOUT 180)
endif()

if(TEST test_best_response_ii)
    set_tests_properties(test_best_response_ii PROPERTIES
        LABELS "solver;solver-v3;game_theory;br02"
        TIMEOUT 180)
endif()
if(TEST test_best_response_ii_multiway)
    set_tests_properties(test_best_response_ii_multiway PROPERTIES
        LABELS "solver;solver-v3;game_theory;br02"
        TIMEOUT 180)
endif()
if(TEST test_best_response_ii_leduc)
    set_tests_properties(test_best_response_ii_leduc PROPERTIES
        LABELS "solver;solver-v3;game_theory;br02"
        TIMEOUT 180)
endif()
