# Benchmarks CMakeLists.txt
# Performance benchmarks for poker-eval library

# Check if benchmarks should be built
if(NOT BUILD_EXAMPLES)
    return()
endif()

message(STATUS "Building performance benchmarks")

# Benchmark executables
set(BENCHMARK_PROGRAMS
    ofc_simd_benchmark
    benchmark_canonical_vs_legacy
    benchmark_iterator_perf
    bench_7c_simd_micro
    bench_7c_validate
    bench_7c_hilo_singlepass
    bench_cfr_holdem_river
    bench_cfr_holdem_multi
    bench_cfr_holdem_turn
    bench_cfr_omaha_river
    bench_cfr_omaha8_river
    bench_cfr_shortdeck_river
    bench_cfr_stud_river
    bench_cfr_stud8_river
    bench_cfr_razz_river
    bench_reproducible_suite
    bench_run_it_twice
    bench_range_parsing
    bench_preflop_equity
    bench_flop_equity
)

set(GPU_BENCHMARK_AVAILABLE 0)
if(GPU_ACCELERATION_AVAILABLE)
    set(GPU_BENCHMARK_AVAILABLE 1)
endif()

# Common benchmark dependencies
set(BENCHMARK_LIBS
    poker_eval_modular
    ${CMAKE_THREAD_LIBS_INIT}
)

if(UNIX AND NOT APPLE)
    list(APPEND BENCHMARK_LIBS m)
endif()

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    list(APPEND BENCHMARK_LIBS rt)  # For clock_gettime
endif()

