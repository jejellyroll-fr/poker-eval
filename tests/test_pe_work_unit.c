#include <poker_eval/solver/pe_work_unit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, message) \
    do { if (!(condition)) { fprintf(stderr, "FAILED: %s\n", message); failures++; } } while (0)

int main(void)
{
    pe_work_unit_t unit;
    pe_work_unit_t decoded;
    char descriptor[PE_WORK_UNIT_DESCRIPTOR_MAX];
    uint8_t boards[] = {0u, 1u, 51u, 7u, 8u, 9u};
    uint8_t ranges[] = {0u, 0xffu, 0x10u, 0x80u};
    double regrets[] = {1.25, -0.0, -3.5};
    size_t length;

    pe_work_unit_init(&unit);
    pe_work_unit_init(&decoded);
    unit.public_state = UINT64_C(0x0102030405060708);
    unit.player = 2u;
    unit.iteration_begin = 17u;
    unit.iteration_end = 64u;
    unit.boards = boards;
    unit.board_count = 2u;
    unit.board_width = 3u;
    unit.ranges = ranges;
    unit.ranges_size = sizeof(ranges);
    unit.regret_snapshot = regrets;
    unit.regret_count = sizeof(regrets) / sizeof(regrets[0]);

    CHECK(pe_work_unit_validate(&unit) == 0, "valid work unit rejected");
    length = pe_work_unit_to_string(&unit, descriptor, sizeof(descriptor));
    CHECK(length != 0u && length < sizeof(descriptor),
          "work unit descriptor was not rendered");
    CHECK(pe_work_unit_from_string(descriptor, &decoded) == 0,
          "work unit descriptor was not parsed");
    CHECK(decoded.public_state == unit.public_state &&
              decoded.player == unit.player &&
              decoded.iteration_begin == unit.iteration_begin &&
              decoded.iteration_end == unit.iteration_end &&
              decoded.board_count == unit.board_count &&
              decoded.board_width == unit.board_width &&
              decoded.ranges_size == unit.ranges_size &&
              decoded.regret_count == unit.regret_count,
          "work unit metadata did not round-trip");
    CHECK(decoded.boards != NULL && decoded.ranges != NULL &&
              decoded.regret_snapshot != NULL &&
              memcmp(decoded.boards, boards, sizeof(boards)) == 0 &&
              memcmp(decoded.ranges, ranges, sizeof(ranges)) == 0 &&
              memcmp(decoded.regret_snapshot, regrets, sizeof(regrets)) == 0,
          "work unit payload did not round-trip exactly");
    CHECK(pe_work_unit_from_string("PE_WORK_V0;state=0x0", &decoded) != 0,
          "unsupported work unit version accepted");

    pe_work_unit_destroy(&decoded);
    puts(failures == 0 ? "test_pe_work_unit: round-trip passed"
                       : "test_pe_work_unit: failed");
    return failures == 0 ? 0 : 1;
}
