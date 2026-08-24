# Engine module

set(ENGINE_SOURCES
    game_engine.c
    engine_api.c
    # mixed_game.c lives under games/ but belongs to the engine layer: it is the
    # only games source calling into the engine, while engine_api.c calls its
    # mixed_game_* API. Compiling it here breaks the poker_games <-> poker_engine
    # target cycle, which CMake only tolerates between static archives.
    ${CMAKE_CURRENT_SOURCE_DIR}/../games/mixed_game.c
    policy/nn_policy.c
    solvers/cfr/board_canonical.c
    solvers/cfr/board_texture.c
    solvers/cfr/strength_bucketing.c
    solvers/cfr/draw_abstraction.c
    solvers/cfr/draw_chance.c
    solvers/cfr/icm_utility.c
    solvers/cfr/cfr_core.c
    solvers/cfr/legacy_vector_adapter.c
    solvers/cfr/cfr_traversal_full_scalar.c
    solvers/cfr/cfr_locks.c
    solvers/cfr/cfr_storage_legacy_port.c
    solvers/cfr/cfr_regret_ops.c
    solvers/cfr/cfr_average_ops.c
    solvers/cfr/bet_sizing.c
    solvers/cfr/cfr_storage.c
    solvers/cfr/hand_clustering.c
    solvers/cfr/holdem_river_adapter.c
    solvers/cfr/multiway_postflop_adapter.c
    solvers/cfr/cfr_resolve.c
    solvers/cfr/mpf_export.c
    solvers/cfr/mpf_compact_storage.c
    solvers/cfr/mpf_tree.c
    solvers/cfr/mpf_stack_index.c
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../solver/domain/abstraction.c
)

add_library(poker_engine ${ENGINE_SOURCES})
set_property(TARGET poker_engine PROPERTY POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(poker_engine PRIVATE -Wno-error)
endif()

target_include_directories(poker_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# EXT-03: the legacy solver now reports through the v3 telemetry port instead
# of writing to stderr itself. The dependency runs engine -> solver, which is
# the migration direction; poker_solver itself links only poker_core.
target_link_libraries(poker_engine PUBLIC poker_core poker_games poker_equity poker_economics poker_solver m)

if(BUILD_TESTS)
    add_executable(test_legacy_vector_adapter
        ${CMAKE_SOURCE_DIR}/tests/test_legacy_vector_adapter.c)
    target_link_libraries(test_legacy_vector_adapter PRIVATE poker_engine)
    add_test(NAME test_legacy_vector_adapter COMMAND test_legacy_vector_adapter)
endif()

install(TARGETS poker_engine
        EXPORT poker-eval-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)

set(BETTING_SOURCES
    betting_api.c
    betting_engine.c
)

add_library(poker_betting ${BETTING_SOURCES})
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
