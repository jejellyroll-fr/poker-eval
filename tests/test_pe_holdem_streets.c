#include <poker_eval/solver/pe_holdem_streets.h>

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); ++failures; } } while (0)

static mask_t card(int index)
{
    return mask_set(MASK_EMPTY, index);
}

static int count_boards(mask_t board, uint8_t added, double weight, void *user)
{
    size_t *count = (size_t *)user;
    CHECK(mask_popcount(board) == (int)(added == 3u ? 3u : 4u),
          "callback board has unexpected size");
    CHECK(weight == 1.0, "public outcome weight is not one");
    ++*count;
    return 0;
}

static void test_street_mapping(void)
{
    pe_holdem_street_t street;
    CHECK(pe_holdem_street_from_board(MASK_EMPTY, &street) == 0 &&
              street == PE_HOLDEM_PREFLOP &&
              pe_holdem_next_public_count(street) == 3u,
          "preflop mapping failed");
    CHECK(pe_holdem_street_from_board(card(0) | card(1) | card(2), &street) == 0 &&
              street == PE_HOLDEM_FLOP &&
              pe_holdem_next_public_count(street) == 1u,
          "flop mapping failed");
    CHECK(pe_holdem_street_from_board(card(0) | card(1) | card(2) | card(3),
                                      &street) == 0 &&
              street == PE_HOLDEM_TURN &&
              pe_holdem_next_public_count(street) == 1u,
          "turn mapping failed");
    CHECK(pe_holdem_street_from_board(card(0) | card(1) | card(2) | card(3) |
                                          card(4), &street) == 0 &&
              street == PE_HOLDEM_RIVER &&
              pe_holdem_next_public_count(street) == 0u,
          "river mapping failed");
}

static void test_exact_transitions(void)
{
    size_t count = 0u;
    double weight_sum = 0.0;
    size_t callbacks = 0u;
    mask_t board = card(0) | card(1) | card(2);
    mask_t dead = card(3) | card(4) | card(5) | card(6);
    CHECK(pe_holdem_public_outcome_count(board, dead) == 45u,
          "flop-to-turn outcome count is wrong");
    CHECK(pe_holdem_public_chance_enumerate(
              board, dead, count_boards, &callbacks, &count, &weight_sum) == 0,
          "flop-to-turn enumeration failed");
    CHECK(count == 45u && callbacks == 45u && fabs(weight_sum - 45.0) < 1e-12,
          "flop-to-turn totals are wrong: %zu %zu %.17g", count, callbacks,
          weight_sum);
    CHECK(pe_holdem_public_outcome_count(MASK_EMPTY, dead) == 17296u,
          "preflop flop-combination count is wrong");
}

int main(void)
{
    test_street_mapping();
    test_exact_transitions();
    if (failures)
        return 1;
    puts("test_pe_holdem_streets: exact preflop/flop/turn transitions passed");
    return 0;
}
