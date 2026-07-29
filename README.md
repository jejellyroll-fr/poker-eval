# poker-eval

`poker-eval` is a C/C++ poker evaluation library with a compatible Python
interface for applications that historically imported `pokereval.PokerEval`.
It supports Hold'em, Omaha, Stud, draw, lowball, mixed, and several extended
game variants.

## Build and test

Requirements:

- CMake 3.16 or newer
- A C99 and C++11 compiler
- Python 3.9 or newer when building the Python binding

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_GPU=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

GPU support is optional and can be enabled with `-DBUILD_GPU=ON` when the
required platform SDK is available.

Useful build switches:

| Option | Default | Purpose |
| --- | --- | --- |
| `BUILD_TESTS` | `ON` | Build the regression and integration tests |
| `BUILD_BINDINGS` | `ON` | Build language bindings |
| `BUILD_PYTHON_BINDING` | `ON` | Build `pypokereval` and `pokereval.py` |
| `BUILD_C_API` | `ON` | Build the stable C API |
| `BUILD_EXAMPLES` | `ON` | Build examples and command-line tools |
| `BUILD_GPU` | `ON` | Enable optional GPU support |

## Python installation

The repository is directly installable as a Python package:

```bash
python -m pip install .
```

This builds a native wheel containing both the `pypokereval` extension and the
`pokereval.PokerEval` compatibility wrapper.

```python
from pokereval import PokerEval

evaluator = PokerEval()
result = evaluator.poker_eval(
    game="holdem",
    pockets=[["As", "Ah"], ["Ks", "Kh"]],
    board=["2c", "3d", "4h", "5s", "9c"],
    dead=[],
)

print(result["eval"][0]["ev"])  # 1000
print(
    evaluator.winners(
        game="holdem",
        pockets=[["As", "Ah"], ["Ks", "Kh"]],
        board=["2c", "3d", "4h", "5s", "9c"],
        dead=[],
    )
)  # {"hi": [0]}
```

The wrapper retains the methods used by fpdb, including `poker_eval`, `best`,
`card2string`, and `winners`.

## CMake installation

```bash
cmake --install build --prefix /desired/prefix
```

Consumers can then use the exported CMake package:

```cmake
find_package(poker-eval CONFIG REQUIRED)
```

The public headers are under `include/poker_eval`. API and game-specific usage
guides are kept under `docs/`.

## Repository layout

```text
include/       Public headers
src/           Core library implementation
bindings/      C, Python, and optional language bindings
tests/         Regression and integration tests
examples/      Focused usage examples
docs/          User and API guides
scripts/       Build, validation, and benchmark helpers
```

## License

See `LICENCE` for the historical GPLv3-or-later licensing record and `LICENSE`
for the terms covering newer project material.
