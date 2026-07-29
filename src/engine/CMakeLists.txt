# Engine module

set(ENGINE_SOURCES
    game_engine.c
    engine_api.c
    policy/nn_policy.c
    solvers/cfr/cfr_core.c
    solvers/cfr/cfr_storage.c
    solvers/cfr/holdem_river_adapter.c
    solvers/cfr/multiway_postflop_adapter.c
    solvers/cfr/mpf_export.c
    solvers/cfr/mpf_tree.c
    solvers/cfr/jsmn.c
    solvers/cfr/holdem_multi_river_adapter.c
    solvers/cfr/holdem_turn_adapter.c
    solvers/cfr/omaha_river_adapter.c
    solvers/cfr/omaha8_river_adapter.c
    solvers/cfr/shortdeck_river_adapter.c
    solvers/cfr/stud_river_adapter.c
    solvers/cfr/stud8_river_adapter.c
    solvers/cfr/razz_river_adapter.c
    solvers/ismcts/ismcts_poker.c
)

add_library(poker_engine STATIC ${ENGINE_SOURCES})
set_property(TARGET poker_engine PROPERTY POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(poker_engine PRIVATE -Wno-error)
endif()

target_include_directories(poker_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(poker_engine PUBLIC poker_core poker_games m)

install(TARGETS poker_engine
        EXPORT poker-eval-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)

set(BETTING_SOURCES
    betting_api.c
    betting_engine.c
)

add_library(poker_betting STATIC ${BETTING_SOURCES})
set_property(TARGET poker_betting PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(poker_betting PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(poker_betting PUBLIC poker_engine)

install(TARGETS poker_betting
        EXPORT poker-eval-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
