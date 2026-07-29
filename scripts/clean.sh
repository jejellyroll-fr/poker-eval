#!/bin/bash

echo "=== Poker-Eval Project Cleanup ==="
echo "Cleaning build artifacts and temporary files..."

# Remove build directory
if [ -d "build" ]; then
    echo "Removing build directory..."
    rm -rf build/
fi

# Remove legacy build directory if it exists
if [ -d "legacy" ]; then
    echo "Removing legacy build directory..."
    rm -rf legacy/
fi

# Remove object files
echo "Removing object files..."
find . -name "*.o" -delete
find . -name "*.obj" -delete

# Remove coverage files
echo "Removing coverage files..."
find . -name "*.gcda" -delete
find . -name "*.gcno" -delete
find . -name "*.gcov" -delete

# Remove debug files
echo "Removing debug files..."
find . -name "*.dSYM" -type d -exec rm -rf {} + 2>/dev/null || true
find . -name "core" -delete
find . -name "core.*" -delete

# Remove temporary files
echo "Removing temporary files..."
find . -name "*~" -delete
find . -name "*.tmp" -delete
find . -name "*.bak" -delete
find . -name ".DS_Store" -delete

# Remove CMake cache files
echo "Removing CMake cache files..."
find . -name "CMakeCache.txt" -delete
find . -name "CMakeFiles" -type d -exec rm -rf {} + 2>/dev/null || true
find . -name "cmake_install.cmake" -delete
find . -name "Makefile" -not -path "./tests/Makefile*" -delete

# Remove IDE files
echo "Removing IDE files..."
find . -name "*.vcxproj*" -delete
find . -name "*.sln" -delete
find . -name ".vscode" -type d -exec rm -rf {} + 2>/dev/null || true
find . -name ".idea" -type d -exec rm -rf {} + 2>/dev/null || true

# Remove package files
echo "Removing package files..."
find . -name "*.deb" -delete
find . -name "*.rpm" -delete
find . -name "*.tar.gz" -delete
find . -name "*.zip" -delete

echo "✅ Cleanup completed!"
echo ""
echo "Project is now clean and ready for:"
echo "  • Git commit/push"
echo "  • Fresh build (mkdir build && cd build && cmake .. && make)"
echo "  • Distribution packaging"
echo ""