#!/usr/bin/env bash
#
# Configure, build, and test the project with the CMake "macos" preset.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

PRESET="macos"
DEFAULT_BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
BUILD_DIR="${DEFAULT_BUILD_DIR}"

# cpu | opencl | auto
GPU_MODE="${GPU_MODE:-opencl}"
USE_PRESET=1

# Toujours définir les tableaux (critique avec set -u)
GPU_ARGS=()
EXTRA_CMAKE_ARGS=()

# Arguments passés au script → extra flags cmake
if [[ "$#" -gt 0 ]]; then
  EXTRA_CMAKE_ARGS+=("$@")
fi

# Configure GPU flags
case "${GPU_MODE}" in
  cpu)
    GPU_ARGS=(
      -DBUILD_GPU=OFF
      -DENABLE_CUDA=OFF
      -DENABLE_OPENCL=OFF
    )
    ;;
  opencl)
    GPU_ARGS=(
      -DBUILD_GPU=ON
      -DENABLE_CUDA=OFF
      -DENABLE_OPENCL=ON
    )
    ;;
  auto)
    if pkg-config --exists OpenCL 2>/dev/null || \
       [[ -d "/System/Library/Frameworks/OpenCL.framework" ]]; then
      GPU_ARGS=(
        -DBUILD_GPU=ON
        -DENABLE_CUDA=OFF
        -DENABLE_OPENCL=ON
      )
      echo "GPU_MODE=auto → OpenCL détecté, activation OPENCL."
    else
      GPU_ARGS=(
        -DBUILD_GPU=OFF
        -DENABLE_CUDA=OFF
        -DENABLE_OPENCL=OFF
      )
      echo "GPU_MODE=auto → pas d'OpenCL détecté, fallback CPU."
    fi
    ;;
  *)
    echo "GPU_MODE='${GPU_MODE}' non supporté sur macOS. Utilise cpu|opencl|auto." >&2
    exit 1
    ;;
esac

# Sanity check
if ! command -v cmake >/dev/null 2>&1; then
  echo "Erreur: cmake introuvable dans le PATH." >&2
  exit 1
fi

echo "==> Configuring (${PRESET}, GPU_MODE=${GPU_MODE})"

if command -v ninja >/dev/null 2>&1; then
  # Utilise le preset CMake et injecte les options supplémentaires
  cmake --preset "${PRESET}" \
    ${GPU_ARGS[@]+"${GPU_ARGS[@]}"} \
    ${EXTRA_CMAKE_ARGS[@]+"${EXTRA_CMAKE_ARGS[@]}"}
else
  echo "Ninja not found; falling back to Unix Makefiles." >&2
  USE_PRESET=0
  BUILD_DIR="${DEFAULT_BUILD_DIR}-make"

  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "Unix Makefiles" \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STATIC_LIBS=ON \
    -DBUILD_EXAMPLES=ON \
    -DBUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_LTO=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
    ${GPU_ARGS[@]+"${GPU_ARGS[@]}"} \
    ${EXTRA_CMAKE_ARGS[@]+"${EXTRA_CMAKE_ARGS[@]}"}
fi

echo "==> Building"
if [[ "${USE_PRESET}" -eq 1 ]]; then
  cmake --build --preset "${PRESET}" --parallel
else
  cmake --build "${BUILD_DIR}" --parallel
fi

echo "==> Running tests"
if [[ "${USE_PRESET}" -eq 1 ]]; then
  ctest --test-dir "${DEFAULT_BUILD_DIR}" --output-on-failure || {
    echo "Tests failed in ${DEFAULT_BUILD_DIR}" >&2
    exit 1
  }
else
  ctest --test-dir "${BUILD_DIR}" --output-on-failure || {
    echo "Tests failed in ${BUILD_DIR}" >&2
    exit 1
  }
fi

echo "Build completed successfully for preset ${PRESET} (GPU_MODE=${GPU_MODE}, BUILD_DIR=${BUILD_DIR})"
