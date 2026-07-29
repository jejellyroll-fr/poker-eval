# Example programs
if(NOT BUILD_EXAMPLES)
    return()
endif()
set(EXAMPLES
    # Basic examples
    eval
    fish
    hcmp2
    hcmpn
    pokenum
    ofcalc
    five_card_hands
    seven_card_hands
    usedecks

    # Advanced examples
    hand_distribution_example
    plo_equity_example
    simd_range_equity_example
    batched_montecarlo_example
    doubleflop_example
    range_equity_calc
    advanced_range_example
    complex_expressions_example
    example_percentage_usage
    modular_example
    # OpenMP-dependent examples (guarded below)
    range_equity_mt_example
    benchmark_range_equity_mt

    # Benchmarks
    benchmark_all_versions
    benchmark_large_ranges
    bench_eedc_vs_classic
    bench_eedc_quick
    bench_omaha_opt
    bench_omaha_canonicalize
    bench_batched_simd_cache
    bench_holdem8_simd
    bench_compressed_tables
    bench_lto_comparison
    bench_doubleflop_simd
    bench_enum_adapters
    bench_arch_comparison
    benchmark_qmc_vs_mc
    benchmark_canonicalize_boards
    bench_holdem_canonicalize_7
    benchmark_variance_qmc
    strat_tuner
    bench_holdem_postproc
    bench_multiway_equity
    multiway_equity_cli

    # Performance tests
    performance_test_suite
    test_mt_optimization
    real_world_integration
    simple_lto_demo
    deterministic_benchmark_demo

    # Modern API examples
    modern_api_example
    modern_mask_example
    eval_context_example
    modern_combinations_example
    combo_validation_suite
    omaha_combinations_example

    # Section 7.2: Pre-flop/Flop equity examples
    preflop_equity_demo
    preflop_with_table_demo
    flop_equity_demo

    # Section 7.3: Run It Twice examples
    run_it_twice_demo

    # Section 7.1: PLO5/PLO6 examples
    bench_plo_comparison

    # Game variant examples
    badugi_example
    pineapple_example
    joker_expansion_demo
    mixed_game_demo

    # Drawmaha example
    drawmaha_example

    # Omaha range parser example
    omaha_range_example

    # Omaha table generator
    generate_omaha_table

    # Stud examples

    # ICM Calculator example
    icm_calculator_example
    pe_minimal
    pe_round_smoke_test

    # Neural network policy demo
    nn_policy_demo
)

# Remove OpenMP-dependent examples when OpenMP is disabled
if(NOT ENABLE_OPENMP)
    list(REMOVE_ITEM EXAMPLES
        range_equity_mt_example
        benchmark_all_versions
        benchmark_large_ranges
        benchmark_range_equity_mt
        performance_test_suite
        test_mt_optimization
        real_world_integration)
endif()

