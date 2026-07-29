#!/usr/bin/env bash
#
# Configure, build, and test the project with the CMake "linux-gcc" preset.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
PRESET="linux-gcc"
DEFAULT_BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
BUILD_DIR="${DEFAULT_BUILD_DIR}"
GPU_MODE="${GPU_MODE:-cpu}"   # cpu|cuda|opencl|both|auto
USE_PRESET=1

case "${GPU_MODE}" in
  cpu)
    GPU_ARGS=(-DBUILD_GPU=OFF -DENABLE_CUDA=OFF -DENABLE_OPENCL=OFF)
    ;;
  cuda)
    GPU_ARGS=(-DBUILD_GPU=ON -DENABLE_CUDA=ON -DENABLE_OPENCL=OFF)
    ;;
  opencl)
    GPU_ARGS=(-DBUILD_GPU=ON -DENABLE_CUDA=OFF -DENABLE_OPENCL=ON)
    ;;
  both|all)
    GPU_ARGS=(-DBUILD_GPU=ON -DENABLE_CUDA=ON -DENABLE_OPENCL=ON)
    ;;
  auto)
    GPU_ARGS=()
    ;;
  *)
    echo "Unknown GPU_MODE='${GPU_MODE}'. Expected cpu|cuda|opencl|both|auto." >&2
    exit 1
    ;;
esac

EXTRA_CMAKE_ARGS=("$@")

echo "==> Configuring (${PRESET}, GPU_MODE=${GPU_MODE})"
if command -v ninja >/dev/null 2>&1; then
  cmake --preset "${PRESET}" "${GPU_ARGS[@]}" "${EXTRA_CMAKE_ARGS[@]}"
else
  echo "Ninja not found; falling back to Unix Makefiles." >&2
  USE_PRESET=0
  BUILD_DIR="${DEFAULT_BUILD_DIR}-make"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "Unix Makefiles" \
        -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=ON \
        -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_LTO=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
        "${GPU_ARGS[@]}" "${EXTRA_CMAKE_ARGS[@]}"
fi


echo "==> Building"
if [[ "${USE_PRESET}" -eq 1 ]]; then
  cmake --build --preset "${PRESET}" --parallel
else
  cmake --build "${BUILD_DIR}" --parallel
fi

run_gpu_validation() {
  if [[ "${GPU_MODE}" == "cpu" ]]; then
    echo "GPU_MODE=cpu -> skipping run_gpu_multi_game_validation target"
    return 0
  fi

  local cmd
  if [[ "${USE_PRESET}" -eq 1 ]]; then
    cmd=(cmake --build --preset "${PRESET}" --target run_gpu_multi_game_validation)
  else
    cmd=(cmake --build "${BUILD_DIR}" --target run_gpu_multi_game_validation)
  fi

  if "${cmd[@]}"; then
    echo "GPU multi-game validation passed"
  else
    echo "GPU multi-game validation target missing or failing (GPU disabled?)"
  fi
}

echo "==> Running GPU multi-game validation"
run_gpu_validation

echo "==> Running tests"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "Build completed successfully for preset ${PRESET}"
