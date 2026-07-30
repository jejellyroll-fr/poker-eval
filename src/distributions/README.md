# Distributions Module

This module manages the generation and manipulation of hand distributions for various poker variants.

## Source Files (`src/distributions/`)

- `HoldemAgnosticHand.c` - Hand distribution generation implementation for Hold'em
- `holdem_distributions.c` - Complementary Hold'em distribution functions
- `omaha_distributions.c` - Omaha (PLO) hand distribution generation and instantiation
- `plo_integration.c` - Advanced integration and expansion of PLO patterns
- `plo_nomenclature.c` - Parsing and classification of PLO hand categories (21 categories)
- `stud_distributions.c` - Hand distribution generation for Stud variants (7-Card Stud, Razz)

## Headers (`include/poker_eval/distributions/`)

- `HoldemAgnosticHand.h` - Directives and prototypes for Hold'em distributions
- `card_converter.h` - Card conversion utility functions
- `hand_distributions.h` - General interface for hand distributions
- `holdem_distributions.h` - Header for Hold'em distributions
- `omaha_distributions.h` - Header and structures for Omaha distributions (`OmahaHandQuery`, `OmahaHandList`)
- `plo_integration.h` - Prototypes for PLO pattern integration
- `plo_nomenclature.h` - Structures and enums for PLO nomenclature (`PLOHandCategory`)
- `stud_distributions.h` - Header for Stud distributions