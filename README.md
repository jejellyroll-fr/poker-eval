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

---

### **Comprehensive Table of Poker Variants**

| **Variant Name**                   | **Type**        | **Deck Used**                          | **High Evaluation**                   | **Low Evaluation**                    | **Combination**                                             | **Summary of Rules**                                                                                                                                                                                                 |
|------------------------------------|-----------------|----------------------------------------|---------------------------------------|----------------------------------------|-------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Texas Hold'em**                  | Hold'em         | Standard (52 cards)                    | Standard (High hand ranking)          | N/A                                    | Use any 5 of 7 cards (2 hole cards + 5 community cards)      | Each player receives 2 private (hole) cards and uses 5 community cards to form the best possible 5-card hand. **(Most well-known variant)**                                                                                  |
| **Omaha**                          | Hold'em         | Standard (52 cards)                    | Standard                              | N/A                                    | Must use exactly 2 hole cards and 3 community cards         | Players receive 4 private (hole) cards and must use exactly 2 of them with 3 community cards to form their best hand.                                                                                                         |
| **Omaha Hi/Lo (Omaha8)**           | Hold'em         | Standard (52 cards)                    | Standard                              | A-5 Lowball (8 or better)              | Must use exactly 2 hole cards and 3 community cards         | Similar to Omaha, but the pot is split between the best high hand and the best qualifying low hand (low hand must be 8 or lower).                                                                                             |
| **Short Deck Hold'em (6+ Hold'em)**| Hold'em         | Short Deck (36 cards, no 2-5)           | Short Deck Rules                      | N/A                                    | Use any 5 of 7 cards (2 hole cards + 5 community cards)      | Played with a reduced deck (no 2s-5s). **Two main rules**: 1) A flush beats a full house, and 2) Ace can be low in a straight (A-6-7-8-9). **(Growing in popularity)**                                                  |
| **Seven Card Stud**                | Stud            | Standard (52 cards)                    | Standard                              | N/A                                    | Best 5 out of 7 cards                                       | Each player receives 7 cards (some face up, some face down). No community cards. **(Classic well-known variant)**                                                                                                            |
| **Seven Card Stud Hi/Lo (Stud8)**  | Stud            | Standard (52 cards)                    | Standard                              | A-5 Lowball (8 or better)              | Best 5 out of 7 cards                                       | Variant of Seven Card Stud where the pot is split between the best high hand and the best low hand.                                                                                                                        |
| **Razz**                           | Stud            | Standard (52 cards)                    | N/A                                   | A-5 Lowball                            | Best 5 out of 7 cards (lowest hand wins)                    | Players aim to make the lowest possible hand. Straights and flushes do not count against the player. **(Popular lowball variant)**                                                                                                |
| **Five Card Draw**                 | Draw            | Standard (52 cards)                    | Standard                              | N/A                                    | Best 5-card hand after drawing                               | Each player receives 5 cards and can exchange (draw) cards to improve their hand. **(Traditional variant)**                                                                                                                |
| **2-7 Triple Draw**                | Draw            | Standard (52 cards)                    | N/A                                   | 2-7 Lowball (Triple Draw)              | Best 5-card low hand after three draws                      | Players aim for the lowest possible hand (2-3-4-5-7). Straights and flushes count against the player. **(Common lowball variant)**                                                                                           |
| **2-7 Single Draw**                | Draw            | Standard (52 cards)                    | N/A                                   | 2-7 Lowball (Single Draw)              | Best 5-card low hand after one draw                         | Similar to Triple Draw but with only one drawing round.                                                                                                                                                                  |
| **Badugi**                         | Draw            | Standard (52 cards)                    | N/A                                   | Badugi Low (unique 4-card low hand)    | Best 4-card hand with unique suits and ranks                | Players aim for a Badugi: four cards of different suits and ranks. The best hand is A-2-3-4 of all different suits. **(Exotic variant)**                                                                                      |
| **Short Deck Hold'em (6+ Hold'em)**| Hold'em         | Short Deck (36 cards, no 2-5)           | Short Deck Rules                      | N/A                                    | Use any 5 of 7 cards (2 hole cards + 5 community cards)      | Similar to Texas Hold'em but with a reduced deck. **Two main rules**: 1) A flush beats a full house, and 2) Ace can be low in a straight (A-6-7-8-9). **(Expanding variant)**                                                 |
| **Joker Poker**                    | Various         | Standard (52 cards + Jokers)            | Standard with Jokers as wild          | N/A                                    | Jokers are used as wild cards                                 | Jokers are added to the deck and can replace any card to complete a combination. **(Uses Jokers)**                                                                                                                          |
| **Manila Poker (Asian Poker)**     | Hold'em         | Asian Deck (32 cards, 7 to Ace)          | Adjusted Hand Rankings                | N/A                                    | Must use both hole cards with community cards               | Played with a 32-card deck. Players receive 2 hole cards and must use both with 5 community cards. **(Uses Asian Deck)**                                                                                                  |
| **Open Face Chinese Poker**        | Other           | Standard (52 cards)                    | Standard                              | N/A                                    | Players build their hands progressively                      | Players arrange their cards face-up one by one to form three hands (two 5-card hands and one 3-card hand). **(Popular in Asia)**                                                                                           |
| **2-7 Razz**                       | Stud            | Standard (52 cards)                    | N/A                                   | 2-7 Lowball                            | Best 5 out of 7 cards (lowest hand wins)                    | Combination of Razz and 2-7 Lowball rules. Straights and flushes count against the player.                                                                                                                                 |
| **Badacey**                        | Draw            | Standard (52 cards)                    | N/A                                   | Split: A-5 Lowball & Badugi            | Combination of Badugi and A-5 Lowball hands                 | Split-pot game: half the pot goes to the best Badugi hand, half to the best A-5 Lowball hand.                                                                                                                             |
| **Badeucy**                        | Draw            | Standard (52 cards)                    | N/A                                   | Split: 2-7 Lowball & Badugi            | Combination of Badugi and 2-7 Lowball hands                 | Similar to Badacey, but with 2-7 Lowball rules for the low hand.                                                                                                                                                     |
| **Triple Stud**                    | Mixed           | Standard (52 cards)                    | Varies by rotation                    | Varies by rotation                     | Best 5-card hand in each game                               | Rotation between Stud, Razz, and Stud Hi/Lo. Rules change with each rotation. **(Advanced variant)**                                                                                                                  |
| **HORSE**                          | Mixed           | Standard (52 cards)                    | Varies by game                        | Varies by game                          | Varies by game                                             | Rotation between Hold'em, Omaha Hi/Lo, Razz, Stud, and Stud Hi/Lo. **(Well-known in tournaments)**                                                                                                                   |
| **Baduci**                         | Draw            | Standard (52 cards)                    | N/A                                   | Split: 2-7 Lowball & Badugi            | Combination of Badugi and 2-7 Lowball hands                 | Similar to Badeucy, sometimes with slight variations depending on the casino. **(Exotic variant)**                                                                                                                       |
| **Pineapple Hold'em**              | Hold'em         | Standard (52 cards)                    | Standard                              | N/A                                    | Use any 5 of 7 cards after discarding one                   | Players receive 3 hole cards and must discard one after the flop. **(Popular variant)**                                                                                                                               |
| **Crazy Pineapple**                | Hold'em         | Standard (52 cards)                    | Standard                              | N/A                                    | Use any 5 of 7 cards after discarding one                   | Similar to Pineapple, but players discard one hole card after the turn. **(Dynamic variant)**                                                                                                                         |
| **Courchevel**                     | Hold'em         | Standard (52 cards)                    | Standard                              | N/A                                    | Must use exactly 2 closed cards and 3 community cards        | Variant of Omaha where the first community card is revealed before the first betting round. Players receive 5 closed cards. **(Regional variant)**                                                                  |
| **Five Card Omaha**                | Hold'em         | Standard (52 cards)                    | Standard                              | N/A                                    | Must use exactly 2 closed cards and 3 community cards        | Similar to Omaha but with 5 closed cards, increasing possible hand combinations. **(Advanced variant)**                                                                                                               |
| **Chinese Poker**                  | Other           | Standard (52 cards)                    | Standard                              | N/A                                    | Players arrange 13 cards into three hands                     | Each player arranges 13 cards into three hands (two 5-card hands and one 3-card hand). Hands are compared individually. **(Popular in Asia)**                                                                      |
| **Badugi Lowball**                 | Draw            | Standard (52 cards)                    | N/A                                   | Badugi Low                             | Best 4-card hand with unique suits and ranks                | Variant of Badugi played exclusively as a lowball game, without high hand evaluation. **(Exotic variant)**                                                                                                           |
| **California Lowball**             | Draw            | Standard (52 cards)                    | N/A                                   | A-5 Lowball                            | Best 5-card low hand after drawing                          | Similar to Five Card Draw but the objective is to form the lowest possible hand. Aces are low. **(Popular variant)**                                                                                                 |
| **Kansas City Lowball**            | Draw            | Standard (52 cards)                    | N/A                                   | 2-7 Lowball                            | Best 5-card low hand after drawing                          | Lowball variant where Aces are high, and straights and flushes count against the player. **(Known variant)**                                                                                                        |
| **Super Stud Hi/Lo**               | Stud            | Standard (52 cards)                    | Standard                              | A-5 Lowball                            | Best high and low 5-card hands                              | Combines Stud with community cards and split-pot rules. Players aim for both high and low hands. **(Advanced variant)**                                                                                              |
| **No Qualifier Variants**          | Hold'em/Stud    | Standard (52 cards)                    | Standard                              | A-5 Lowball (No qualifier)             | Standard combinations                                       | In some Hi/Lo games, there is no minimum requirement for the low hand. The pot can be split even if the low hand is a high hand. **(Specific variant)**                                                          |
| **Ace-to-Six Lowball**             | Draw            | Standard (52 cards)                    | N/A                                   | A-6 Lowball                            | Best 5-card low hand                                         | Players aim for the lowest possible hand from Ace to Six. Straights and flushes do not count against the player. **(Known variant)**                                                                                 |
| **Six-Card Omaha**                 | Hold'em         | Standard (52 cards)                    | Standard                              | N/A                                    | Must use exactly 2 hole cards and 3 community cards         | Similar to Omaha, but with 6 hole cards, increasing hand possibilities. **(Advanced variant)**                                                                                                                     |
| **Tahoe Poker**                    | Hold'em         | Standard (52 cards)                    | Standard                              | N/A                                    | Use any combination of hole and community cards              | Similar to Omaha, but players can use any number of their hole cards with the community cards. **(Flexible variant)**                                                                                                 |

