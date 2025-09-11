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

### Basic Installation

```bash
make install
```

This installs:
- Executables to `${CMAKE_INSTALL_PREFIX}/bin`
- Libraries to `${CMAKE_INSTALL_PREFIX}/lib`

### Installing on Target Systems

#### Using Pre-built Artifacts

1. **Download the appropriate artifact** for your target architecture from the GitHub releases or CI artifacts
2. **Extract the archive** to a temporary directory
3. **Copy files to system directories**:

##### Linux/FreeBSD
```bash
# Extract artifact
unzip poker-eval-linux-amd64.zip
cd poker-eval-linux-amd64

# Install system-wide (requires root)
sudo mkdir -p /usr/local/bin /usr/local/lib /usr/local/include
sudo cp eval fish pokenum hcmp2 hcmpn seven_card_hands usedecks /usr/local/bin/
sudo cp libpoker_lib.so /usr/local/lib/
sudo cp -r ../include/* /usr/local/include/

# Update library cache
sudo ldconfig

# Or install to user directory
mkdir -p $HOME/.local/bin $HOME/.local/lib
cp eval fish pokenum hcmp2 hcmpn seven_card_hands usedecks $HOME/.local/bin/
cp libpoker_lib.so $HOME/.local/lib/
export PATH="$HOME/.local/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
```

##### macOS
```bash
# Extract artifact
unzip poker-eval-darwin-amd64.zip
cd poker-eval-darwin-amd64

# Install system-wide (requires admin)
sudo mkdir -p /usr/local/bin /usr/local/lib /usr/local/include
sudo cp eval fish pokenum hcmp2 hcmpn seven_card_hands usedecks /usr/local/bin/
sudo cp libpoker_lib.dylib /usr/local/lib/
sudo cp -r ../include/* /usr/local/include/

# Or install to user directory
mkdir -p $HOME/.local/bin $HOME/.local/lib
cp eval fish pokenum hcmp2 hcmpn seven_card_hands usedecks $HOME/.local/bin/
cp libpoker_lib.dylib $HOME/.local/lib/
export PATH="$HOME/.local/bin:$PATH"
export DYLD_LIBRARY_PATH="$HOME/.local/lib:$DYLD_LIBRARY_PATH"
```

##### Windows
```powershell
# Extract artifact
Expand-Archive poker-eval-windows-amd64.zip
cd poker-eval-windows-amd64

# Install system-wide (requires Administrator)
# Create directories if they don't exist
New-Item -ItemType Directory -Force -Path "C:\Program Files\PokerEval\bin"
New-Item -ItemType Directory -Force -Path "C:\Program Files\PokerEval\lib"
New-Item -ItemType Directory -Force -Path "C:\Program Files\PokerEval\include"

# Copy files
Copy-Item *.exe "C:\Program Files\PokerEval\bin\"
Copy-Item poker_lib.dll "C:\Program Files\PokerEval\lib\"
Copy-Item ..\include\* "C:\Program Files\PokerEval\include\" -Recurse

# Add to system PATH (requires restart or logout/login)
# Add "C:\Program Files\PokerEval\bin" to your system PATH environment variable

# Or install to user directory
$userPath = "$env:USERPROFILE\.local"
New-Item -ItemType Directory -Force -Path "$userPath\bin"
New-Item -ItemType Directory -Force -Path "$userPath\lib"
Copy-Item *.exe "$userPath\bin\"
Copy-Item poker_lib.dll "$userPath\lib\"

# Add to user PATH
$env:PATH = "$userPath\bin;$env:PATH"
```

#### Using Package Managers (if available)

##### Linux - Create .deb Package
```bash
# Install packaging tools
sudo apt-get install build-essential devscripts debhelper

# Create package structure
mkdir -p poker-eval-1.0/DEBIAN
mkdir -p poker-eval-1.0/usr/local/bin
mkdir -p poker-eval-1.0/usr/local/lib
mkdir -p poker-eval-1.0/usr/local/include

# Copy files
cp build/* poker-eval-1.0/usr/local/bin/
cp *.so poker-eval-1.0/usr/local/lib/
cp -r include/* poker-eval-1.0/usr/local/include/

# Create control file
cat > poker-eval-1.0/DEBIAN/control << EOF
Package: poker-eval
Version: 1.0
Section: games
Priority: optional
Architecture: amd64
Maintainer: Your Name <your.email@example.com>
Description: Poker hand evaluation library
 A library for evaluating poker hands and related utilities.
EOF

# Build package
dpkg-deb --build poker-eval-1.0
sudo dpkg -i poker-eval-1.0.deb
```

##### macOS - Using Homebrew
```bash
# Create a Homebrew formula (for local tap)
# This is for advanced users who want to create their own tap
```

##### Windows - Using Chocolatey
```powershell
# Advanced: Create a Chocolatey package
# This requires creating a .nuspec file and packaging scripts
```

### Development Integration

#### CMake Integration
To use poker-eval in your CMake project:

```cmake
# Find the library
find_library(POKER_EVAL_LIB poker_lib PATHS /usr/local/lib ~/.local/lib)
find_path(POKER_EVAL_INCLUDE poker_defs.h PATHS /usr/local/include ~/.local/include)

# Link to your target
target_link_libraries(your_target ${POKER_EVAL_LIB})
target_include_directories(your_target PRIVATE ${POKER_EVAL_INCLUDE})
```

#### pkg-config Integration
Create a pkg-config file for easier integration:

```bash
# Create poker-eval.pc
sudo tee /usr/local/lib/pkgconfig/poker-eval.pc << EOF
prefix=/usr/local
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: poker-eval
Description: Poker hand evaluation library
Version: 1.0
Libs: -L\${libdir} -lpoker_lib
Cflags: -I\${includedir}
EOF

# Use in your project
pkg-config --cflags --libs poker-eval
```

#### Docker Integration
Create a Docker image with poker-eval pre-installed:

```dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    cmake make gcc \
    && rm -rf /var/lib/apt/lists/*

# Copy and build poker-eval
COPY . /poker-eval
WORKDIR /poker-eval
RUN mkdir build && cd build && \
    cmake .. && make && make install

# Set library path
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

CMD ["eval"]
```

### Verification

After installation, verify it works:

```bash
# Test executables
eval --help
pokenum --help

# Test library (C example)
cat > test.c << EOF
#include <stdio.h>
#include "poker_defs.h"

int main() {
    printf("Poker-eval library loaded successfully!\n");
    return 0;
}
EOF

gcc -o test test.c -lpoker_lib
./test
```

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