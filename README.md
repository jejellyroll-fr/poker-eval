# Poker-Eval Library

poker-eval is a library designed for evaluating poker hands and supporting various poker games. It offers a powerful and flexible API for hand odds calculation and comparison across many poker variants.

## Features

- Supports multiple poker deck, including:
  - Standard Deck (52 cards)
  - Asian stud Deck (32 cards)
  - Joker Deck(53 cards)
  - Short Deck (36 cards)
- Lookup table generation for optimized evaluations.
- Tools for odds calculation and game simulation.
- Fast and efficient poker hand evaluation.

## Supported Games

- Holdem Hi
- Holdem Hi/Low 8-or-better
- Omaha Hi
- Omaha Hi 5cards
- Omaha Hi 6cards
- Omaha Hi/Low 8-or-better
- Omaha 5cards Hi/Low 8-or-better
- 7-card Stud Hi
- 7-card Stud Hi/Low 8-or-better
- 7-card Stud Hi/Low no qualifier
- 7-card Stud A-5 Low
- 5-card Draw Hi with joker
- 5-card Draw Hi/Low 8-or-better with joker
- 5-card Draw Hi/Low no qualifier with joker
- 5-card Draw A-5 Lowball with joker
- 5-card Draw 2-7 Lowball
- ShortDeck Holdem NL Hi

## Prerequisites

### Windows

    Install Visual Studio (with C++ support).
    Install CMake.

### macOS and Linux

    Install CMake via package manager (e.g., Homebrew on macOS, apt on Ubuntu).

## Installation

### Windows

    Open a Command Prompt or PowerShell.

    Navigate to the poker-eval project directory.

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

    Navigate to the poker-eval project directory.

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

## Usage

After installation, include poker-eval headers in your project and link your application with the poker_lib library (either static or shared depending on your setup).

Refer to the API documentation and provided examples for more details on using poker-eval.

## CI github

[![Build and Package](https://github.com/jejellyroll-fr/poker-eval/actions/workflows/ci.yaml/badge.svg?branch=master&event=push)](https://github.com/jejellyroll-fr/poker-eval/actions/workflows/ci.yaml)

## Known bugs

- enumerate and evaluate joker games
- wrong counting scoops on hi-lo games