# Create benchmark executables
foreach(BENCHMARK ${BENCHMARK_PROGRAMS})
    add_executable(${BENCHMARK} ${BENCHMARK}.c)

    # Link minimal deps for canonical benchmark to keep build light
    if(BENCHMARK STREQUAL "benchmark_canonical_vs_legacy")
        target_link_libraries(${BENCHMARK} poker_core poker_utils)
    elseif(BENCHMARK STREQUAL "bench_7c_simd_micro")
        target_link_libraries(${BENCHMARK} poker_core poker_utils)
    elseif(BENCHMARK STREQUAL "bench_7c_validate")
        target_link_libraries(${BENCHMARK} poker_core poker_utils)
    elseif(BENCHMARK STREQUAL "bench_7c_hilo_singlepass")
        target_link_libraries(${BENCHMARK} poker_core poker_utils)
    elseif(BENCHMARK STREQUAL "bench_cfr_holdem_river")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_cfr_holdem_multi")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_cfr_holdem_turn")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_cfr_omaha_river")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_cfr_omaha8_river")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_cfr_shortdeck_river")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_cfr_stud_river")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_cfr_stud8_river")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_cfr_razz_river")
        target_link_libraries(${BENCHMARK} poker_core poker_engine poker_equity poker_utils m)
    elseif(BENCHMARK STREQUAL "bench_range_parsing")
        if(UNIX AND NOT APPLE)
            target_link_libraries(${BENCHMARK} -Wl,--start-group poker_equity poker_distributions poker-eval-range -Wl,--end-group poker_core ${CMAKE_THREAD_LIBS_INIT} m)
        else()
            target_link_libraries(${BENCHMARK} poker_equity poker_distributions poker-eval-range poker_core ${CMAKE_THREAD_LIBS_INIT})
        endif()
    else()
        target_link_libraries(${BENCHMARK} ${BENCHMARK_LIBS})
    endif()

    # Add include directories for canonical headers
    target_include_directories(${BENCHMARK} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    if(APPLE)
        target_link_options(${BENCHMARK} PRIVATE "-fuse-ld=/usr/bin/ld")
    endif()

    # Set optimization flags for reliable benchmarking
    target_compile_options(${BENCHMARK} PRIVATE
        $<$<CONFIG:Release>:-O2 -DNDEBUG>
        $<$<CONFIG:Debug>:-O0 -g>
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${BENCHMARK} PRIVATE -Wno-error)
    endif()

    # Set properties
    set_target_properties(${BENCHMARK} PROPERTIES
        OUTPUT_NAME ${BENCHMARK}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Add to install
    install(TARGETS ${BENCHMARK}
            RUNTIME DESTINATION bin
            COMPONENT Benchmarks)
endforeach()

# Add custom target to run all benchmarks
add_custom_target(run-ofc-benchmarks
    COMMENT "Running OFC performance benchmarks..."
)

# Add individual benchmark run targets
foreach(BENCHMARK ${BENCHMARK_PROGRAMS})
    add_custom_target(run-${BENCHMARK}
        COMMAND $<TARGET_FILE:${BENCHMARK}>
        DEPENDS ${BENCHMARK}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running ${BENCHMARK} benchmark..."
    )

    add_dependencies(run-ofc-benchmarks run-${BENCHMARK})
endforeach()

# GPU-specific benchmarks
if(GPU_BENCHMARK_AVAILABLE)
    # List of GPU benchmark programs
    set(GPU_BENCHMARK_PROGRAMS
        bench_gpu_batched
        bench_gpu_cfr
        bench_gpu_comprehensive
    )

    # Add GPU benchmarks
    foreach(GPU_BENCH ${GPU_BENCHMARK_PROGRAMS})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${GPU_BENCH}.c")
            add_executable(${GPU_BENCH} ${GPU_BENCH}.c)
            target_link_libraries(${GPU_BENCH} poker_gpu poker_core poker_engine poker_utils ${CMAKE_THREAD_LIBS_INIT})

            target_include_directories(${GPU_BENCH} PRIVATE
                ${CMAKE_SOURCE_DIR}/include
            )

            target_compile_options(${GPU_BENCH} PRIVATE
                $<$<CONFIG:Release>:-O2 -DNDEBUG>
                $<$<CONFIG:Debug>:-O0 -g>
            )

            set_target_properties(${GPU_BENCH} PROPERTIES
                OUTPUT_NAME ${GPU_BENCH}
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
            )

            install(TARGETS ${GPU_BENCH}
                    RUNTIME DESTINATION bin
                    COMPONENT Benchmarks)

            add_custom_target(run-${GPU_BENCH}
                COMMAND $<TARGET_FILE:${GPU_BENCH}>
                DEPENDS ${GPU_BENCH}
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Running ${GPU_BENCH} benchmark..."
            )

            add_dependencies(run-ofc-benchmarks run-${GPU_BENCH})
        endif()
    endforeach()

    # Original OFC GPU benchmark (from gpu/ directory)
    add_executable(ofc_gpu_benchmark ${CMAKE_SOURCE_DIR}/gpu/ofc_gpu_benchmark.c)
    target_link_libraries(ofc_gpu_benchmark poker_gpu poker_eval_modular ${CMAKE_THREAD_LIBS_INIT})

    target_compile_options(ofc_gpu_benchmark PRIVATE
        $<$<CONFIG:Release>:-O2 -DNDEBUG>
        $<$<CONFIG:Debug>:-O0 -g>
    )

    set_target_properties(ofc_gpu_benchmark PROPERTIES
        OUTPUT_NAME ofc_gpu_benchmark
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    install(TARGETS ofc_gpu_benchmark
            RUNTIME DESTINATION bin
            COMPONENT Benchmarks)

    add_custom_target(run-ofc_gpu_benchmark
        COMMAND $<TARGET_FILE:ofc_gpu_benchmark>
        DEPENDS ofc_gpu_benchmark
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running ofc_gpu_benchmark benchmark..."
    )

    add_dependencies(run-ofc-benchmarks run-ofc_gpu_benchmark)
endif()

# Benchmark summary target
add_custom_target(benchmark-summary
    COMMAND ${CMAKE_COMMAND} -E echo "Available benchmarks:"
    COMMAND ${CMAKE_COMMAND} -E echo "  ofc_simd_benchmark - OFC SIMD optimisations performance test"
    COMMAND ${CMAKE_COMMAND} -E echo "$<IF:$<BOOL:${GPU_BENCHMARK_AVAILABLE}>,  ofc_gpu_benchmark  - OFC GPU acceleration benchmark,>"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Usage:"
    COMMAND ${CMAKE_COMMAND} -E echo "  make run-ofc-benchmarks      # Run all OFC benchmarks"
    COMMAND ${CMAKE_COMMAND} -E echo "  make run-ofc_simd_benchmark  # Run OFC SIMD benchmark only"
    COMMAND ${CMAKE_COMMAND} -E echo "  ./bin/ofc_simd_benchmark     # Run directly"
    COMMAND ${CMAKE_COMMAND} -E echo "$<IF:$<BOOL:${GPU_BENCHMARK_AVAILABLE}>,  ./bin/ofc_gpu_benchmark     # Run GPU benchmark (pass --backend),>"
)

message(STATUS "Benchmarks configured:")
foreach(BENCHMARK ${BENCHMARK_PROGRAMS})
    message(STATUS "  ${BENCHMARK}")
endforeach()
if(GPU_BENCHMARK_AVAILABLE)
    message(STATUS "  ofc_gpu_benchmark")
endif()