# Helper function to add an example
function(add_poker_example name)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}.c")
        add_executable(${name} ${name}.c)

        # Add include directories
        target_include_directories(${name} PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include)

        # Link with appropriate library
        # Note: poker_utils and poker_equity appear twice to resolve circular dependencies
        target_link_libraries(${name} PRIVATE
            poker_utils
            poker_equity
            poker_core
            poker_games
            poker_distributions
            poker-eval-range
            poker_equity
            poker_utils)

        # Disable treating warnings as errors for examples
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${name} PRIVATE -Wno-error)
        endif()

        # Special case: ofcalc needs poker_ofc library
        if(name STREQUAL "ofcalc")
            target_link_libraries(${name} PRIVATE poker_ofc)
            # Add GPU support if available
            if(BUILD_GPU AND TARGET poker_gpu AND TARGET poker_ofc_gpu)
                target_link_libraries(${name} PRIVATE poker_gpu poker_ofc_gpu)
                # Add GPU compile definitions
                if(CUDA_FOUND)
                    target_compile_definitions(${name} PRIVATE HAVE_CUDA)
                endif()
                if(OpenCL_FOUND)
                    target_compile_definitions(${name} PRIVATE HAVE_OPENCL)
                endif()
            endif()
        endif()

        # Special case: pe_* examples need poker_engine and poker_betting libraries
        if(name MATCHES "^pe_")
            target_link_libraries(${name} PRIVATE poker_engine poker_betting)
        endif()

        if(name STREQUAL "mixed_game_demo")
            target_link_libraries(${name} PRIVATE poker_engine poker_betting)
        endif()

        # Special case: nn_policy_demo needs poker_engine and poker_ofc libraries
        if(name STREQUAL "nn_policy_demo")
            target_link_libraries(${name} PRIVATE poker_engine poker_ofc)
        endif()

        # Add math library
        target_link_libraries(${name} PRIVATE m)

        # Add OpenMP if needed
        if(OpenMP_FOUND)
            string(FIND "${name}" "mt" MT_POS)
            string(FIND "${name}" "range_equity" RE_POS)
            string(FIND "${name}" "benchmark" BM_POS)

            if(NOT MT_POS EQUAL -1 OR NOT RE_POS EQUAL -1 OR NOT BM_POS EQUAL -1)
                target_link_libraries(${name} PRIVATE OpenMP::OpenMP_C)
            endif()
        endif()

        # Set output directory
        set_target_properties(${name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )

        # Install examples to a separate directory
        install(TARGETS ${name}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/poker-eval-examples
            OPTIONAL
        )
    else()
        message(WARNING "Example source file ${name}.c not found")
    endif()
endfunction()

# Add all examples
foreach(example ${EXAMPLES})
    add_poker_example(${example})
endforeach()

# Add simple CTest verification for enumeration adapters
if(BUILD_TESTS)
    if(TARGET bench_enum_adapters)
        add_test(NAME bench_enum_adapters_k5
                 COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bench_enum_adapters --verify-only --k 5)
        add_test(NAME bench_enum_adapters_k2
                 COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bench_enum_adapters --verify-only --k 2)
        add_test(NAME bench_enum_adapters_k1
                 COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bench_enum_adapters --verify-only --k 1)
    endif()
endif()

# GPU examples require special handling
if(CUDA_FOUND AND TARGET poker_gpu_combinatorics_cuda)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gpu_combinatorics_demo.c")
        add_executable(gpu_combinatorics_demo gpu_combinatorics_demo.c)

        target_include_directories(gpu_combinatorics_demo PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include
            ${CMAKE_SOURCE_DIR}/gpu/include
            ${CUDA_INCLUDE_DIRS})

        target_link_libraries(gpu_combinatorics_demo PRIVATE
            poker_gpu_combinatorics_cuda
            ${CUDA_LIBRARIES}
            m)

        target_compile_definitions(gpu_combinatorics_demo PRIVATE ENABLE_CUDA)

        set_target_properties(gpu_combinatorics_demo PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

        install(TARGETS gpu_combinatorics_demo
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/poker-eval-examples
            OPTIONAL)
    endif()
endif()

# Special-case: top-level Omaha + Canonical demo (outside examples/)
if(EXISTS "${CMAKE_SOURCE_DIR}/omaha_canonical_demo.c")
    add_executable(omaha_canonical_demo "${CMAKE_SOURCE_DIR}/omaha_canonical_demo.c")

    # Add include directories
    target_include_directories(omaha_canonical_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)

    # Link with appropriate library
    target_link_libraries(omaha_canonical_demo PRIVATE
        poker_core
        poker_games
        poker_utils
        poker_distributions
        poker_equity)

    target_link_libraries(omaha_canonical_demo PRIVATE m)
    set_target_properties(omaha_canonical_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# GPU examples (if GPU support is enabled)
if(BUILD_GPU AND TARGET poker_gpu_unified)
    set(GPU_EXAMPLES
        gpu_eval_example
        adaptive_gpu_example
    )

    foreach(example ${GPU_EXAMPLES})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${example}.c")
            add_executable(${example} ${example}.c)

            # Add include directories
            target_include_directories(${example} PRIVATE
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_BINARY_DIR}/include)

            # Link with appropriate library
            target_link_libraries(${example} PRIVATE
                poker_core
                poker_games
                poker_utils
                poker_distributions
                poker_equity
                poker_gpu)

            target_link_libraries(${example} PRIVATE m)

            set_target_properties(${example} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            )

            install(TARGETS ${example}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/poker-eval-examples
                OPTIONAL
            )
        endif()
    endforeach()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pokenum_opencl.c")
        add_executable(pokenum_opencl pokenum_opencl.c)
        target_include_directories(pokenum_opencl PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include
            ${CMAKE_SOURCE_DIR}/src/gpu)
        target_link_libraries(pokenum_opencl PRIVATE
            poker_core
            poker_games
            poker_utils
            poker_distributions
            poker_equity
            poker_gpu_unified
            poker_gpu_game_config
            OpenCL::OpenCL
            m)
        set_target_properties(pokenum_opencl PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        install(TARGETS pokenum_opencl
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/poker-eval-examples
            OPTIONAL)
    endif()
endif()

# Windows-specific files
if(WIN32)
    # Add getopt implementation for Windows
    add_library(getopt_w32 STATIC getopt_w32.c)

    # Link Windows examples with getopt
    foreach(example ${EXAMPLES})
        if(TARGET ${example})
            target_link_libraries(${example} PRIVATE getopt_w32)
        endif()
    endforeach()
endif()

# Custom targets for running benchmarks
add_custom_target(run-benchmarks
    COMMENT "Running all benchmarks..."
)

# Capture GPU/SIMD baseline metrics before SIMD refactor
add_custom_target(capture_gpu_simd_baseline
    COMMENT "Record PE_RANGE_BUFFER_MODE/PE_RANGE_COMBO_TRACE metrics before SIMD work"
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/capture_gpu_simd_baseline.sh
)

# Add individual benchmark targets
set(BENCHMARK_TARGETS
    benchmark_range_equity_mt
    bench_eedc_vs_classic
    bench_omaha_opt
    bench_compressed_tables
    bench_lto_comparison
)

foreach(benchmark ${BENCHMARK_TARGETS})
    if(TARGET ${benchmark})
        add_custom_target(run-${benchmark}
            COMMAND $<TARGET_FILE:${benchmark}>
            DEPENDS ${benchmark}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Running ${benchmark}..."
        )

        add_dependencies(run-benchmarks run-${benchmark})
    endif()
endforeach()

# Create a simple test script
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/run_examples.sh
"#!/bin/bash
# Run basic poker-eval examples

echo \"Running poker-eval examples...\"

for example in eval fish hcmpn pokenum; do
    if [ -x \"./$example\" ]; then
        echo \"\\nRunning $example:\"
        ./$example
    fi
done

echo \"\\nExamples completed.\"
")

# Make the script executable
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.19")
    file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/run_examples.sh
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
else()
    execute_process(COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/run_examples.sh ERROR_QUIET)
endif()
