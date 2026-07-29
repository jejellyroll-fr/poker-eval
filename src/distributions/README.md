# Module Distributions

Ce module gère les distributions de mains et les ranges :

## Composants

### Distributions de mains
- `hand_distributions.h/c` - Interface générale
- `omaha_distributions.h/c` - Distributions Omaha
- `stud_distributions.h/c` - Distributions Stud
- `holdem_distributions.h/c` - Distributions Hold'em

### Parsing et génération
- `range_parser.h/c` - Parsing des ranges de mains
- `hand_generator.h/c` - Génération de mains spécifiques

### Nomenclature
- `plo_nomenclature.h/c` - Nomenclature PLO
- `card_converter.h/c` - Conversion entre formats