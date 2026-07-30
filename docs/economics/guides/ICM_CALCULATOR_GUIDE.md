# ICM Calculator Guide

## Introduction

The ICM (Independent Chip Model) calculator in `poker-eval` provides monetary equity valuation and deal-chopping calculations for tournament poker and Sit-and-Go scenarios. The core API is defined in `<poker_eval/economics/icm.h>`.

ICM converts chip stacks into monetary equity values based on the probability of each player finishing in each paid position according to the Malmuth-Harville model.

## Header File

To use the ICM API, include the public header:

```c
#include <poker_eval/economics/icm.h>
```

## Data Types & Constants

### `ICM_MAX_PLAYERS`
```c
#define ICM_MAX_PLAYERS 23
```
Maximum supported number of players and payout positions in ICM calculations.

### `icm_input_t`
Input parameters structure passed to ICM calculation functions.

```c
typedef struct {
    double stacks[ICM_MAX_PLAYERS];  /* Chip stack size for each player */
    int num_players;                 /* Number of active players (1 to ICM_MAX_PLAYERS) */
    double payouts[ICM_MAX_PLAYERS]; /* Payout amount for each finishing position */
    int num_payouts;                 /* Number of paid positions */
} icm_input_t;
```

### `icm_result_t`
Output structure containing calculation results.

```c
typedef struct {
    double icm_ev[ICM_MAX_PLAYERS];   /* Expected Value in currency/monetary units */
    double equity[ICM_MAX_PLAYERS];   /* Normalized equity share (0.0 to 1.0) */
} icm_result_t;
```

## Functions

### `pe_icm_calculate`

Calculates ICM EV and equity shares using the Malmuth-Harville probability model.

```c
int pe_icm_calculate(const icm_input_t *input, icm_result_t *result);
```

- **Parameters:**
  - `input`: Pointer to populated `icm_input_t` struct.
  - `result`: Pointer to `icm_result_t` struct where results will be written.
- **Returns:**
  - `0` on success.
  - `-1` if arguments are invalid (`input` or `result` is NULL, `num_players <= 0`, `num_players > ICM_MAX_PLAYERS`, `num_payouts <= 0`, or total chip stack sum is `<= 0.0`).

### `pe_icm_chop`

Calculates tournament deal-chop payouts for active players based on ICM EV.

```c
int pe_icm_chop(const icm_input_t *input, double *out_payouts);
```

- **Parameters:**
  - `input`: Pointer to populated `icm_input_t` struct.
  - `out_payouts`: Output array of at least `input->num_players` doubles to store each player's chop payout.
- **Returns:**
  - `0` on success.
  - `-1` on error.

### Malmuth-Harville Model (`pe_icm_malmuth_harville`)

The underlying algorithm used by `pe_icm_calculate` implements the **Malmuth-Harville** model:
- The probability of player $i$ taking 1st place is equal to their stack divided by the total active chips:
  $$P(\text{Player } i \text{ finishes } 1\text{st}) = \frac{\text{stacks}[i]}{\sum_{k} \text{stacks}[k]}$$
- For subsequent finishing positions ($2\text{nd}, 3\text{rd}, \dots$), probabilities are calculated recursively by conditioning on remaining active players and scaling remaining stack proportions.

## Code Example

```c
#include <stdio.h>
#include <poker_eval/economics/icm.h>

int main(void) {
    icm_input_t input = {0};
    icm_result_t result = {0};

    /* 3 players remaining at final table */
    input.num_players = 3;
    input.stacks[0] = 5000.0;
    input.stacks[1] = 3000.0;
    input.stacks[2] = 2000.0;

    /* Payouts: 1st = $500, 2nd = $300, 3rd = $200 */
    input.num_payouts = 3;
    input.payouts[0] = 500.0;
    input.payouts[1] = 300.0;
    input.payouts[2] = 200.0;

    if (pe_icm_calculate(&input, &result) == 0) {
        printf("ICM Calculation Successful:\n");
        for (int i = 0; i < input.num_players; i++) {
            printf("Player %d: EV = $%.2f, Equity Share = %.4f (%.2f%%)\n",
                   i + 1, result.icm_ev[i], result.equity[i], result.equity[i] * 100.0);
        }
    }

    /* Calculate deal chop */
    double chop_payouts[ICM_MAX_PLAYERS];
    if (pe_icm_chop(&input, chop_payouts) == 0) {
        printf("\nDeal Chop Payouts:\n");
        for (int i = 0; i < input.num_players; i++) {
            printf("Player %d chop: $%.2f\n", i + 1, chop_payouts[i]);
        }
    }

    return 0;
}
```