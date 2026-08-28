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

add_executable(pe-preflop-solve pe_preflop_solve.c)
target_include_directories(pe-preflop-solve PRIVATE ${TOOLS_COMMON_INCLUDES})
target_link_libraries(pe-preflop-solve PRIVATE poker_eval_modular m)

find_path(PE_SDL2_INCLUDE_DIR SDL2/SDL.h
    PATH_SUFFIXES include)
find_library(PE_SDL2_LIBRARY NAMES SDL2
    PATH_SUFFIXES lib lib/x64 lib/x86)
if(PE_SDL2_INCLUDE_DIR AND PE_SDL2_LIBRARY)
    add_executable(poker-eval-trainer-gui poker_eval_trainer_gui.c)
    target_include_directories(poker-eval-trainer-gui PRIVATE
        ${PE_SDL2_INCLUDE_DIR}
        ${TOOLS_COMMON_INCLUDES})
    target_compile_definitions(poker-eval-trainer-gui PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(poker-eval-trainer-gui PRIVATE -Wno-error -Wno-documentation-unknown-command)
    target_link_libraries(poker-eval-trainer-gui PRIVATE
        poker_eval_modular ${PE_SDL2_LIBRARY})
    if(UNIX)
        target_link_libraries(poker-eval-trainer-gui PRIVATE m)
    endif()
    if(WIN32)
        target_link_libraries(poker-eval-trainer-gui PRIVATE shell32)
    endif()
else()
    message(WARNING "SDL2 not found: poker-eval-trainer-gui will not be built. Install SDL2 for the portable desktop GUI.")
endif()

# Preferred native desktop shell. NAppGUI is deliberately an external,
# configurable dependency: distributors can package it, while a normal build
# remains independent of any GUI toolkit. Configure with
# -DPE_NAPPGUI_ROOT=/path/to/nappgui_src.
set(PE_NAPPGUI_ROOT "" CACHE PATH
    "NAppGUI source tree used to build the native poker-eval Studio GUI")
if(PE_NAPPGUI_ROOT AND EXISTS "${PE_NAPPGUI_ROOT}/CMakeLists.txt")
    set(NAPPGUI_DEMO OFF CACHE BOOL "" FORCE)
    set(NAPPGUI_WEB OFF CACHE BOOL "" FORCE)
    if(NOT TARGET nappgui::gui)
        # The parent project enables -Wundef/-Werror globally; NAppGUI is an
        # external C90 project and intentionally tests some undefined vendor
        # macros. Keep the warning policy local to this sub-build.
        set(PE_SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-undef -Wno-error")
        add_subdirectory("${PE_NAPPGUI_ROOT}"
                         "${CMAKE_BINARY_DIR}/nappgui" EXCLUDE_FROM_ALL)
        set(CMAKE_C_FLAGS "${PE_SAVED_CMAKE_C_FLAGS}")
        foreach(PE_NAPPGUI_TARGET sewer osbs core geom2d draw2d osgui gui osapp encode inet ogl3d nrc)
            if(TARGET ${PE_NAPPGUI_TARGET})
                target_compile_options(${PE_NAPPGUI_TARGET} PRIVATE
                    -Wno-undef -Wno-error)
            endif()
        endforeach()
    endif()
    if(TARGET nappgui::gui AND TARGET nappgui::osapp)
        set(PE_NAPPGUI_LIBS
            nappgui::osapp nappgui::gui nappgui::osgui nappgui::draw2d
            nappgui::geom2d nappgui::core nappgui::osbs nappgui::sewer)
    elseif(TARGET gui AND TARGET osapp)
        # A source checkout exposes plain targets; an installed package uses
        # the nappgui:: namespace.
        set(PE_NAPPGUI_LIBS osapp gui osgui draw2d geom2d core osbs sewer)
    endif()
    if(PE_NAPPGUI_LIBS)
        add_executable(poker-eval-studio
            poker_eval_studio.c
            pe_tree_editor_model.c
            pe_tree_outline.c
            pe_analysis_model.c)
        # Studio launches the command-line runners for Solve.  Make the
        # native target self-contained so building only poker-eval-studio
        # also places both runners beside it.
        add_dependencies(poker-eval-studio pe-preflop-solve pe-vector-sim)
        target_include_directories(poker-eval-studio PRIVATE
            ${TOOLS_COMMON_INCLUDES}
            ${PE_NAPPGUI_ROOT}/src)
        target_compile_options(poker-eval-studio PRIVATE
            -Wno-error -Wno-cast-qual -Wno-undef)
        target_link_libraries(poker-eval-studio PRIVATE
            poker_eval_modular
            ${PE_NAPPGUI_LIBS})
        if(WIN32)
            target_link_libraries(poker-eval-studio PRIVATE
                shell32 ws2_32 gdiplus shlwapi comctl32 uxtheme)
        elseif(APPLE)
            target_link_libraries(poker-eval-studio PRIVATE
                "-framework Cocoa"
                "-framework UniformTypeIdentifiers")
        elseif(UNIX)
            find_package(Threads REQUIRED)
            target_link_libraries(poker-eval-studio PRIVATE
                Threads::Threads ${CMAKE_DL_LIBS} m)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(PE_GTK3 REQUIRED gtk+-3.0)
            target_include_directories(poker-eval-studio PRIVATE ${PE_GTK3_INCLUDE_DIRS})
            target_link_directories(poker-eval-studio PRIVATE ${PE_GTK3_LIBRARY_DIRS})
            target_link_libraries(poker-eval-studio PRIVATE ${PE_GTK3_LIBRARIES})
        endif()
    else()
        message(WARNING "PE_NAPPGUI_ROOT does not expose the expected NAppGUI targets; poker-eval-studio will not be built.")
    endif()
else()
    message(STATUS "NAppGUI not configured: poker-eval-studio is disabled (set PE_NAPPGUI_ROOT to enable it).")
endif()
