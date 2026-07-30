# PLO (Pot-Limit Omaha) Nomenclature

## Overview

In Pot-Limit Omaha (PLO) circles, coaches, solvers, and forums have converged on a **near-universal nomenclature** to quickly describe starting hands and ranges.

## Notation Conventions

### 1. Rank Initials
- **A-K-Q-J-T-9…2**: Similar to Hold'em but listed across four cards
- Examples: `AAKQ`, `JT98`, `8765`

### 2. Suitedness Suffixes
These suffixes instantly indicate flush potential:

| Suffix | Meaning | Example Text | Quick Note |
|--------|---------|--------------|------------|
| **ds** | *double-suited* (2 suit pairs) | `AAKKds` | "Nut potential": max two suits |
| **ss** | *single-suited* (one suited pair) | `AQT9ss` | Single flush draw |
| **r / rainbow** | four different suits | `AKJ7r` | Zero flush draw |

### 3. Placeholders
- **x** or **xx**: Wildcard / indifferent cards
- Example: `AAxx` designates any hand with a pair of Aces

### 4. Structural Abbreviations
- **rundown**: Consecutive cards (e.g., `JT98`)
- **1-gap**: One missing card in sequence (e.g., `JT86`)
- **double-paired**: Two pairs (e.g., `KKQQ`)

### 5. Formal Syntax (ProPokerTools / MonkerSolver)
Uses operators `, : ! > <` and macros like `$ds`, `$0g`, etc., to filter or combine sub-ranges in solvers.

## The 21 Starting Categories (4-Card PLO)

| Category Group | Category | % of Deck* | Hand Examples | Pre-flop Strategy Note (100 bb, 6-max) |
|----------------|----------|------------|---------------|-----------------------------------------|
| **A — Unpaired** | | | | |
| | 1. Double-suited (DS) | 9.5 % | T♠9♠8♥7♥ / J♠9♠8♦2♣ / K♥9♦6♠3♣ | "Pure" hand: focused on connectivity. DS = RFI IP; SS usually call; RB often fold OOP |
| | 2. Single-suited (SS) | 26.9 % | | |
| | 3. Rainbow (RB) | 6.3 % | | |
| **B — One-Pair (xxyy)** | | | | |
| | 4. Pair DS | 3.6 % | K♠K♦9♠4♦ | 3-bet IP or vs cold-call; showdown value + blockers |
| | 5. Pair SS | 20.6 % | Q♠Q♦T♥6♣ | Late RFI; fold OOP vs 3-bet without side cards |
| | 6. Pair RB | 23.8 % | J♦J♣8♠3♥ | Defense BB vs open, otherwise too fragile |
| **C — Two-Pair (xxyy)** | | | | |
| | 7. 2-Pair DS | 1.1 % | J♠J♦9♠9♦ / K♠K♦Q♥Q♣ | DS = big 3-bet-fold blocker, SS/RB more situational |
| | 8. 2-Pair SS | 10.0 % | | |
| | 9. 2-Pair RB | 0.8 % | | |
| **D — Trips (xxx y)** | | | | |
| | 10. Trips DS | 0.18 % | Q♠Q♦Q♥9♠ | Often limp/raise SB or 3-bet IP; poor post-flop playability |
| | 11. Trips SS | 1.6 % | | |
| | 12. Trips RB | 0.13 % | | |
| **E — Aces (AA xy)** | | | | |
| | 13. AA DS | 0.45 % | A♠A♦K♠J♦ / A♠A♥9♦5♣ | Always 4-bet ≤ 100 bb (except very tight spots) |
| | 14. AA SS | 3.9 % | | |
| | 15. AA RB | 0.3 % | | |
| **F — Broadway-heavy** | | | | |
| | 16. 3 Broadway DS | 2.2 % | K♠Q♠J♦T♦ / Q♠J♥T♥8♣ | Very strong IP: nut-straights + blocker equity |
| | 17. 3 Broadway SS | 7.7 % | | |
| | 18. 3 Broadway RB | 0.7 % | | |
| **G — Ragged / Low** | | | | |
| | 19. Ragged DS | 2.6 % | 9♠6♠4♦2♥ / 8♠5♣3♦2♣ | Leverage for open-limp deep-stack; otherwise mostly fold |
| | 20. Ragged SS | 7.0 % | | |
| | 21. Ragged RB | 2.0 % | | |

*Percentages are approximate and may vary depending on sources.

## Sources & Popularization

These conventions were popularised by:
- ProPokerTools
- PLO Mastermind
- Educational sites: PokerVIP, PokerStrategy, Adda52, 888poker

They now form the "common language" of pre-flop range charts and PLO study scripts.