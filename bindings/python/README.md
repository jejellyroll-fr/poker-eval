What is pypoker-eval ?  

This package is python adaptor for the poker-eval toolkit for writing
programs which simulate or analyze poker games as found at
http://gna.org/projects/pokersource/. The python interface is
somewhat simpler than the C API of poker-eval. It assumes that the
caller is willing to have a higher level API and is not interested in
a one to one mapping of the poker-eval API.

For more information about the functions provided, check the
documentation of the PokerEval class in the pokereval.py file.

Loic Dachary <loic@dachary.org>

## Prerequisites

### Windows

    Install Visual Studio (with C++ support).
    Install CMake.

### macOS and Linux

    Install CMake via package manager (e.g., Homebrew on macOS, apt on Ubuntu).

## Installation

### Windows

    Open a Command Prompt or PowerShell.

    Navigate to the pypoker-eval project directory.

    Create a build directory and navigate into it:

bash

```
mkdir build
cd build
```

Generate project files with CMake (replace {your version} with your version of Visual Studio):

bash

```
cmake .. -G "Visual Studio 17 2022"
```

Build the project:

bash

```
cmake --build .
```

### macOS and Linux

    Open a terminal.

    Navigate to the pypoker-eval project directory.

    Create a build directory and navigate into it:

bash

```
mkdir build
cd build
```

Generate the Makefiles and build the project:

bash

```
cmake ..
make
```









