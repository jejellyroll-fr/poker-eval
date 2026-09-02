/* GPU-06: deterministic infoset/action/combo to ragged-slot mapping. */

#include <poker_eval/solver/pe_compute.h>

#include <stdio.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            failures++;                                            \
        }                                                          \
    } while (0)

int main(void)
{
    const pe_infoset_id_t infosets[] = {41u, 7u};
    const uint32_t offsets[] = {0u, 6u, 10u};
    const uint16_t actions[] = {2u, 1u};
    const uint16_t combos[] = {3u, 4u};
    const pe_infoset_layout_t layout = {
        2u, infosets, offsets, actions, combos
    };
    pe_update_t update = {7u, 0u, 3u, 0.0, 0.0};
    uint32_t slot = UINT32_MAX;

    CHECK(pe_infoset_layout_resolve_slot(&layout, &update, &slot) == 0 &&
              slot == 9u,
          "second infoset combo did not resolve to slot 9");
    update.infoset = 41u;
    update.action = 1u;
    update.combo = 2u;
    CHECK(pe_infoset_layout_resolve_slot(&layout, &update, &slot) == 0 &&
              slot == 5u,
          "first infoset action/combo did not resolve to slot 5");
    update.action = 2u;
    CHECK(pe_infoset_layout_resolve_slot(&layout, &update, &slot) != 0,
          "out-of-range action was accepted");
    update.action = 1u;
    update.infoset = 99u;
    CHECK(pe_infoset_layout_resolve_slot(&layout, &update, &slot) != 0,
          "unknown infoset was accepted");
    if (failures != 0)
        return 1;
    puts("test_pe_compute_layout: ragged slot mapping passed");
    return 0;
}
