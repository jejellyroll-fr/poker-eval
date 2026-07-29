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

# Create a combined interface library that includes all modules
add_library(poker_eval_modular INTERFACE)

# Link all modules
# Note: There is a circular dependency between poker_equity and poker_distributions:
# - plo_integration.c (distributions) calls CalculateEquityForRanges (equity)
# - AdvancedRangeParser.c (equity) uses symbols from distributions
# We use linker groups to resolve circular dependencies in static libraries (Linux only)
if(UNIX AND NOT APPLE)
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
