# Distributions module CMakeLists.txt

# Distributions source files
set(DISTRIBUTIONS_SOURCES
    omaha_distributions.c
    stud_distributions.c
    holdem_distributions.c
    plo_nomenclature.c
    plo_integration.c
    hand_distribution.c
)

# Distributions header files
set(DISTRIBUTIONS_HEADERS
    hand_distributions.h
    hand_distribution.h
    omaha_distributions.h
    stud_distributions.h
    holdem_distributions.h
    plo_nomenclature.h
    plo_integration.h
    card_converter.h
    card.h
)

# Add distributions library
add_library(poker_distributions ${DISTRIBUTIONS_SOURCES})
set_property(TARGET poker_distributions PROPERTY POSITION_INDEPENDENT_CODE ON)

# Link with core and games modules
target_link_libraries(poker_distributions poker_core poker_equity)

# Set include directories
target_include_directories(poker_distributions PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/inlines
    ${CMAKE_SOURCE_DIR}/games
    ${CMAKE_SOURCE_DIR}/games/inlines
)

# Install library
install(TARGETS poker_distributions
        EXPORT poker-eval-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
