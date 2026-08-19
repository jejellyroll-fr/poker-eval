# Games module CMakeLists.txt

# Games source files
set(GAMES_SOURCES
    rules_std.c
    rules_joker.c
    rules_short.c
    rules_astud.c
    rules_manila.c
    rules_drawmaha.c
    rules_fusion.c
    rules_pineapple.c
    rules_badugi.c
    badugi_eval.c
    triple_draw.c
    deck_manila.c
    wildcard_policy.c
    # mixed_game.c is compiled by the engine module: see src/engine/CMakeLists.txt
    lowball.c
    rules_omaha5.c
    rules_omaha6.c
)

# Games header files
set(GAMES_HEADERS
    rules_std.h
    rules_joker.h
    rules_short.h
    rules_astud.h
    rules_manila.h
    rules_omaha5.h
    rules_omaha6.h
    rules_undef.h
    game_std.h
    game_joker.h
    game_short.h
    game_astud.h
    inlines/eval_manila.h
    lowball_algorithm.h
    mixed_game.h
    wildcard_policy.h
    inlines/eval_omaha.h
    inlines/eval_low.h
    inlines/eval_low8.h
    inlines/eval_low27.h
    inlines/eval_joker.h
    inlines/eval_joker_low.h
    inlines/eval_joker_low8.h
    inlines/eval_astud.h
    inlines/eval_short.h
)

# Add games library
add_library(poker_games ${GAMES_SOURCES})
set_property(TARGET poker_games PROPERTY POSITION_INDEPENDENT_CODE ON)

# Link with core module. poker_engine is deliberately absent: the dependency ran
# through mixed_game.c only, which the engine module now compiles.
target_link_libraries(poker_games poker_core)

# Drawmaha calls pe_compute_draw_optima, which lives in the equity module. equity
# already depends on poker_games, so a direct target link would form a cycle that
# CMake forbids for shared libraries. Following the project's documented policy
# ("shared libraries resolve this at load time"), defer the symbol to runtime when
# building shared libraries on Apple; the final link still provides it via
# poker_equity.
if(APPLE AND BUILD_SHARED_LIBS)
  target_link_options(poker_games PRIVATE -Wl,-undefined,dynamic_lookup)
endif()

# Set include directories
target_include_directories(poker_games PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/inlines
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/inlines
)

# Install library
install(TARGETS poker_games
        EXPORT poker-eval-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
