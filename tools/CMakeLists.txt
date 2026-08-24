cmake_minimum_required(VERSION 3.16)

if(NOT TARGET poker_eval_modular)
    message(FATAL_ERROR "tools/ expects poker_eval_modular target to be available")
endif()

set(TOOLS_COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

add_executable(mpf_run_with_metrics mpf_run_with_metrics.c)
target_include_directories(mpf_run_with_metrics PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(mpf_run_with_metrics PRIVATE poker_eval_modular m)

add_executable(mpf_dump_results mpf_dump_results.c)
target_include_directories(mpf_dump_results PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(mpf_dump_results PRIVATE poker_eval_modular m)

add_executable(poker-eval-trainer gto_trainer.c)
target_include_directories(poker-eval-trainer PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(poker-eval-trainer PRIVATE poker_eval_modular m)

add_executable(pe-solution-report pe_solution_report.c)
target_include_directories(pe-solution-report PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-solution-report PRIVATE poker_eval_modular m)

add_executable(pe-runout-report pe_runout_report.c)
target_include_directories(pe-runout-report PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-runout-report PRIVATE poker_eval_modular m)

add_executable(pe-monker-validate pe_monker_validate.c)
target_include_directories(pe-monker-validate PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-monker-validate PRIVATE poker_eval_modular m)

add_executable(pe-vector-sim pe_vector_sim.c)
target_include_directories(pe-vector-sim PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-vector-sim PRIVATE poker_eval_modular m)

add_executable(pe-hand-history-import pe_hand_history_import.c)
target_include_directories(pe-hand-history-import PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-hand-history-import PRIVATE poker_eval_modular m)

add_executable(pe-push-fold pe_push_fold.c)
target_include_directories(pe-push-fold PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-push-fold PRIVATE poker_eval_modular m)

add_executable(pe-push-fold-multiway pe_push_fold_multiway.c)
target_include_directories(pe-push-fold-multiway PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-push-fold-multiway PRIVATE poker_eval_modular m)

add_executable(pe-icm pe_icm.c)
target_include_directories(pe-icm PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-icm PRIVATE poker_eval_modular m)

add_executable(pe-fgs pe_fgs.c)
target_include_directories(pe-fgs PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-fgs PRIVATE poker_eval_modular m)

add_executable(pe-hrc-import pe_hrc_import.c)
target_include_directories(pe-hrc-import PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-hrc-import PRIVATE poker_eval_modular m)

add_executable(pe-preflop-tree pe_preflop_tree.c)
target_include_directories(pe-preflop-tree PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-preflop-tree PRIVATE poker_eval_modular m)

find_path(PE_SDL2_INCLUDE_DIR SDL2/SDL.h)
find_library(PE_SDL2_LIBRARY NAMES SDL2)
if(PE_SDL2_INCLUDE_DIR AND PE_SDL2_LIBRARY)
    add_executable(poker-eval-trainer-gui poker_eval_trainer_gui.c)
    target_include_directories(poker-eval-trainer-gui PRIVATE ${PE_SDL2_INCLUDE_DIR})
    target_compile_definitions(poker-eval-trainer-gui PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(poker-eval-trainer-gui PRIVATE -Wno-error -Wno-documentation-unknown-command)
    target_link_libraries(poker-eval-trainer-gui PRIVATE ${PE_SDL2_LIBRARY})
    if(UNIX)
        target_link_libraries(poker-eval-trainer-gui PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(poker-eval-trainer-gui PRIVATE shell32)
    endif()
else()
    message(WARNING "SDL2 not found: poker-eval-trainer-gui will not be built. Install SDL2 for the portable desktop GUI.")
endif()
