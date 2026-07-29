#!/bin/bash
# Test combinatorial generator with sanitizers for maximum safety

set -e

echo "🛡️  Testing Combinatorial Generator with Sanitizers"
echo "=================================================="

# Save current directory
ORIGINAL_DIR=$(pwd)
BUILD_DIR="build_sanitizer_test"

# Clean and create sanitizer build
echo "Setting up sanitizer build..."
rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with sanitizers
echo "Configuring with AddressSanitizer and UBSan..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_SANITIZERS=ON \
    -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g"

# Build
echo "Building with sanitizers..."
make combo_validation_suite -j4

# Run the validation suite
echo ""
echo "🔍 Running validation suite with sanitizers..."
echo "============================================="
./examples/combo_validation_suite

echo ""
echo "✅ Sanitizer test completed successfully!"
echo "No memory leaks, undefined behavior, or address violations detected."

# Clean up
cd "$ORIGINAL_DIR"
# Uncomment to keep sanitizer build for investigation:
# rm -rf "$BUILD_DIR"

echo "🎉 Generator passes all sanitizer checks - truly \"béton\"!"