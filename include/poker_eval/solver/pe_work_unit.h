/*
 * pe_work_unit.h - backend-independent distributed work unit (DIST-02)
 */

#ifndef POKER_EVAL_PE_WORK_UNIT_H
#define POKER_EVAL_PE_WORK_UNIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_WORK_UNIT_DESCRIPTOR_MAX 65536
#define PE_WORK_UNIT_DESCRIPTOR_VERSION 1

typedef struct pe_work_unit_t
{
    uint64_t public_state;
    uint8_t player;
    uint64_t iteration_begin;
    uint64_t iteration_end;

    /* Boards are packed as board_count consecutive board_width-byte states. */
    uint8_t *boards;
    size_t board_count;
    uint8_t board_width;

    /* Opaque serialized ranges owned by the work unit. */
    uint8_t *ranges;
    size_t ranges_size;

    /* F64 regret snapshot owned by the work unit. */
    double *regret_snapshot;
    size_t regret_count;
} pe_work_unit_t;

void pe_work_unit_init(pe_work_unit_t *unit);
void pe_work_unit_destroy(pe_work_unit_t *unit);

/** Validate ownership-independent invariants and numeric snapshot values. */
int pe_work_unit_validate(const pe_work_unit_t *unit);

/** Serialize in the stable PE_WORK_V1 format; return required bytes. */
size_t pe_work_unit_to_string(const pe_work_unit_t *unit,
                              char *out, size_t capacity);

/** Parse a PE_WORK_V1 string and allocate all variable-sized fields.
 * `out` must be initialized with pe_work_unit_init() or already destroyed. */
int pe_work_unit_from_string(const char *text, pe_work_unit_t *out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_WORK_UNIT_H */
