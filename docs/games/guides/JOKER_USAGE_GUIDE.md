# User Guide for Games with Joker

## Overview

Joker game support is now fully integrated into pokenum. The joker is represented by "Xx" and is treated as the 53rd card in the deck.

## Supported Games

### 1. A-5 Lowball with joker (`-l`)
The most commonly used game with joker. The joker can substitute for any card to form the best possible low hand.

```bash
# Simple example
./pokenum -l 7h 5s 3d Xx - 9s 8h 6d 4c

# With more players
./pokenum -l Ac 2d - 5h 6s - Xx Kh
```

### 2. 5-card Draw Hi with joker (`-5d`)
The joker can substitute for any card to form the best high hand.

```bash
./pokenum -5d As Ah Xx - Ks Kh Kd
```

### 3. 5-card Draw Hi/Lo 8-or-better with joker (`-5d8`)
The joker can be used for high or low.

```bash
./pokenum -5d8 Ac 2c 3c - 8h 8d 8s
```

### 4. 5-card Draw Hi/Lo no qualifier with joker (`-5dnsq`)
Similar to the previous game, but without a low qualifier.

```bash
./pokenum -5dnsq 5h 5d - Xx 2c 3d
```

## Syntax

### Joker Representation
- Always use "Xx" (uppercase X followed by lowercase x)
- Valid examples: `Xx`, `xx`, `XX`, `xX` (all are accepted)

### General Format
```bash
./pokenum [options] <hand1> - <hand2> - ... [-- <board>] [/ <dead cards>]
```

### Useful Options
- `-mc <n>`: Use Monte Carlo with n iterations (recommended for complex cases)
- `-t`: Terse mode (single-line output)
- `-O`: Calculate ordering histogram

## Practical Examples

### 1. Classic Lowball
```bash
# Player 1 has a nearly complete hand, player 2 has the joker
./pokenum -l Ac 2c 3c 4c - Xx 6h 7h 8h
```

### 2. Multi-player Comparison
```bash
# 3 players, one with the joker
./pokenum -l Xx - Ac 2d 3h - 5s 6s 7s
```

### 3. With Monte Carlo (recommended for complex cases)
```bash
# 100,000 simulations
./pokenum -mc 100000 -l Xx 2h - 5s 6d - Ac Kh
```

### 4. Dead Cards
```bash
# King of hearts is dead
./pokenum -l Xx 2d 3h - 5s 6s 7s / Kh
```

## Important Notes

### Performance
- Exhaustive enumeration with a joker generates significantly more combinations (53 cards instead of 52)
- For cases with few fixed cards, Monte Carlo is preferred
- Example: with 2 players having 1 card each, exhaustive enumeration generates millions of combinations

### Joker Rules
- In A-5 lowball: the joker is always the best possible card to complete a low hand
- In high: the joker completes the best possible hand (straight flush, four of a kind, etc.)
- The joker cannot be duplicated (there is only one joker in the deck)

### Known Limitations
- Certain very wide combinations may cause timeouts
- Workaround: use `-mc` with a reasonable number of iterations

## Troubleshooting

### Problem: Timeout or freeze
Solution: Use Monte Carlo
```bash
# Instead of
./pokenum -l Xx - Ac

# Use
./pokenum -mc 100000 -l Xx - Ac
```

### Problem: "Joker enumeration not yet implemented"
Solution: Recompile with a clean build
```bash
rm -rf build
mkdir build
cd build
cmake ..
make -j4
```

## Example Results

### Lowball with Joker
```
5-card Draw A-5 Lowball with joker: 45540 enumerated outcomes
cards              win   %win      lose  %lose       tie   %tie        EV
5s 3d 7h Xx      19447  42.70     26093  57.30         0   0.00     0.427
9s 4c 6d 8h      26093  57.30     19447  42.70         0   0.00     0.573
```

The joker gives the first player a 7-5-3-2-A hand, but the second player with 9-8-6-4-x wins more often.