# CMake Build Guide for poker-eval

This guide explains how to build poker-eval using the modern CMake build system.

## Prerequisites

- CMake 3.16 or later
- C99-compatible compiler (GCC, Clang, MSVC)
- Optional: OpenMP support for multithreading
- Optional: CUDA/OpenCL for GPU acceleration
- Optional: Python 3.x for Python bindings
- Optional: Java JDK for Java bindings

## Quick Start

### Unix-like Systems (Linux, macOS)

```bash
# Basic build
./build.sh

# Debug build
./build.sh --type Debug

# Build with specific features
./build.sh --gpu --coverage

# Using CMake presets
./build.sh --preset release
```

### Windows

```cmd
# Basic build
build.bat

# Debug build
build.bat --type Debug

# Using Visual Studio preset
build.bat --preset windows-msvc
```

### Manual CMake Commands

```bash
# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j

# Test
cd build && ctest

# Install
sudo cmake --install build
```

## Build Options

### Core Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SHARED_LIBS` | ON | Build shared libraries |
| `BUILD_STATIC_LIBS` | ON | Build static libraries |
| `BUILD_EXAMPLES` | ON | Build example programs |
| `BUILD_TESTS` | ON | Build test programs |
| `BUILD_BINDINGS` | ON | Build language bindings |
| `BUILD_GPU` | ON | Build GPU acceleration support |
| `BUILD_DOCUMENTATION` | OFF | Build Doxygen documentation |

### Feature Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_LTO` | ON | Enable Link Time Optimization |
| `ENABLE_COVERAGE` | OFF | Enable code coverage |
| `ENABLE_SANITIZERS` | OFF | Enable address/undefined sanitizers |
| `USE_FIVE_CARDS` | OFF | Use 5-card mode (default is 7) |

### Language Bindings

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_PYTHON_BINDING` | ON | Build Python bindings |
| `BUILD_JAVA_BINDING` | OFF | Build Java bindings |
| `BUILD_CSHARP_BINDING` | OFF | Build C# bindings |
| `BUILD_CXX_BINDING` | OFF | Build C++ bindings |

## CMake Presets

The project includes several predefined configurations in `CMakePresets.json`:

### General Presets

- `default` - Standard release build with all features
- `debug` - Debug build with coverage enabled
- `release` - Optimized release build
- `ci` - Configuration for continuous integration

### Platform-Specific Presets

- `windows-msvc` - Windows build with Visual Studio
- `macos` - macOS build with deployment target
- `linux-gcc` - Linux build with GCC
- `linux-clang` - Linux build with Clang

### Package Manager Presets

- `vcpkg` - Build with vcpkg integration
- `conan` - Build with Conan integration

## Package Manager Integration

### vcpkg

```bash
# Install poker-eval using vcpkg
vcpkg install poker-eval

# Use in your project
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

### Conan

```bash
# Install from conanfile.py
conan install . --build=missing

# Create package
conan create . poker-eval/1.0.0@

# Use in your project
conan install poker-eval/1.0.0@
```

## Advanced Usage

### Cross-Compilation

```bash
# Example for ARM64
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=toolchain-arm64.cmake \
    -DCMAKE_BUILD_TYPE=Release
```

### Custom Installation

```bash
# Install to custom prefix
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/opt/poker-eval
cmake --build build
sudo cmake --install build
```

### Building Specific Components

```bash
# Build only the library (no examples/tests)
cmake -B build -S . \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_BINDINGS=OFF

# Build only static library
cmake -B build -S . \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_STATIC_LIBS=ON
```

### GPU Acceleration

```bash
# Build with CUDA support
cmake -B build -S . -DBUILD_GPU=ON

# Verify GPU support
./build/examples/gpu_eval_example
```

### Code Coverage

```bash
# Build with coverage
./build.sh --type Debug --coverage

# Run tests
cd build && ctest

# Generate coverage report
make coverage-report

# View report
open coverage/html/index.html
```

## Integration in Your Project

### Using find_package

```cmake
find_package(poker-eval REQUIRED)
target_link_libraries(your_target PRIVATE poker-eval::poker-eval)
```

### Using FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    poker-eval
    GIT_REPOSITORY https://github.com/poker-eval/poker-eval.git
    GIT_TAG main
)
FetchContent_MakeAvailable(poker-eval)

target_link_libraries(your_target PRIVATE poker-eval::poker-eval)
```

### Using as Subdirectory

```cmake
add_subdirectory(poker-eval)
target_link_libraries(your_target PRIVATE poker-eval::poker-eval)
```

## Troubleshooting

### Common Issues

1. **CMake version too old**
   ```
   Error: CMake 3.16 or higher is required
   ```
   Solution: Update CMake from https://cmake.org/

2. **OpenMP not found**
   ```
   Warning: OpenMP not found
   ```
   Solution: Install OpenMP development packages:
   - Ubuntu/Debian: `sudo apt-get install libomp-dev`
   - macOS: `brew install libomp`
   - Windows: Included with Visual Studio

3. **GPU libraries not found**
   ```
   Warning: CUDA/OpenCL not found
   ```
   Solution: Install CUDA toolkit or OpenCL SDK

### Debug Build Issues

Enable verbose output:
```bash
./build.sh --verbose
# or
cmake --build build --verbose
```

Check CMake cache:
```bash
ccmake build  # Interactive cache editor
# or
cmake -LA build  # List all cache variables
```

## Migration from Autotools

If you're migrating from the old autotools build system:

1. The CMake build is a complete replacement - no need to run `./configure`
2. All the same features are available through CMake options
3. Installation paths follow standard CMake conventions
4. pkg-config files are automatically generated

### Equivalent Commands

| Autotools | CMake |
|-----------|-------|
| `./configure` | `cmake -B build -S .` |
| `./configure --prefix=/usr/local` | `cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr/local` |
| `make` | `cmake --build build` |
| `make check` | `cd build && ctest` |
| `make install` | `cmake --install build` |
| `make dist` | `cd build && cpack -G TGZ` |

## Contributing

When contributing to poker-eval:

1. Ensure your changes work with CMake
2. Update CMakeLists.txt files as needed
3. Test on multiple platforms if possible
4. Run the test suite: `cd build && ctest`
5. Check that examples still build and run

## Support

For build-related issues:
- Check this guide first
- Review CMakePresets.json for standard configurations
- Look at build.sh/build.bat for examples
- Open an issue on the project repository