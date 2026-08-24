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

add_executable(pe-vector-sim pe_vector_sim.c)
target_include_directories(pe-vector-sim PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-vector-sim PRIVATE poker_eval_modular m)

add_executable(pe-hand-history-import pe_hand_history_import.c)
target_include_directories(pe-hand-history-import PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-hand-history-import PRIVATE poker_eval_modular m)

add_executable(pe-preflop-tree pe_preflop_tree.c)
target_include_directories(pe-preflop-tree PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-preflop-tree PRIVATE poker_eval_modular m)
