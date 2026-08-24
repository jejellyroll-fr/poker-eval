# Minimal Emscripten toolchain profile for the portable core and v3 solver.
# Invoke with: emcmake cmake -S . -B build-wasm -DCMAKE_TOOLCHAIN_FILE=cmake/wasm.cmake
# The desktop GPU, OpenMP, zstd and native bindings are intentionally disabled;
# the resulting static library is the supported WASM embedding boundary.

set(CMAKE_SYSTEM_NAME Emscripten)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_GPU OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENMP OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
set(ENABLE_NATIVE_ARCH OFF CACHE BOOL "" FORCE)

# Keep the module usable from JavaScript without forcing an application-level
# main() into the library. Embedders can add their own EXPORTED_FUNCTIONS.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -sWASM_BIGINT=1" CACHE STRING "" FORCE)
