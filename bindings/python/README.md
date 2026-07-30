# PyPoker-Eval (Python Bindings)

This package is a Python adapter for the `poker-eval` toolkit for writing programs which simulate or analyze poker games. The Python interface provides high-level abstractions for poker hand evaluation and equity calculations.

For details on the provided Python classes and functions, consult `pokereval.py`.

## Building and Installing Python Bindings

Python bindings can be built either via CMake from the repository root or installed directly using `pip`.

### Option 1: Install via pip

Navigate to the `bindings/python/` directory and run:

```bash
pip install .
```

For editable/development mode:

```bash
pip install -e .
```

### Option 2: Build with CMake from Repository Root

From the project root directory, configure CMake with `BUILD_BINDINGS=ON` and build:

```bash
mkdir -p build
cd build
cmake .. -DBUILD_BINDINGS=ON
cmake --build .
```

## Prerequisites

- Python 3.7+
- C compiler (GCC, Clang, or MSVC)
- CMake 3.15+ (if building via CMake)
