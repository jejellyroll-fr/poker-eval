# GPU acceleration module CMakeLists.txt

# When BUILD_GPU is ON the CUDA toolkit is optional by default. A missing
# toolkit used to print nothing, so configurations silently fell back to
# OpenCL-only (or no backend at all) while the CUDA sources went unbuilt.
# Surface it: always announce the outcome, and let CI opt into failing hard.
option(POKER_EVAL_CUDA_REQUIRED
    "Require the CUDA toolkit when BUILD_GPU is ON (error instead of silently disabling CUDA)"
    OFF)

# Check for CUDA/OpenCL when GPU mode is enabled
set(GPU_SOURCES)
set(GPU_LIBRARIES)
set(GPU_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include)
if(BUILD_GPU)
    if(POLICY CMP0146)
        cmake_policy(SET CMP0146 OLD)
    endif()
    find_package(CUDA QUIET)
    find_package(OpenCL)

    if(NOT CUDA_FOUND)
        if(POKER_EVAL_CUDA_REQUIRED)
            message(FATAL_ERROR "POKER_EVAL_CUDA_REQUIRED is ON but the CUDA toolkit was not \
found. Install the NVIDIA CUDA toolkit (https://developer.nvidia.com/cuda-downloads) and retry.")
        else()
            message(WARNING "CUDA toolkit not found - CUDA acceleration will NOT be built. \
Install the NVIDIA CUDA toolkit to enable it, or set -DPOKER_EVAL_CUDA_REQUIRED=ON to fail \
here instead of silently skipping it.")
        endif()
    endif()

    # CUDA support
    if(CUDA_FOUND)
        message(STATUS "CUDA found, building CUDA acceleration")

        set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS} -O3 -arch=sm_50)

        cuda_add_library(poker_cuda STATIC
            cuda/eval_cuda_kernel.cu
            cuda/eval_cuda_tables.cu
            cuda/eval_cuda.c
        )

        set(CUDA_SEPARABLE_COMPILATION ON)
        cuda_add_library(poker_cuda_generic STATIC
            cuda/eval_generic_kernel.cu
            cuda/eval_low_kernel.cu
            cuda/eval_omaha_kernel.cu
            cuda/eval_cuda_tables.cu
            cuda/eval_cuda_generic.c
        )
        set(CUDA_SEPARABLE_COMPILATION OFF)

        cuda_add_library(poker_gpu_cfr_cuda STATIC
            kernels/gpu_cfr_kernels.cu
            gpu_cfr_cuda.cu
        )

        cuda_add_library(poker_gpu_combinatorics_cuda STATIC
            kernels/gpu_combinatorics.cu
        )

        cuda_add_library(poker_cuda_batch STATIC
            kernels/eval_holdem_batch.cu
        )

        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(poker_cuda PRIVATE -Wno-error=pedantic)
            target_compile_options(poker_cuda_generic PRIVATE -Wno-error=pedantic)
            target_compile_options(poker_gpu_cfr_cuda PRIVATE -Wno-error=pedantic)
            target_compile_options(poker_gpu_combinatorics_cuda PRIVATE -Wno-error=pedantic)
            target_compile_options(poker_cuda_batch PRIVATE -Wno-error=pedantic)
        endif()

        target_include_directories(poker_cuda PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include
            ${CUDA_INCLUDE_DIRS}
        )
        target_include_directories(poker_cuda_generic PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include
            ${CUDA_INCLUDE_DIRS}
        )
        target_include_directories(poker_gpu_cfr_cuda PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${CUDA_INCLUDE_DIRS}
        )
        target_include_directories(poker_gpu_combinatorics_cuda PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include
            ${CUDA_INCLUDE_DIRS}
        )
        target_include_directories(poker_cuda_batch PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include
            ${CUDA_INCLUDE_DIRS}
        )

        target_link_libraries(poker_cuda ${CUDA_LIBRARIES} poker_core)
        target_link_libraries(poker_cuda_generic ${CUDA_LIBRARIES} poker_core)
        target_link_libraries(poker_gpu_cfr_cuda ${CUDA_LIBRARIES} poker_core)
        target_link_libraries(poker_gpu_combinatorics_cuda ${CUDA_LIBRARIES} poker_core)
        target_link_libraries(poker_cuda_batch ${CUDA_LIBRARIES} poker_core)

        list(APPEND GPU_LIBRARIES poker_cuda poker_cuda_generic poker_gpu_cfr_cuda poker_gpu_combinatorics_cuda poker_cuda_batch)

        add_definitions(-DHAVE_CUDA)
        add_definitions(-DENABLE_CUDA)
    endif()

    # OpenCL support
    if(OpenCL_FOUND)
        message(STATUS "OpenCL found, building OpenCL acceleration")

        add_library(poker_opencl STATIC
            opencl/eval_opencl.c
        )

        target_include_directories(poker_opencl PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include
            ${OpenCL_INCLUDE_DIRS}
        )

        target_link_libraries(poker_opencl PUBLIC poker_core ${OpenCL_LIBRARIES})

        # Apple deprecated the whole OpenCL framework in favour of Metal, so its
        # headers carry deprecation attributes on every entry point. GCC honours
        # them and -Werror then turns roughly 240 warnings from Apple's own SDK
        # into a build failure. Nothing here can be fixed by this project, so
        # the diagnostic is dropped for the translation units that include those
        # headers, and only for them.
        if(APPLE)
            target_compile_options(poker_opencl PRIVATE -Wno-deprecated-declarations)
        endif()

        list(APPEND GPU_LIBRARIES poker_opencl)

        add_definitions(-DHAVE_OPENCL)

        file(COPY opencl/eval_kernel.cl DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
        file(COPY opencl/eval_low_kernel.cl DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
        file(COPY opencl/eval_omaha_kernel.cl DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
        file(COPY opencl/eval_generic_kernel.cl DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

        message(STATUS "OpenCL multi-game kernels configured (eval_low, eval_omaha, eval_generic)")

        # GPU-CFR OpenCL backend
        add_library(poker_gpu_cfr_opencl STATIC
            gpu_cfr_opencl.c
        )

        target_include_directories(poker_gpu_cfr_opencl PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${OpenCL_INCLUDE_DIRS}
        )

        target_link_libraries(poker_gpu_cfr_opencl PUBLIC poker_core ${OpenCL_LIBRARIES})
        target_compile_definitions(poker_gpu_cfr_opencl PRIVATE HAVE_OPENCL)

        if(APPLE)
            target_compile_options(poker_gpu_cfr_opencl PRIVATE -Wno-deprecated-declarations)
        endif()

        if(TARGET cfr_core)
            target_link_libraries(poker_gpu_cfr_opencl PUBLIC cfr_core)
        endif()

        list(APPEND GPU_LIBRARIES poker_gpu_cfr_opencl)

        # Copy CFR kernels
        file(COPY kernels/gpu_cfr_kernels.cl DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/kernels)

        message(STATUS "OpenCL GPU-CFR backend configured")
    endif()

    # GPU game configuration helpers
    add_library(poker_gpu_game_config STATIC
        gpu_game_config.c
    )
    target_include_directories(poker_gpu_game_config PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
    )

    add_library(poker_gpu_cfr_base STATIC
        gpu_cfr.c
        gpu_cfr_converter.c
        gpu_cfr_adapter.c
    )
    target_include_directories(poker_gpu_cfr_base PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )

    if(TARGET cfr_core)
        target_link_libraries(poker_gpu_cfr_base cfr_core)
    endif()

    if(GPU_LIBRARIES)
        add_library(poker_gpu_unified STATIC
            eval_gpu_unified.c
            eval_gpu_adaptive.c
            eval_batched_gpu.c
            eval_batched_cuda.c
            eval_batched_opencl.c
            gpu_table_loader.c
        )
        target_include_directories(poker_gpu_unified PUBLIC ${GPU_INCLUDE_DIRS})
        target_link_libraries(poker_gpu_unified
            PUBLIC poker_core
            PRIVATE ${GPU_LIBRARIES})

        if(CUDA_FOUND)
            target_compile_definitions(poker_gpu_unified PRIVATE HAVE_CUDA)
            target_compile_definitions(poker_gpu_unified PRIVATE ENABLE_CUDA)
        endif()
        if(OpenCL_FOUND)
            target_compile_definitions(poker_gpu_unified PRIVATE HAVE_OPENCL)
            target_compile_definitions(poker_gpu_unified PRIVATE ENABLE_OPENCL)
        endif()

        add_library(poker_ofc_gpu STATIC
            ofc_gpu_interface.c
            ofc_gpu_memory.c
            ofc_gpu_compiler.c
            ofc_gpu_kernels.c
            ofc_gpu_production.c
        )

        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(poker_ofc_gpu PRIVATE -Wno-stack-usage -Wno-error -Wno-pedantic)
        endif()

        target_include_directories(poker_ofc_gpu PUBLIC
            ${GPU_INCLUDE_DIRS}
            ${CMAKE_SOURCE_DIR}/include
        )
        target_link_libraries(poker_ofc_gpu poker_gpu_unified ${GPU_LIBRARIES})
        target_compile_definitions(poker_ofc_gpu PRIVATE POKER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        if(CUDA_FOUND)
            target_compile_definitions(poker_ofc_gpu PRIVATE HAVE_CUDA)
            target_include_directories(poker_ofc_gpu PRIVATE ${CUDA_INCLUDE_DIRS})
        endif()
        if(OpenCL_FOUND)
            target_compile_definitions(poker_ofc_gpu PRIVATE HAVE_OPENCL)
            target_include_directories(poker_ofc_gpu PRIVATE ${OpenCL_INCLUDE_DIRS})
        endif()

        list(APPEND GPU_LIBRARIES poker_ofc_gpu)

        add_library(poker_gpu INTERFACE)
        target_link_libraries(poker_gpu INTERFACE
            poker_gpu_unified
            poker_ofc_gpu
            poker_gpu_cfr_base
            poker_gpu_game_config)
        target_include_directories(poker_gpu INTERFACE ${GPU_INCLUDE_DIRS})

        install(TARGETS ${GPU_LIBRARIES} poker_gpu_unified poker_gpu_cfr_base poker_gpu_game_config
                LIBRARY DESTINATION lib
                ARCHIVE DESTINATION lib)

        install(FILES include/eval_gpu.h include/gpu_cfr.h include/gpu_combinatorics.h
                DESTINATION include)
    endif()

    set(GPU_ACCELERATION_AVAILABLE ${GPU_LIBRARIES} PARENT_SCOPE)
else()
    set(GPU_ACCELERATION_AVAILABLE "" PARENT_SCOPE)
endif()

# GPU game configuration helpers
