#!/bin/bash
# Build GPU tests without full CMake rebuild

set -e

PROJECT_ROOT="."
BUILD_DIR="$PROJECT_ROOT/build/linux-clang"
INCLUDE_DIRS="-I$PROJECT_ROOT/include -I$BUILD_DIR/include"

OFC_SOURCES="$PROJECT_ROOT/src/ofc/ofc.c $PROJECT_ROOT/src/ofc/ofc_simd.c $PROJECT_ROOT/src/ofc/ofc_monte_carlo_simd.c"
GPU_SOURCES_BASE="$PROJECT_ROOT/src/gpu/ofc_gpu_interface.c $PROJECT_ROOT/src/gpu/ofc_gpu_memory.c $PROJECT_ROOT/src/gpu/ofc_gpu_compiler.c $PROJECT_ROOT/src/gpu/ofc_gpu_kernels.c $PROJECT_ROOT/src/gpu/ofc_gpu_production.c $PROJECT_ROOT/src/gpu/eval_gpu_unified.c"
CORE_SOURCES="$PROJECT_ROOT/src/core/deck_std.c $PROJECT_ROOT/src/core/low_eval.c $PROJECT_ROOT/src/core/t_nbits.c $PROJECT_ROOT/src/core/t_topcard.c $PROJECT_ROOT/src/core/t_topfivecards.c $PROJECT_ROOT/src/core/t_straight.c $PROJECT_ROOT/src/core/t_cardmasks.c $PROJECT_ROOT/src/core/t_botcard.c $PROJECT_ROOT/src/core/t_botfivecards.c $PROJECT_ROOT/src/core/deck.c"
GAMES_SOURCES="$PROJECT_ROOT/src/games/lowball_algorithm.c"

ALL_SOURCES="$OFC_SOURCES $GPU_SOURCES_BASE $CORE_SOURCES $GAMES_SOURCES"

# Detect GPU backend
CUDA_AVAILABLE=0
OPENCL_AVAILABLE=0

if [ -f "/usr/local/cuda/include/cuda.h" ]; then
    CUDA_AVAILABLE=1
    CUDA_FLAGS="-DHAVE_CUDA -I/usr/local/cuda/include"
    CUDA_LIBS="-L/usr/local/cuda/lib64 -lcudart"
    ALL_SOURCES="$ALL_SOURCES $PROJECT_ROOT/src/gpu/cuda/eval_cuda.c"
    echo "✓ CUDA detected"
fi

if [ -f "/usr/include/CL/cl.h" ] || [ -f "/usr/local/include/CL/cl.h" ]; then
    OPENCL_AVAILABLE=1
    OPENCL_FLAGS="-DHAVE_OPENCL"
    OPENCL_LIBS="-lOpenCL"
    ALL_SOURCES="$ALL_SOURCES $PROJECT_ROOT/src/gpu/opencl/eval_opencl.c"
    echo "✓ OpenCL detected"
fi

if [ $CUDA_AVAILABLE -eq 0 ] && [ $OPENCL_AVAILABLE -eq 0 ]; then
    echo "⚠ No GPU backend detected - tests will check availability only"
fi

# Compile sources into object files
echo "Compiling sources..."
gcc -c $INCLUDE_DIRS $CUDA_FLAGS $OPENCL_FLAGS $ALL_SOURCES -Wno-unused-function -Wno-implicit-function-declaration

# Compile tests and link with object files
echo "Compiling test_ofc_gpu_basic..."
gcc $INCLUDE_DIRS $CUDA_FLAGS $OPENCL_FLAGS \
    $PROJECT_ROOT/tests/test_ofc_gpu_basic.c \
    *.o \
    -o test_ofc_gpu_basic \
    -lm -lpthread $CUDA_LIBS $OPENCL_LIBS

echo "Compiling test_ofc_gpu_comprehensive..."
gcc $INCLUDE_DIRS $CUDA_FLAGS $OPENCL_FLAGS \
    $PROJECT_ROOT/tests/test_ofc_gpu_comprehensive.c \
    *.o \
    -o test_ofc_gpu_comprehensive \
    -lm -lpthread $CUDA_LIBS $OPENCL_LIBS

echo "✓ Compilation complete"
echo "Run with:"
echo "  ./test_ofc_gpu_basic"
echo "  ./test_ofc_gpu_comprehensive"