---

### **Supplementary Glossary**

**1. Reduced Deck (Short Deck):**  
A deck from which certain low-value cards are removed, typically cards 2 through 5, resulting in a 36-card deck. This reduction alters hand probabilities, making certain combinations like flushes rarer and more powerful.

**2. Lowball:**  
A type of poker where the objective is to form the lowest possible hand. Variants include:

- **A-5 Lowball:** Ace is considered the lowest card. Straights and flushes do not count against the low hand.
- **2-7 Lowball:** Ace is always the highest card. Straights and flushes count against the low hand, so the best possible hand is 7-5-4-3-2 without being of the same suit.

**3. Badugi:**  
An exotic poker variant where players aim to obtain the lowest possible hand with four unique cards in both rank and suit. Duplicate suits or ranks weaken the hand.

**4. Split-Pot:**  
A pot is divided between multiple players who hold the best hands in different categories (e.g., best high hand and best low hand).

**5. Wild Cards:**  
Special cards (like Jokers) that can substitute for any other card to complete a combination.

**6. Hand Ranking:**  
The hierarchy of poker hands from highest to lowest (e.g., Royal Flush, Straight Flush, Four of a Kind, etc.). Variants may adjust these rankings based on specific rules.

**7. Short Deck Rules:**  
Specific rules adapted for games played with a reduced deck. For example, in Short Deck Hold'em, a flush beats a full house, unlike standard rankings.

