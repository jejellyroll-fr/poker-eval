#!/bin/bash
# Build script for poker-eval using CMake
# Supports both legacy and new modular structure

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="Release"
BUILD_DIR="build"
PRESET=""
CLEAN=0
PARALLEL=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# New modular structure options
BUILD_MODULAR=0
BUILD_LEGACY=1
BUILD_BOTH=0
MODULAR_ONLY=0

# Function to print colored output
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_modular() {
    echo -e "${BLUE}[MODULAR]${NC} $1"
}

# Function to show usage
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Build poker-eval library using CMake
Supports both legacy and new modular structure

OPTIONS:
    -h, --help          Show this help message
    -t, --type TYPE     Build type (Debug, Release, RelWithDebInfo, MinSizeRel)
                        Default: Release
    -d, --dir DIR       Build directory (default: build)
    -p, --preset PRESET Use CMake preset (e.g., default, debug, release, ci)
    -c, --clean         Clean build directory before building
    -v, --verbose       Enable verbose output
    -j, --jobs N        Number of parallel jobs (default: auto-detect)
    --compiler COMP     Choose compiler (gcc or clang)
                        Note: GPU support is automatically disabled with clang
                        due to CUDA compatibility issues

STRUCTURE OPTIONS:
    --modular           Build new modular structure only
    --legacy            Build legacy structure only (default)
    --both              Build both legacy and modular structures
    --test-modular      Test modular structure compilation

LIBRARY OPTIONS:
    --shared            Build shared libraries only
    --static            Build static libraries only
    --no-tests          Disable building tests
    --no-examples       Disable building examples
    --gpu               Enable GPU acceleration support
    --coverage          Enable code coverage
    --sanitizers        Enable sanitizers (debug builds)
    --lto               Enable Link Time Optimization
    --five-cards        Use five cards mode (default is 7)

EXAMPLES:
    # Basic release build (legacy)
    $0

    # Build new modular structure
    $0 --modular

    # Build both structures
    $0 --both

    # Test modular structure
    $0 --test-modular

    # Debug build with coverage (modular)
    $0 --modular --type Debug --coverage

    # Build using preset
    $0 --preset ci

    # Clean build with GPU support (both structures)
    $0 --both --clean --gpu
EOF
}

# Function to build modular structure
build_modular() {
    print_modular "Building new modular structure..."
    
    local modular_build_dir="$BUILD_DIR/modular"
    
    if [ $CLEAN -eq 1 ]; then
        print_info "Cleaning modular build directory: $modular_build_dir"
        rm -rf "$modular_build_dir"
    fi
    
    mkdir -p "$modular_build_dir"
    
    print_modular "Configuring modular structure..."
    cd "$modular_build_dir"
    cmake ../../src -DCMAKE_BUILD_TYPE="$BUILD_TYPE" $CMAKE_ARGS
    
    print_modular "Building modular structure with $PARALLEL parallel jobs..."
    make -j "$PARALLEL"
    
    cd ../..
    print_modular "Modular structure build completed!"
}

# Function to test modular structure
test_modular() {
    print_modular "Testing modular structure..."
    
    # Test basic compilation
    print_modular "Testing basic header compilation..."
    if gcc -c test_modular_build.c -I src/core -o /tmp/test_modular.o 2>/dev/null; then
        print_modular "✅ Headers compile successfully"
        rm -f /tmp/test_modular.o
    else
        print_error "❌ Header compilation failed"
        return 1
    fi
    
    # Test full example
    print_modular "Testing full example compilation..."
    if gcc test_modular_build.c -I src/core -o /tmp/test_modular_exec 2>/dev/null; then
        print_modular "✅ Full example compiles successfully"
        
        # Test execution
        if /tmp/test_modular_exec >/dev/null 2>&1; then
            print_modular "✅ Example executes successfully"
        else
            print_warning "⚠️ Example compilation succeeded but execution failed"
        fi
        rm -f /tmp/test_modular_exec
    else
        print_error "❌ Full example compilation failed"
        return 1
    fi
    
    # Test modular build if exists
    if [ -d "src/build" ]; then
        print_modular "Testing modular build system..."
        cd src/build
        if make -j 1 >/dev/null 2>&1; then
            print_modular "✅ Modular build system works"
        else
            print_warning "⚠️ Modular build system has issues"
        fi
        cd ../..
    fi
    
    print_modular "Modular structure tests completed!"
}

# Function to build legacy structure
build_legacy() {
    print_info "Building legacy structure..."
    
    local legacy_build_dir="$BUILD_DIR/legacy"
    
    if [ $CLEAN -eq 1 ]; then
        print_info "Cleaning legacy build directory: $legacy_build_dir"
        rm -rf "$legacy_build_dir"
    fi
    
    mkdir -p "$legacy_build_dir"
    
    print_info "Configuring legacy structure..."
    if [ -n "$PRESET" ]; then
        print_info "Using preset: $PRESET"
        cmake --preset "$PRESET" $CMAKE_ARGS
    else
        cd "$legacy_build_dir"
        cmake ../.. -DCMAKE_BUILD_TYPE="$BUILD_TYPE" $CMAKE_ARGS
        cd ../..
    fi
    
    print_info "Building legacy structure with $PARALLEL parallel jobs..."
    if [ -n "$PRESET" ]; then
        cmake --build --preset "$PRESET" -j "$PARALLEL"
    else
        cmake --build "$legacy_build_dir" -j "$PARALLEL"
    fi
    
    print_info "Legacy structure build completed!"
}

