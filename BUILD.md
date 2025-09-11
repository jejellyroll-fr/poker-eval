# Build Documentation

This document describes the build process for poker-eval, including supported architectures and how to build for different targets.

## Supported Architectures

The CI automatically builds poker-eval for the following platforms and architectures:

### Linux
- **AMD64 (x86-64)** - Standard 64-bit Intel/AMD processors
- **ARM64 (aarch64)** - 64-bit ARM processors (e.g., AWS Graviton, Apple Silicon under Linux)

### macOS
- **AMD64 (x86-64)** - Intel-based Macs
- **ARM64 (Apple Silicon)** - M1/M2/M3 Macs

### Windows
- **AMD64 (x86-64)** - Standard 64-bit Windows
- **ARM64 (aarch64)** - Windows on ARM (e.g., Surface Pro X, Snapdragon laptops)

### FreeBSD
- **AMD64 (x86-64)** - Standard 64-bit FreeBSD

## CI Build Process

The GitHub Actions CI automatically builds all supported architectures on:
- Push to `master` or `development` branches
- Pull requests targeting `master` or `development` branches
- Manual workflow dispatch
- Git tags (also creates releases)

### Generated Artifacts

Each successful CI run produces 7 artifacts:
- `poker-eval-linux-amd64.zip`
- `poker-eval-linux-arm64.zip`
- `poker-eval-darwin-amd64.zip`
- `poker-eval-darwin-arm64.zip`
- `poker-eval-windows-amd64.zip`
- `poker-eval-windows-arm64.zip`
- `poker-eval-freebsd-amd64.zip`

## Local Building

### Prerequisites

#### Linux
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install cmake make gcc

# For ARM64 cross-compilation
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

#### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Or install via Homebrew
brew install cmake make
```

#### Windows
- Visual Studio 2022 with C++ build tools
- CMake (can be installed via Visual Studio Installer or Chocolatey)

#### FreeBSD
```bash
pkg install cmake gmake gcc
```

### Building for Current Architecture

```bash
mkdir build
cd build
cmake ..
make        # Use 'gmake' on FreeBSD
```

### Cross-Compilation

#### Linux ARM64 from x86-64
```bash
mkdir build-arm64
cd build-arm64
cmake -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 ..
make
```

#### macOS Universal Binaries
```bash
# For x86-64
mkdir build-amd64
cd build-amd64
cmake -DCMAKE_OSX_ARCHITECTURES=x86_64 ..
make

# For ARM64 (Apple Silicon)
mkdir build-arm64
cd build-arm64
cmake -DCMAKE_OSX_ARCHITECTURES=arm64 ..
make
```

#### Windows ARM64
```powershell
mkdir build-arm64
cd build-arm64
cmake .. -G "Visual Studio 17 2022" -A ARM64
cmake --build .
```

### Additional Architectures

#### 32-bit Builds
For 32-bit x86 builds (where supported):

```bash
# Linux i386
cmake -DCMAKE_C_FLAGS="-m32" -DCMAKE_CXX_FLAGS="-m32" ..

# Windows x86
cmake .. -G "Visual Studio 17 2022" -A Win32
```

#### Other ARM Variants
```bash
# ARM v7 (32-bit)
cmake -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm ..
```

## Build Output

After a successful build, you'll find these executables:

### Core Executables
- `eval` - Poker hand evaluator
- `fish` - Fish (poker variant) evaluator
- `pokenum` - Poker enumeration tool
- `hcmp2` - Hand comparison (2 players)
- `hcmpn` - Hand comparison (N players)
- `seven_card_hands` - Seven card hand generator
- `usedecks` - Deck utilities

### Table Generation Tools
- `mktab_astud` - Asian stud tables
- `mktab_basic` - Basic lookup tables
- `mktab_evx` - Evaluation extension tables
- `mktab_joker` - Joker variant tables
- `mktab_lowball` - Lowball tables
- `mktab_packed` - Packed representation tables
- `mktab_short` - Short deck tables

### Test Executables
- `test_card` - Card handling tests
- `test_cardconverter` - Card conversion tests
- `test_handagnostichand` - Hand evaluation tests

### Libraries
- `libpoker_lib.so` / `libpoker_lib.dylib` / `poker_lib.dll` - Shared library
- `libpoker_lib_static.a` / `poker_lib_static.lib` - Static library

## Build Options

### CMake Options
- `USE_FIVE_CARDS` (default: ON) - Compile for 5-card dealt games
- `CMAKE_BUILD_TYPE` - Build type (Debug, Release, RelWithDebInfo, MinSizeRel)
- `CMAKE_INSTALL_PREFIX` - Installation directory

Example:
```bash
cmake -DUSE_FIVE_CARDS=OFF -DCMAKE_BUILD_TYPE=Release ..
```

### Configuration for 7-Card Games
```bash
cmake -DUSE_FIVE_CARDS=OFF ..
make
```

## Installation

```bash
make install
```

This installs:
- Executables to `${CMAKE_INSTALL_PREFIX}/bin`
- Libraries to `${CMAKE_INSTALL_PREFIX}/lib`

## Troubleshooting

### Common Issues

#### Missing Dependencies
Ensure all required development tools are installed for your platform.

#### Cross-Compilation Failures
- Verify cross-compilation toolchain is properly installed
- Check that target system headers are available
- Ensure CMake can find the correct compilers

#### Windows Build Issues
- Make sure Visual Studio Build Tools include the required components
- For ARM64 builds, ensure ARM64 build tools are installed
- Try running from "Developer Command Prompt" or "Developer PowerShell"

#### macOS Issues
- Install Xcode Command Line Tools: `xcode-select --install`
- For older macOS versions, you may need to adjust `CMAKE_OSX_DEPLOYMENT_TARGET`

### Getting Help

For build issues:
1. Check that all prerequisites are installed
2. Verify CMake can detect your compiler: `cmake --version` and `cmake --help`
3. Try a clean build: `rm -rf build && mkdir build && cd build`
4. Check the CMake configuration output for any warnings or errors

## Contributing

When adding support for new architectures:
1. Update the CI configuration in `.github/workflows/ci.yaml`
2. Test the build locally if possible
3. Update this documentation
4. Ensure cross-compilation works correctly
5. Verify the generated binaries have the correct architecture

For questions about specific architectures or build configurations, please open an issue on the GitHub repository.