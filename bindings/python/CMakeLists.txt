cmake_minimum_required(VERSION 3.10)
project(PyPokerEvalBinding)


# Obtenir le répertoire home de l'utilisateur sous Windows
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(HOME_DIR $ENV{USERPROFILE})
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" OR ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(HOME_DIR $ENV{HOME})
endif()


# Détecter le système d'exploitation et définir les chemins pour Python 3.11
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    # Chemins pour Python 3.11 installé par Chocolatey sur Windows
    set(Python3_EXECUTABLE "${HOME_DIR}/AppData/Local/Programs/Python/Python311/python.exe")
    set(Python3_LIBRARY "${HOME_DIR}/AppData/Local/Programs/Python/Python311/libs/python311.lib")
    set(Python3_INCLUDE_DIR "${HOME_DIR}/AppData/Local/Programs/Python/Python311/include")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # Sur macOS, Homebrew installe Python 3.11 dans /usr/local/opt/python@3.11
    set(Python3_EXECUTABLE "/usr/local/opt/python@3.11/bin/python3.11")
    set(Python3_LIBRARY "/usr/local/opt/python@3.11/lib/libpython3.11.dylib")
    set(Python3_INCLUDE_DIR "/usr/local/opt/python@3.11/include/python3.11")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # Sur Linux, le PPA deadsnakes permet l'installation de Python 3.11
    set(Python3_EXECUTABLE "/usr/bin/python3.11")
    set(Python3_LIBRARY "/usr/lib/x86_64-linux-gnu/libpython3.11.so")
    set(Python3_INCLUDE_DIR "/usr/include/python3.11")
endif()

# Essayez de trouver une installation de Python 3.x
find_package(Python3 COMPONENTS Interpreter Development)
if(Python3_FOUND)
    message(STATUS "Python 3.x trouvé : Version ${Python3_VERSION}, Librairies : ${Python3_LIBRARIES}, Include : ${Python3_INCLUDE_DIRS}")
    include_directories(${Python3_INCLUDE_DIRS})

    # Chemin vers la bibliothèque poker-eval construite, adapté en fonction de l'OS
    if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        include_directories("${HOME_DIR}/Documents/GitHub/poker-eval/include")
        set(POKER_EVAL_LIB_PATH "${HOME_DIR}/Documents/GitHub/poker-eval//build/Debug/poker_lib_static.lib")
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        include_directories("${CMAKE_SOURCE_DIR}/include")
        set(POKER_EVAL_LIB_PATH "${CMAKE_SOURCE_DIR}/libpoker_lib_static.a")
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        include_directories("${CMAKE_SOURCE_DIR}/include")
        set(POKER_EVAL_LIB_PATH "${CMAKE_SOURCE_DIR}/libpoker_lib_static.a")
    endif()

    # Incluez les en-têtes de poker-eval si vous les avez
    include_directories("${HOME_DIR}/Documents/GitHub/poker-eval/include")

    # Définissez le module Python
    add_library(pypokereval MODULE pypokereval.c)
    target_link_libraries(pypokereval ${Python3_LIBRARIES} ${POKER_EVAL_LIB_PATH})
    set_target_properties(pypokereval PROPERTIES PREFIX "")
else()
    message(WARNING "Python 3.x n'a pas été trouvé, le binding Python ne sera pas construit.")
endif()
