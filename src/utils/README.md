# Utils Module

This module contains utilities, helper functions, table management, and auxiliary calculators for the library.

## Source Files (`src/utils/`)

### Aligned and Compressed Tables
- `aligned_tables.c` - Memory-aligned evaluation tables
- `aligned_tables_dynamic.c` - Dynamic allocation and management of aligned tables
- `compressed_tables.c` - Compressed evaluation tables

### Table Generation & Preflop
- `generate_preflop_table.c` - Preflop equity table generation
- `mktable.c` - Main program/tool for creating lookup tables
- `mktable_utils.c` - Utilities and helper functions for table generation (`mktable`)
- `mktab_astud.c` - Table generation for Asian Stud
- `mktab_basic.c` - Basic table generation for standard games
- `mktab_evx.c` - Table generation for EVX algorithms
- `mktab_joker.c` - Table generation with Joker support
- `mktab_lowball.c` - Table generation for Lowball / Razz
- `mktab_packed.c` - Packed format table generation
- `mktab_short.c` - Table generation for Short Deck

### Utilities, Calculators & Wrappers
- `combinations.c` - Combination calculations and indexing
- `icm_calculator.c` - Independent Chip Model (ICM) tournament calculator
- `poker_eval_modern.c` - Modern evaluation interface and helpers
- `poker_wrapper.c` - High-level C wrappers for the library
- `universal_deck.c` - Universal deck utility implementation

### Static Tables & Auxiliary Generators (`src/utils/tables/`)
- `tables/t_botfivecardsj.c`
- `tables/t_evx_flushcards.c`
- `tables/t_evx_pairval.c`
- `tables/t_evx_strval.c`
- `tables/t_toptwobits.c`
- `tables/t_topfivebits.c`
- `tables/t_topbit.c`
- `tables/t_nbitsandstr.c`
- `tables/t_maskrank.c`
- `tables/t_jokerstraight.c`
- `tables/t_evx_tripsval.c`