# Parse command line arguments
CMAKE_ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -d|--dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -p|--preset)
            PRESET="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN=1
            shift
            ;;
        -v|--verbose)
            CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_VERBOSE_MAKEFILE=ON"
            shift
            ;;
        -j|--jobs)
            PARALLEL="$2"
            shift 2
            ;;
        --compiler)
            COMPILER="$2"
            shift 2
            ;;
        --modular)
            BUILD_MODULAR=1
            BUILD_LEGACY=0
            MODULAR_ONLY=1
            shift
            ;;
        --legacy)
            BUILD_LEGACY=1
            BUILD_MODULAR=0
            shift
            ;;
        --both)
            BUILD_BOTH=1
            BUILD_LEGACY=1
            BUILD_MODULAR=1
            shift
            ;;
        --test-modular)
            test_modular
            exit $?
            ;;
        --shared)
            CMAKE_ARGS="$CMAKE_ARGS -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF"
            shift
            ;;
        --static)
            CMAKE_ARGS="$CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON"
            shift
            ;;
        --no-tests)
            CMAKE_ARGS="$CMAKE_ARGS -DBUILD_TESTS=OFF"
            shift
            ;;
        --no-examples)
            CMAKE_ARGS="$CMAKE_ARGS -DBUILD_EXAMPLES=OFF"
            shift
            ;;
        --gpu)
            CMAKE_ARGS="$CMAKE_ARGS -DBUILD_GPU=ON"
            shift
            ;;
        --coverage)
            CMAKE_ARGS="$CMAKE_ARGS -DENABLE_COVERAGE=ON"
            shift
            ;;
        --sanitizers)
            CMAKE_ARGS="$CMAKE_ARGS -DENABLE_SANITIZERS=ON"
            shift
            ;;
        --lto)
            CMAKE_ARGS="$CMAKE_ARGS -DENABLE_LTO=ON"
            shift
            ;;
        --five-cards)
            CMAKE_ARGS="$CMAKE_ARGS -DUSE_FIVE_CARDS=ON"
            shift
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Set compiler if requested
if [ -n "$COMPILER" ]; then
    if [ "$COMPILER" = "clang" ]; then
        export CC=clang
        export CXX=clang++
        print_info "Using Clang as compiler"
        # Disable GPU support for Clang due to CUDA compatibility issues
        if [[ $CMAKE_ARGS != *"BUILD_GPU=OFF"* ]]; then
            CMAKE_ARGS="$CMAKE_ARGS -DBUILD_GPU=OFF"
            print_warning "GPU support disabled for Clang due to CUDA compatibility issues"
        fi
    elif [ "$COMPILER" = "gcc" ]; then
        export CC=gcc
        export CXX=g++
        print_info "Using GCC as compiler"
    else
        print_error "Unknown compiler: $COMPILER (use 'gcc' or 'clang')"
        exit 1
    fi
fi

# Check for required tools
if ! command -v cmake &> /dev/null; then
    print_error "CMake is not installed. Please install CMake 3.16 or later."
    exit 1
fi

# Check CMake version
CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d. -f1)
CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d. -f2)

if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 16 ]); then
    print_error "CMake 3.16 or later is required. Found version $CMAKE_VERSION"
    exit 1
fi

# Show build configuration
echo
print_info "=== Poker-eval Build Configuration ==="
print_info "Build type: $BUILD_TYPE"
print_info "Build directory: $BUILD_DIR"
print_info "Parallel jobs: $PARALLEL"

if [ $BUILD_BOTH -eq 1 ]; then
    print_info "Building: Both legacy and modular structures"
elif [ $BUILD_MODULAR -eq 1 ]; then
    print_modular "Building: Modular structure only"
else
    print_info "Building: Legacy structure only"
fi

if [ -n "$CMAKE_ARGS" ]; then
    print_info "CMake arguments: $CMAKE_ARGS"
fi
echo

# Create main build directory
mkdir -p "$BUILD_DIR"

# Build based on options
if [ $BUILD_LEGACY -eq 1 ] && [ $BUILD_MODULAR -eq 0 ]; then
    # Legacy only
    build_legacy
elif [ $BUILD_MODULAR -eq 1 ] && [ $BUILD_LEGACY -eq 0 ]; then
    # Modular only
    build_modular
elif [ $BUILD_BOTH -eq 1 ]; then
    # Both structures
    build_legacy
    echo
    build_modular
fi

echo
print_info "=== Build Summary ==="

if [ $BUILD_LEGACY -eq 1 ]; then
    print_info "✅ Legacy structure built successfully"
    echo "  - Location: $BUILD_DIR/legacy"
    echo "  - Run tests: cd $BUILD_DIR/legacy && ctest"
    echo "  - Install: cd $BUILD_DIR/legacy && sudo make install"
    echo "  - Run examples: cd $BUILD_DIR/legacy/examples && ./pokenum"
fi

if [ $BUILD_MODULAR -eq 1 ]; then
    print_modular "✅ Modular structure built successfully"
    echo "  - Location: $BUILD_DIR/modular"
    echo "  - Test headers: gcc test_modular_build.c -I src/core"
    echo "  - Run example: gcc examples/modular_example.c -I src/core"
    echo "  - Use modules: Include from src/core, src/games, etc."
fi

# Show coverage commands if enabled
if [[ $CMAKE_ARGS == *"ENABLE_COVERAGE=ON"* ]]; then
    echo "  - Generate coverage: cd $BUILD_DIR && make coverage-report"
fi

echo
print_info "Build completed successfully! 🎉"

if [ $MODULAR_ONLY -eq 1 ]; then
    echo
    print_modular "=== Modular Structure Usage ==="
    print_modular "The new modular structure is ready to use!"
    print_modular "See MIGRATION_GUIDE_MODULAR_COMPLETE.md for usage examples"
    print_modular "Test with: ./build.sh --test-modular"
fi
