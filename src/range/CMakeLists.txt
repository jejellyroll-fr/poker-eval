# CMakeLists.txt for poker-eval-range library

# Source files
set(RANGE_SOURCES
    AdvancedRangeParser.c
    StudRangeParser.c
    pe_range.c
)

# Headers (for installation)
set(RANGE_HEADERS
    ../../include/poker_eval/range/AdvancedRangeParser.h
    ../../include/poker_eval/range/StudRangeParser.h
    ../../include/poker_eval/range.h
)

add_library(poker-eval-range STATIC ${RANGE_SOURCES})

target_include_directories(poker-eval-range PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies (core, etc.)
target_link_libraries(poker-eval-range PUBLIC poker_core)
# Distributions logic is needed for PLO integration (used by AdvancedRangeParser)
target_link_libraries(poker-eval-range PRIVATE poker_distributions)

# Position independent code for shared library linkage
set_target_properties(poker-eval-range PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(TARGETS poker-eval-range
        EXPORT poker-eval-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
