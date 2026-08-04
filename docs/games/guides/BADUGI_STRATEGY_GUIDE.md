# Badugi Strategy Guide with pokenum

## 📖 Introduction

This guide explains how to use `pokenum` to analyze card-drawing strategies in Badugi. Badugi is a lowball draw poker game where the objective is to form the best 4-card hand with unique ranks and suits.

## 🎯 Badugi Rules Recap

- **Objective**: 4-card hand with unique ranks and suits
- **Ranking**: Lower = better (A-2-3-4 is the nuts)
- **Hierarchy**: 4-card > 3-card > 2-card > 1-card
- **Draws**: Up to 3 draw rounds, each player can exchange 0-4 cards

## 🚀 Basic Usage

```bash
# General format
./pokenum -badugi <player1_hand> - <player2_hand> [options]

# Monte Carlo (recommended for incomplete hands)
./pokenum -mc 100000 -badugi <cards> - <cards>
```

## 📊 Pre-Draw Analysis (1st round)

### Scenario 1: Evaluating a starting base

```bash
# You have A♠ 2♥, opponent has K♠ Q♦
./pokenum -badugi As 2h - Ks Qd

# Result: As 2h wins ~98% of the time
# Analysis: Excellent base, keep and draw 2 cards
```

### Scenario 2: Comparing different bases

```bash
# Low base vs medium base
./pokenum -badugi As 3h - 7s 9d
# Expected result: A-3 dominates heavily

# Two medium bases
./pokenum -badugi 6s 8h - 7d 9c
# Result: Closer matchup, 6-8 slightly favored
```

### Scenario 3: Hands with duplicates

```bash
# Pair vs distinct cards
./pokenum -badugi As Ad - 6h 9c
# The pair must discard a card, significant disadvantage

# Suited duplicate vs distinct ranks
./pokenum -badugi As 2s - 7h 9d
# Suited duplicate is less penalizing than a rank duplicate
```

## 🔄 Post-Draw Analysis

### After the 1st draw

```bash
# You had A♠ 2♥, drew 3♦ J♣ (keep A-2-3, discard J)
./pokenum -badugi As 2h 3d - Opponent_cards

# You had a pair, drew a distinct card
./pokenum -badugi 4s 7h 9c - Opponent_cards
```

### Analyzing pat hands (complete hands)

```bash
# Your 4-card Badugi vs opponent who is still drawing
./pokenum -badugi As 2h 3d 4c - 6s 7h
# Huge advantage, but watch out for opponent nuts draws
```

## 📈 Advanced Strategic Scenarios

### 1. "Breaking" Decision (breaking a hand)

```bash
# You have a 3-card 6-7-8, opponent seems to have better
./pokenum -mc 50000 -badugi 6s 7h 8d - As 2c

# Vs keeping and hoping for a good 4th card
./pokenum -mc 50000 -badugi 6s 7h 8d - As 2c 3h
```

### 2. Analysis of "Smooth" vs "Rough" draws

```bash
# Smooth draw (A-2-3 + any card)
./pokenum -mc 100000 -badugi As 2h 3d - opponent_hand

# Rough draw (7-8-9 + any card)
./pokenum -mc 100000 -badugi 7s 8h 9d - opponent_hand
```

### 3. Critical Heads-Up Situations

```bash
# Last card, you pat vs opponent drawing 1
./pokenum -badugi As 2h 3d 5c - 4s 6h 7d

# Analyze if your rough Badugi holds against a good draw
./pokenum -mc 75000 -badugi 8s 9h Tc Jd - As 2c 3h
```

## 🎲 Monte Carlo vs Exhaustive Enumeration

### When to use `-mc`:
- **Incomplete hands** (< 4 cards per player)
- **Fast analysis** (100k samples = ~1 second)
- **Complex situations** with many variables

### When to use exhaustive enumeration:
- **Complete hands** (4 cards each)
- **Absolute precision** required
- **Fast calculations** (few unknown cards)

## 📊 Interpreting Results

```
Badugi (4-card lowball, unique suits and ranks): 50000 sampled outcomes
cards           win   %win      lose  %lose       tie   %tie        EV
As 2h 3d      47532  95.06      2468   4.94         0   0.00     0.951
Ks Qh           2468   4.94     47532  95.06         0   0.00     0.049
```

### Reading:
- **win %**: Win percentage
- **EV**: Expected value (0.951 = recovers 95.1% of the pot on average)
- **tie %**: Ties (rare in Badugi)

## 🏆 Optimal Strategies by Situation

### Early Draw Position:
```bash
# Opening standards: A-2-X, A-3-X, 2-3-X
./pokenum -badugi As 2h - random_opponent
# Threshold: >70% equity for value bet
```

### Late Draw Position:
```bash
# Callable hands with wider draws
./pokenum -badugi 4s 6h - tight_opener_range
# Threshold: >30% equity for defensive call
```

### All-in Situations:
```bash
# ICM considerations with short stacks
./pokenum -mc 200000 -badugi your_hand - opponent_range
# Need >50% for neutral call, higher depending on ICM
```

## 🔧 Useful Pre-configured Commands

```bash
# Test an A-2 base vs random opposition
alias badugi_a2="./pokenum -mc 100000 -badugi As 2h - "

# Quick equity test
alias badugi_quick="./pokenum -mc 50000 -badugi "

# Precise analysis (exhaustive when possible)
alias badugi_exact="./pokenum -badugi "
```

## 💡 Optimization Tips

1. **Use Monte Carlo** for quick in-session analysis
2. **Save results** to build a reference database
3. **Analyze patterns**: A-2-X vs A-3-X vs 2-3-X
4. **Study breakeven thresholds** based on position and stack sizes
5. **Validate your intuitions** with exact calculations

## 🚨 Current Limitations

- **Complex ranges**: pokenum evaluates hand vs hand; range-vs-range analysis is not wired into the CLI. The underlying range parser does support Badugi 4-card hands (`pe_range_parse(game_badugi, "As2d3h4c, KsQdJhTc", ...)`), including `@` weights.
- **Multiway analysis**: Limited to 2 players for now
- **Dead cards**: No explicit support for exposed cards

## 📚 Additional Resources

- **Unit tests**: `./build/tests/test_badugi` for validation
- **Source code**: `src/games/badugi_eval.c` (and headers in `include/poker_eval/games/`) for technical details
- **Other variants**: Badacey (-badacey) and Badeucy support available

---

*This guide uses poker-eval with full Badugi support. For other poker variants, consult the general documentation.*