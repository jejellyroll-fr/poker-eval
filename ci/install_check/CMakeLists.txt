cmake_minimum_required(VERSION 3.16)
project(poker_eval_install_check C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(poker-eval CONFIG REQUIRED)

add_executable(install_check main.c)
target_link_libraries(install_check PRIVATE poker-eval::poker-eval)

