# Economics module

set(ECONOMICS_SOURCES
    rake.c
    icm.c
    pko.c
    pko_ranges.c
    hrc.c
    fgs.c
    push_fold.c
    push_fold_multiway.c
    tournament.c
    paytable_ev.c
    video_poker_strategy.c
    bankroll.c
)

add_library(poker_economics ${ECONOMICS_SOURCES})

target_include_directories(poker_economics PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

# Link with core for basic definitions if needed
target_link_libraries(poker_economics PUBLIC poker_core m)

# The video poker strategy derivation uses the generalized draw optimizer
# (poker_games) and groups deals by suit isomorphism (local canonical key).
target_link_libraries(poker_economics PRIVATE poker_games)

if(POKER_EVAL_WITH_OPENMP)
    target_link_libraries(poker_economics PRIVATE OpenMP::OpenMP_C)
endif()

install(TARGETS poker_economics
        EXPORT poker-eval-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
