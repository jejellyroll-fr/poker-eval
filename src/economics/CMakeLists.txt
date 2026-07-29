# Economics module

set(ECONOMICS_SOURCES
    rake.c
    icm.c
    tournament.c
)

add_library(poker_economics ${ECONOMICS_SOURCES})

target_include_directories(poker_economics PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

# Link with core for basic definitions if needed
target_link_libraries(poker_economics PUBLIC poker_core)

install(TARGETS poker_economics
        EXPORT poker-eval-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
