# Hand Distribution Feature

Hand distribution functionality for working with hand ranges in both Holdem and Omaha.

## Features

### Holdem Hand Distribution
- Parse hand ranges like "AK", "AKs", "AKo", "AA"
- Generate all possible hand combinations from a range
- Handle dead cards correctly

### Omaha Hand Distribution
- Parse Omaha hand patterns like:
  - "AAxx" - pair of aces with any two cards
  - "AAxxds" - double-suited aces
  - "AKQJds" - double-suited broadway
  - "AsKhQdJc" - specific hand
  - "AAAx" - trip aces with any card
- Support for suit properties:
  - ds (double-suited), ss (single-suited), ts (triple-suited)
  - qs (quad-suited/monotone), r/rb (rainbow)

### Test Results

#### Holdem
```
AKs generates 4 hands
AKo generates 12 hands
AK generates 16 hands
AA generates 6 hands
```

#### Omaha
```
AAxx generates 6768 hands (dynamic allocation, no fixed limit)
AAxxds generates 864 hands
AsKhQdJc generates 1 hand
AKQJds generates 36 hands
AAAx generates 192 hands
xxxx generates 270725 hands (C(52,4))
```

## Usage

### Holdem
```c
HandList hands;
HoldemAgnosticHand_Instantiate("AKs", "", &hands);
```

### Omaha
```c
OmahaHandList hands;
OmahaHandList_Init(&hands, 5000);
OmahaHand_Parse("AAxxds", &query);
OmahaHand_Instantiate(&query, dead, &hands);
OmahaHandList_Free(&hands);
```

## Building

```bash
cmake --build --preset debug
ctest --preset debug -R hand_distribution_example
```