**8. Draw:**  
The action of discarding and replacing cards in certain poker variants to improve one's hand.

**9. Pass Card Game:**  
A type of game where players pass cards to their neighbors to form better hands over multiple rounds.

**10. Open Face (Face-Up) Poker:**  
A variant where players lay down their cards face-up progressively, adding strategic elements as the game unfolds.

**11. Hi/Lo:**  
A variant where the pot is split between the best high hand and the best low hand.

**12. Combination Rules:**  
Specific rules on how players can use their cards (e.g., exactly 2 hole cards in Omaha) to form their final hand.

**13. Summary of Rules:**  
A brief description of the main mechanics and objectives of each poker variant.

---

### **Clarifications on Variants with Jokers and Asian Decks**

**Variants Using Jokers:**

- **Joker Poker:**  
  - **Deck Used:** Standard (52 cards) plus 2 Jokers.  
  - **Special Rules:** Jokers act as wild cards, replacing any card to complete a combination. The specific rules for Joker Poker can vary depending on the platform or house rules.

**Variants Using Asian Decks:**

- **Manila Poker (Asian Poker):**  
  - **Deck Used:** Asian Deck (32 cards, typically from 7 to Ace).  
  - **Special Rules:** Played with a 32-card deck. Players receive 2 hole cards and use both with 5 community cards. Flushes are more valuable due to the reduced deck size, making straight flushes extremely powerful.

- **Billabong:**  
  - **Deck Used:** Asian Deck (32 cards, typically from 7 to Ace).  
  - **Special Rules:** A variant of Manila Poker with specific rules regarding hand combinations and the use of community cards. Exact rules can vary based on regional or house variations.

---

### **Most Well-Known Variants**

- **Texas Hold'em:**  
  The most popular poker variant globally, widely broadcasted through televised tournaments like the World Series of Poker (WSOP) and the World Poker Tour (WPT).

- **Omaha and Omaha Hi/Lo:**  
  Highly played in Europe and appreciated for the action they generate due to the numerous possible hand combinations with 4 hole cards.

- **Seven Card Stud:**  
  A classic variant that was the most popular before the rise of Texas Hold'em. It remains widely practiced, especially in private circles and mixed tournaments.

- **Razz:**  
  A lowball variant of Stud, known for reversing the traditional hand values where low hands are sought after.

- **HORSE:**  
  An acronym for Hold'em, Omaha Hi/Lo, Razz, Stud, and Stud Hi/Lo. This rotation of games tests players' versatility and is used in high-level tournaments.

- **Short Deck Hold'em:**  
  Gaining popularity, especially in Asia and high-stakes games. The reduced deck alters the hand probabilities, making certain hands more frequent.

- **Open Face Chinese Poker:**  
  Increasingly popular, especially online and in mobile applications. Appreciated for its blend of strategy and chance.

- **Badugi:**  
  An exotic variant combining elements of lowball and unique hands, offering a different experience from traditional variants.

---

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
