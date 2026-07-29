#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
DEFAULT_TEST_DIR="${BUILD_DIR}/tests"
GPU_BUILD_DIR="${GPU_BUILD_DIR:-${ROOT_DIR}/build/gpu-validation}"
TEST_DIR="${TEST_DIR:-${DEFAULT_TEST_DIR}}"

locate_test_dir() {
    find "${ROOT_DIR}/build" -type d -name tests -exec test -x "{}/test_gpu_omaha" ';' -print -quit
}

find_build_dir_for_tests() {
    local cache
    cache="$(find "${ROOT_DIR}/build" -name CMakeCache.txt -print -quit)"
    if [[ -n "${cache}" ]]; then
        dirname "${cache}"
    else
        printf '%s' "${BUILD_DIR}"
    fi
}


gpu_enabled_in_build() {
    local cache="$1/CMakeCache.txt"
    if [[ -f "${cache}" ]]; then
        grep -m1 -E '^BUILD_GPU:BOOL=' "${cache}" | cut -d= -f2
    fi
}

configure_gpu_build() {
    mkdir -p "${GPU_BUILD_DIR}"
    env CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12 CUDAHOSTCXX=/usr/bin/g++-12 \
    cmake -S "${ROOT_DIR}" -B "${GPU_BUILD_DIR}" \
        -G "Ninja" \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_STATIC_LIBS=ON \
        -DBUILD_EXAMPLES=ON \
        -DBUILD_TESTS=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_LTO=ON \
        -DBUILD_GPU=ON \
        -DENABLE_CUDA=ON \
        -DENABLE_OPENCL=ON \
        -DCMAKE_C_COMPILER=/usr/bin/gcc-12 \
        -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
        -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/gcc-12 \
        -DCMAKE_CUDA_FLAGS="-ccbin=/usr/bin/gcc-12 -allow-unsupported-compiler"
}

ensure_tests() {
    if [[ -x "${TEST_DIR}/test_gpu_omaha" ]]; then
        return 0
    fi

    TEST_DIR="$(locate_test_dir)"
    if [[ -n "${TEST_DIR}" ]]; then
        BUILD_DIR="$(dirname "${TEST_DIR}")"
    else
        BUILD_DIR="$(find_build_dir_for_tests)"
        echo "Test directory '${DEFAULT_TEST_DIR}' missing. Attempting to build in ${BUILD_DIR}..." >&2
    fi

    local gpu_flag
    gpu_flag="$(gpu_enabled_in_build "${BUILD_DIR}")"
    if [[ "${gpu_flag}" != "ON" ]]; then
        echo "GPU acceleration disabled in ${BUILD_DIR} (BUILD_GPU=${gpu_flag}); configuring GPU build at ${GPU_BUILD_DIR}" >&2
        configure_gpu_build
        BUILD_DIR="${GPU_BUILD_DIR}"
        TEST_DIR="${GPU_BUILD_DIR}/tests"
    fi

    cmake --build "${BUILD_DIR}" --target test_gpu_omaha
    TEST_DIR="$(locate_test_dir)"

    if [[ -z "${TEST_DIR}" ]] || [[ ! -x "${TEST_DIR}/test_gpu_omaha" ]]; then
        echo "Failed to build test_gpu_omaha; run cmake --build manually." >&2
        exit 1
    fi
}

ensure_tests

echo "Running GPU multi-game validation (${TEST_DIR}/test_gpu_omaha)..."
ctest --test-dir "${TEST_DIR}" -R test_gpu_omaha --output-on-failure
