# Wrapper for Poker Evaluation Library (`pokereval.pyx`)

This project is a Cython-based wrapper for a poker evaluation library. It provides Python access to evaluate poker hands using a C library, which significantly improves performance for operations such as calculating the best hand or performing Monte Carlo simulations for EV (Expected Value) estimations.

## Features

### 1. Hand Evaluation Functions

- **Evaluate Hands**: Supports a wide range of poker variants, including Texas Hold'em, Omaha, Seven-Card Stud, Lowball, and Razz. Each hand can be evaluated for the best combination of cards and also evaluated for "low" hands where applicable (e.g., Omaha Hi/Lo).
  
- **Monte Carlo Simulations**: Perform simulations to calculate Expected Value (EV) over multiple iterations. This is particularly useful for complex games like Omaha or games with incomplete board information.

### 2. Deck Manipulation

- **Deck Reset and Shuffle**: Easily reset the deck and shuffle it, ensuring unbiased distribution of cards.
- **Random Card Selection**: Efficiently draw random cards from the deck without duplication, ensuring correct handling of hands, board, and dead cards.

### 3. Card Conversion

- **String to Card Number Conversion**: Convert string representations of cards (e.g., 'As', 'Kd') to internal numeric format for evaluation.
- **Card Number to String Conversion**: Convert numeric card representations back to human-readable string formats.

### 4. Best Hand Calculation

- **Best Hand Finder**: For any given hand and board, the wrapper can find the best 5-card combination according to the rules of the specific poker variant.
  
### 5. Low Hand Support

- **Low Hand Evaluation**: Support for evaluating low hands (e.g., in Omaha Hi/Lo or Razz).

### 6. Customizable Random Number Generator

- **RNG Control**: Custom RNG based on Python's `random` module, with functions to seed and control the random number generator.

## Building the Wrapper

To use the wrapper in your project, you'll need to compile the Cython file and link it with the C poker evaluation library.

### Prerequisites

- **Cython**: Make sure you have Cython installed. You can install it via pip:

  ```bash
  pip install cython
  ```

- **C Library**: The project requires the poker evaluation C library (`poker_defs.h`, `eval_omaha.h`, etc.) to be available on your system. These should be included in the project directory or installed in a location where the compiler can find them.

### Compiling with Cython

1. Create a `setup.py` file that will build the Cython extension:

```python
from setuptools import setup
from Cython.Build import cythonize

setup(
    ext_modules = cythonize("pokereval.pyx"),
    include_dirs=['/path/to/c_library/includes'],  # Update this path
    libraries=['poker_eval'],  # Link the necessary poker evaluation library
)
```

2. Run the `setup.py` script to build the extension:

   ```bash
   python setup.py build_ext --inplace
   ```

This will generate a shared object (`.so` file) that you can import directly into Python.

### Using the Wrapper

Once the Cython extension is built, you can use it in your Python code as follows:

```python
from pokereval import PokerEval

# Initialize the poker evaluator
poker_eval = PokerEval()

# Example: evaluate a Texas Hold'em hand
hand = ['As', 'Kd']  # Ace of spades, King of diamonds
board = ['Qh', 'Jh', 'Ts']  # Queen of hearts, Jack of hearts, Ten of spades
best_hand_value = poker_eval.best_hand_value('holdem', 'hi', hand, board)
print(f"Best hand value: {best_hand_value}")
```

## License

 AGPL-3.0 license

## Conclusion

This wrapper is designed to provide high-performance poker hand evaluations using C-based algorithms while providing the flexibility of Python for integration into various projects. It supports numerous poker variants and is optimized for both hand evaluation and Monte Carlo simulations.
