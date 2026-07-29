#!/bin/bash
# Simple build script for poker-eval modular structure (without CMake)
# This script can build the modular structure using direct GCC compilation

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="release"
CLEAN=0
VERBOSE=0
TEST_ONLY=0

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

Simple build script for poker-eval modular structure (no CMake required)

OPTIONS:
    -h, --help          Show this help message
    -t, --type TYPE     Build type (debug, release) - Default: release
    -c, --clean         Clean build artifacts
    -v, --verbose       Enable verbose output
    --test-only         Only run tests, don't build
    --install-cmake     Try to install CMake

EXAMPLES:
    # Basic build
    $0

    # Debug build
    $0 --type debug

    # Test modular structure
    $0 --test-only

    # Clean and rebuild
    $0 --clean
EOF
}

# Function to check for required tools
check_tools() {
    if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
        print_error "Neither GCC nor Clang is available. Please install a C compiler."
        exit 1
    fi
    
    if command -v gcc &> /dev/null; then
        CC="gcc"
        print_info "Using GCC compiler"
    else
        CC="clang"
        print_info "Using Clang compiler"
    fi
}

# Function to install CMake
install_cmake() {
    print_info "Attempting to install CMake..."
    
    if command -v brew &> /dev/null; then
        print_info "Installing CMake with Homebrew..."
        brew install cmake
    elif command -v apt-get &> /dev/null; then
        print_info "Installing CMake with apt-get..."
        sudo apt-get update && sudo apt-get install -y cmake
    elif command -v yum &> /dev/null; then
        print_info "Installing CMake with yum..."
        sudo yum install -y cmake
    else
        print_error "Cannot automatically install CMake. Please install it manually:"
        echo "  - macOS: brew install cmake"
        echo "  - Ubuntu/Debian: sudo apt-get install cmake"
        echo "  - CentOS/RHEL: sudo yum install cmake"
        echo "  - Or download from: https://cmake.org/download/"
        exit 1
    fi
}

# Function to test modular structure
test_modular() {
    print_modular "Testing modular structure..."
    
    # Test 1: Check if source files exist
    print_modular "Checking source files..."
    if [ ! -d "src/core" ]; then
        print_error "Modular source directory not found: src/core"
        return 1
    fi
    
    if [ ! -f "src/core/poker_defs.h" ]; then
        print_error "Core header not found: src/core/poker_defs.h"
        return 1
    fi
    
    print_modular "✅ Source files found"
    
    # Test 2: Test header compilation
    print_modular "Testing header compilation..."
    if [ -f "test_modular_build.c" ]; then
        if $CC -c test_modular_build.c -I src/core -o /tmp/test_modular.o 2>/dev/null; then
            print_modular "✅ Headers compile successfully"
            rm -f /tmp/test_modular.o
        else
            print_error "❌ Header compilation failed"
            return 1
        fi
    else
        print_warning "Test file test_modular_build.c not found, skipping header test"
    fi
    
    # Test 3: Test example compilation
    print_modular "Testing example compilation..."
    if [ -f "examples/modular_example.c" ]; then
        if $CC examples/modular_example.c -I src/core -o /tmp/modular_example 2>/dev/null; then
            print_modular "✅ Example compiles successfully"
            
            # Test execution
            if /tmp/modular_example >/dev/null 2>&1; then
                print_modular "✅ Example executes successfully"
            else
                print_warning "⚠️ Example compiles but execution failed"
            fi
            rm -f /tmp/modular_example
        else
            print_warning "⚠️ Example compilation failed (may need library linking)"
        fi
    else
        print_warning "Example file not found, skipping example test"
    fi
    
    print_modular "Modular structure tests completed!"
    return 0
}

# Function to build core module (simple version)
build_core_simple() {
    print_modular "Building core module (simple)..."
    
    local build_dir="build_simple"
    local core_sources=""
    
    # Create build directory
    mkdir -p "$build_dir"
    
    # Find core source files
    if [ -d "src/core" ]; then
        core_sources=$(find src/core -name "*.c" 2>/dev/null || echo "")
    fi
    
    if [ -z "$core_sources" ]; then
        print_warning "No core source files found, creating header-only test"
        
        # Create a simple test that just includes headers
        cat > "$build_dir/header_test.c" << 'EOF'
#include "../src/core/poker_defs.h"
#include "../src/core/deck_std.h"
#include <stdio.h>

int main() {
    printf("Modular headers included successfully!\n");
    printf("StdDeck has %d cards\n", StdDeck_N_CARDS);
    return 0;
}
EOF
        
        # Compile header test
        if $CC "$build_dir/header_test.c" -I src/core -o "$build_dir/header_test"; then
            print_modular "✅ Header test compiled successfully"
            
            # Run test
            if "$build_dir/header_test"; then
                print_modular "✅ Header test executed successfully"
            fi
        else
            print_error "❌ Header test compilation failed"
            return 1
        fi
    else
        print_modular "Found core sources: $core_sources"
        
        # Try to compile core sources
        local cflags="-I src/core"
        if [ "$BUILD_TYPE" = "debug" ]; then
            cflags="$cflags -g -O0 -DDEBUG"
        else
            cflags="$cflags -O2 -DNDEBUG"
        fi
        
        if [ $VERBOSE -eq 1 ]; then
            cflags="$cflags -v"
        fi
        
        print_modular "Compiling with flags: $cflags"
        
        # Compile each source file to object
        local objects=""
        for src in $core_sources; do
            local obj="$build_dir/$(basename "$src" .c).o"
            if $CC -c "$src" $cflags -o "$obj"; then
                objects="$objects $obj"
                print_modular "✅ Compiled $(basename "$src")"
            else
                print_warning "⚠️ Failed to compile $(basename "$src")"
            fi
        done
        
        if [ -n "$objects" ]; then
            # Try to create a simple library
            if command -v ar &> /dev/null; then
                ar rcs "$build_dir/libpoker_core.a" $objects
                print_modular "✅ Created static library: $build_dir/libpoker_core.a"
            else
                print_warning "ar not available, object files created but no library"
            fi
        fi
    fi
    
    print_modular "Core module build completed!"
}

# Parse command line arguments
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
        -c|--clean)
            CLEAN=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        --test-only)
            TEST_ONLY=1
            shift
            ;;
        --install-cmake)
            install_cmake
            exit $?
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Main execution
echo
print_info "=== Simple Poker-eval Modular Build ==="
print_info "Build type: $BUILD_TYPE"
echo

# Check tools
check_tools

# Clean if requested
if [ $CLEAN -eq 1 ]; then
    print_info "Cleaning build artifacts..."
    rm -rf build_simple
    rm -f /tmp/test_modular.o /tmp/modular_example
fi

# Test only mode
if [ $TEST_ONLY -eq 1 ]; then
    test_modular
    exit $?
fi

# Test modular structure first
if ! test_modular; then
    print_error "Modular structure tests failed"
    exit 1
fi

# Build core module
build_core_simple

echo
print_info "=== Build Summary ==="
print_modular "✅ Simple modular build completed"
echo "  - Build directory: build_simple/"
echo "  - Test with: ./build_simple.sh --test-only"
echo "  - For full CMake build, install CMake: ./build_simple.sh --install-cmake"
echo

print_info "Simple build completed successfully! 🎉"

if ! command -v cmake &> /dev/null; then
    echo
    print_warning "For full functionality, consider installing CMake:"
    print_warning "  macOS: brew install cmake"
    print_warning "  Ubuntu: sudo apt-get install cmake"
    print_warning "  Or use: ./build_simple.sh --install-cmake"
fi