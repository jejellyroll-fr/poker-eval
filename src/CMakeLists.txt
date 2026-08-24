# Main src CMakeLists.txt for modular structure
cmake_minimum_required(VERSION 3.16)
project(poker_eval_modular VERSION 1.0.0 LANGUAGES C)

# Add subdirectories in dependency order
add_subdirectory(core)
add_subdirectory(benchmarks)
add_subdirectory(utils)
add_subdirectory(games)
add_subdirectory(distributions)
add_subdirectory(range)
add_subdirectory(equity)
add_subdirectory(engine)
add_subdirectory(economics)
add_subdirectory(gpu)
add_subdirectory(examples)
add_subdirectory(ofc)

# Solver module (architecture v3). The hexagon's domain is declared here rather
# than through add_subdirectory: it is a single translation unit for now, and
# CTR-01 is scoped to this file. It deliberately links nothing — the domain
# depends on ports, never on the engine, the evaluator or a GPU backend.
find_package(ZLIB REQUIRED)

add_library(poker_solver
    solver/domain/solver.c
    solver/domain/registry.c
    solver/domain/config.c
    solver/domain/rng.c
    solver/domain/plan.c
    solver/domain/storage_v2.c
    solver/domain/range.c
    solver/domain/reach.c
    solver/domain/blockers.c
    solver/domain/chance.c
    solver/domain/combinations.c
    solver/domain/telemetry.c
    solver/domain/traversal_full_vector.c
    solver/domain/batch.c
    solver/domain/regret_vanilla.c
    solver/domain/regret_plus.c
    solver/domain/regret_dcfr.c
    solver/domain/regret_legacy_exp.c
    solver/domain/average.c
    solver/domain/pruning_rbp.c
    solver/domain/showdown_vector.c
    solver/domain/traversal_chance_vector.c
    solver/domain/traversal_external.c
    solver/domain/traversal_outcome.c
    solver/domain/compute_layout.c
    solver/domain/best_response_ii.c
    solver/adapters/storage_ram.c
    solver/adapters/persist_checkpoint.c
    solver/adapters/compute_cpu_ref.c
    solver/adapters/compute_cpu_par.c
    solver/adapters/compute_gpu_updates.c
    solver/adapters/compute_cuda.c
    solver/adapters/compute_opencl.c
    solver/adapters/evaluator_equity.c
    solver/adapters/evaluator_gpu.c
    solver/adapters/monker_classes.c
    solver/adapters/monker_tree_reader.c
    solver/adapters/monker_mkr_reader.c
    solver/adapters/monker_filters.c
    solver/adapters/range_ppt.c
    solver/adapters/range_parser.c
    solver/adapters/telemetry_null.c
    solver/adapters/telemetry_stream.c
    solver/adapters/telemetry_callback.c
)

target_include_directories(poker_solver PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

# The only link dependency of the domain, and it is a leaf: PCG32 lives in
# poker_core, which itself links nothing but libm. No engine, no evaluator, no
# GPU backend — those arrive through the driven ports.
# The range parser adapter needs the range engine. The edge is acyclic:
# poker-eval-range reaches poker_core, poker_equity and poker_distributions,
# and none of those links poker_solver.
target_link_libraries(poker_solver PUBLIC poker_core poker-eval-range poker_equity ZLIB::ZLIB)

if(OpenMP_C_FOUND)
    target_link_libraries(poker_solver PUBLIC OpenMP::OpenMP_C)
endif()

# The adapter itself is always present, but only the CUDA build gets access to
# the existing batched GPU library and its CUDA headers/runtime.
if(TARGET poker_cuda_batch AND TARGET poker_gpu_unified)
    target_compile_definitions(poker_solver PRIVATE PE_COMPUTE_CUDA_AVAILABLE=1)
    target_link_libraries(poker_solver PRIVATE poker_gpu_unified)
endif()

if(TARGET poker_opencl AND TARGET poker_gpu_unified)
    target_compile_definitions(poker_solver PRIVATE PE_COMPUTE_OPENCL_AVAILABLE=1)
    target_link_libraries(poker_solver PRIVATE poker_gpu_unified)
endif()

install(TARGETS poker_solver
        EXPORT poker-eval-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)

# The modular libraries above are declared without an explicit type, so they
# follow BUILD_SHARED_LIBS. Apply the properties a shared build needs in one
# place rather than in each module.
set(POKER_EVAL_MODULES
    poker_core
    poker_utils
    poker_games
    poker_distributions
    poker_equity
    poker-eval-range
    poker_engine
    poker_betting
    poker_economics
    poker_ofc
    poker_solver
)

foreach(module IN LISTS POKER_EVAL_MODULES)
    set_target_properties(${module} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        # The C sources carry no dllexport annotations, so without this a
        # Windows shared build would produce DLLs exporting nothing.
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    if(BUILD_SHARED_LIBS)
        set_target_properties(${module} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})
    endif()
endforeach()

# Create a combined interface library that includes all modules
add_library(poker_eval_modular INTERFACE)

# Link all modules
# Note: There is a circular dependency between poker_equity and poker_distributions:
# - plo_integration.c (distributions) calls CalculateEquityForRanges (equity)
# - AdvancedRangeParser.c (equity) uses symbols from distributions
# Shared libraries resolve this at load time, so the linker workarounds below are
# only needed for static archives.
if(BUILD_SHARED_LIBS)
    target_link_libraries(poker_eval_modular INTERFACE ${POKER_EVAL_MODULES})
elseif(UNIX AND NOT APPLE)
    # GNU ld supports --start-group/--end-group for circular dependencies
    target_link_libraries(poker_eval_modular INTERFACE
        -Wl,--start-group
        poker_core
        poker_utils
        poker_games
        poker_distributions
        poker_equity
        poker-eval-range
        poker_engine
        poker_betting
        poker_economics
        poker_ofc
        poker_solver
        -Wl,--end-group
    )
else()
    # macOS/Apple ld doesn't support --start-group, but handles circular deps differently
    # List libraries with circular dependencies multiple times if needed
    target_link_libraries(poker_eval_modular INTERFACE
        poker_core
        poker_utils
        poker_games
        poker_distributions
        poker_equity
        poker-eval-range
        poker_distributions
        poker_equity
        poker_engine
        poker_betting
        poker_economics
        poker_ofc
        poker_solver
    )
endif()

# Set include directories for the combined library
target_include_directories(poker_eval_modular INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Install the combined library
install(TARGETS poker_eval_modular
        EXPORT poker-eval-targets)

set_target_properties(poker_eval_modular PROPERTIES
    EXPORT_NAME poker-eval)

# Create an alias for backward compatibility
add_library(poker_eval ALIAS poker_eval_modular)
