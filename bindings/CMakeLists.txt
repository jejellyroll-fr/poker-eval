# Language bindings

# C API (stable ABI)
option(BUILD_C_API "Build stable C API" ON)
if(BUILD_C_API)
    add_subdirectory(c)
endif()

# Python binding
option(BUILD_PYTHON_BINDING "Build Python binding" ON)
if(BUILD_PYTHON_BINDING)
    add_subdirectory(python)
endif()

# Java binding
option(BUILD_JAVA_BINDING "Build Java binding" OFF)
if(BUILD_JAVA_BINDING)
    find_package(JNI)
    if(JNI_FOUND)
        add_subdirectory(java)
    else()
        message(WARNING "JNI not found, Java binding will not be built")
    endif()
endif()

# C# binding
option(BUILD_CSHARP_BINDING "Build C# binding" OFF)
if(BUILD_CSHARP_BINDING)
    add_subdirectory(csharp)
endif()

# C++ binding
option(BUILD_CXX_BINDING "Build C++ binding" OFF)
if(BUILD_CXX_BINDING)
    add_subdirectory(c++)
endif()

# Cython binding
option(BUILD_CYTHON_BINDING "Build Cython binding" OFF)
if(BUILD_CYTHON_BINDING)
    add_subdirectory(cython)
endif()
