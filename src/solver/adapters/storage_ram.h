/* Internal RAM adapter factory used by the solver's default-storage path. */

#ifndef POKER_EVAL_STORAGE_RAM_INTERNAL_H
#define POKER_EVAL_STORAGE_RAM_INTERNAL_H

#include <stddef.h>

#include <poker_eval/solver/pe_storage.h>

#ifdef __cplusplus
extern "C" {
#endif

pe_storage_t *pe_storage_ram_create_with_precision(
    size_t expected_infosets, pe_precision_mode_t precision);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_STORAGE_RAM_INTERNAL_H */
