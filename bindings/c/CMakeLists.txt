# C API bindings CMakeLists.txt

add_library(poker_eval_api STATIC
    poker_eval_api.c
)

target_include_directories(poker_eval_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(poker_eval_api PUBLIC poker_core)

# Install
install(TARGETS poker_eval_api
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES poker_eval_api.h
    DESTINATION include/poker_eval
)

# Example/test executable
if(BUILD_EXAMPLES)
    add_executable(demo_c_api demo_c_api.c)
    target_link_libraries(demo_c_api poker_eval_api)
endif()
