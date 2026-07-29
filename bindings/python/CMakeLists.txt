# Python binding
find_package(Python3 COMPONENTS Interpreter Development.Module)

if(Python3_FOUND)
    message(STATUS "Python ${Python3_VERSION} found")

    # Create Python module
    Python3_add_library(pypokereval MODULE pypokereval.c)

    # Link with poker-eval library
    if(TARGET poker_eval_modular)
        target_link_libraries(pypokereval PRIVATE poker_eval_modular)
    elseif(TARGET poker-eval)
        target_link_libraries(pypokereval PRIVATE poker-eval)
    else()
        target_link_libraries(pypokereval PRIVATE poker-eval-static)
    endif()

    # Include directories
    target_include_directories(pypokereval PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/inlines
        ${Python3_INCLUDE_DIRS}
    )

    # Set properties for Python module
    set_target_properties(pypokereval PROPERTIES
        PREFIX ""
        OUTPUT_NAME "pypokereval"
    )

    # Platform-specific settings
    if(WIN32)
        set_target_properties(pypokereval PROPERTIES SUFFIX ".pyd")
    endif()

    # Keep the extension and its compatibility wrapper together. scikit-build-core
    # provides an absolute staging directory while regular CMake installs use a
    # prefix-relative site-packages path.
    if(SKBUILD)
        set(_python_install_dir "${SKBUILD_PLATLIB_DIR}")
    elseif(DEFINED PYTHON_INSTALL_DIR AND NOT PYTHON_INSTALL_DIR STREQUAL "")
        set(_python_install_dir "${PYTHON_INSTALL_DIR}")
    else()
        set(_python_install_dir
            "lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages"
        )
    endif()

    install(TARGETS pypokereval
        LIBRARY DESTINATION "${_python_install_dir}"
        COMPONENT python
        RUNTIME DESTINATION "${_python_install_dir}"
        COMPONENT python
    )

    install(FILES pokereval.py
        DESTINATION "${_python_install_dir}"
        COMPONENT python
    )

    # Add test for Python binding
    if(BUILD_TESTS)
        add_test(NAME test_python_binding
            COMMAND ${Python3_EXECUTABLE} -c
                    "import pypokereval; from pokereval import PokerEval; print('Python binding loaded successfully', PokerEval)"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

        set_tests_properties(test_python_binding PROPERTIES
            ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:pypokereval>:${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()
else()
    message(WARNING "Python 3 not found, Python binding will not be built")
endif()
