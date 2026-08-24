#include <poker_eval/solver/pe_actions.h>

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(condition, message)                                      \
    do                                                                 \
    {                                                                  \
        if (!(condition))                                              \
        {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,  \
                    message);                                         \
            failures++;                                                \
        }                                                              \
    } while (0)

static void test_legacy_translation(void)
{
    const double sizes[] = {0.5, 1.0};
    pe_action_t action;
    int code;

    CHECK(pe_action_from_legacy_code(0, sizes, 2u, 0, 8, &action) ==
              PE_ACTION_OK &&
              action.kind == PE_ACTION_FOLD,
          "fold translation");
    CHECK(pe_action_from_legacy_code(1, sizes, 2u, 0, 8, &action) ==
              PE_ACTION_OK &&
              action.kind == PE_ACTION_CALL,
          "call translation");
    CHECK(pe_action_from_legacy_code(2, sizes, 2u, 1, 8, &action) ==
              PE_ACTION_OK && action.kind == PE_ACTION_RAISE &&
              action.amount_kind == PE_AMOUNT_POT_FRACTION &&
              fabs(action.amount - 0.5) < 1e-12 && action.size_index == 0,
          "pot-sized raise translation");
    CHECK(pe_action_from_legacy_code(8, sizes, 2u, 0, 8, &action) ==
              PE_ACTION_OK &&
              action.kind == PE_ACTION_ALL_IN,
          "all-in translation");
    CHECK(pe_action_to_legacy_code(&action, 2u, 8, &code) == PE_ACTION_OK &&
              code == 8,
          "all-in round trip");
    CHECK(pe_action_from_legacy_code(4, sizes, 2u, 0, 8, &action) ==
              PE_ACTION_ERR_OUT_OF_RANGE,
          "unknown raise index rejected");
}

static void test_commitments(void)
{
    const double chip_sizes[] = {50.0, 100.0};
    const double pot_sizes[] = {0.5, 1.0};
    pe_action_t action;
    double commitment;

    CHECK(pe_action_from_legacy_code(1, chip_sizes, 2u, 0, 8, &action) ==
              PE_ACTION_OK &&
              pe_action_commitment(&action, 25.0, 100.0, 100.0, 25.0, 0,
                                   &commitment) == PE_ACTION_OK &&
              fabs(commitment - 25.0) < 1e-12,
          "call commits only the outstanding amount");
    CHECK(pe_action_from_legacy_code(2, chip_sizes, 2u, 0, 8, &action) ==
              PE_ACTION_OK &&
              pe_action_commitment(&action, 25.0, 100.0, 100.0, 25.0, 0,
                                   &commitment) == PE_ACTION_OK &&
              fabs(commitment - 75.0) < 1e-12,
          "raise commits call plus raise increment");
    CHECK(pe_action_from_legacy_code(2, pot_sizes, 2u, 1, 8, &action) ==
              PE_ACTION_OK &&
              pe_action_commitment(&action, 25.0, 100.0, 100.0, 25.0, 0,
                                   &commitment) == PE_ACTION_OK &&
              fabs(commitment - 75.0) < 1e-12,
          "pot-sized raise resolves against pot");
    CHECK(pe_action_from_legacy_code(8, chip_sizes, 2u, 0, 8, &action) ==
              PE_ACTION_OK &&
              pe_action_commitment(&action, 25.0, 100.0, 100.0, 25.0, 0,
                                   &commitment) == PE_ACTION_OK &&
              fabs(commitment - 100.0) < 1e-12,
          "all-in commits the complete remaining stack");

    action.kind = PE_ACTION_CHECK;
    action.amount_kind = PE_AMOUNT_NONE;
    action.amount = 0.0;
    action.size_index = -1;
    CHECK(pe_action_commitment(&action, 1.0, 100.0, 100.0, 1.0, 0,
                               &commitment) == PE_ACTION_ERR_INVALID,
          "check facing a bet rejected");
}

int main(void)
{
    test_legacy_translation();
    test_commitments();
    if (failures)
        fprintf(stderr, "test_pe_actions: %d failure(s)\n", failures);
    else
        puts("test_pe_actions: semantic action contract passed");
    return failures ? 1 : 0;
}
