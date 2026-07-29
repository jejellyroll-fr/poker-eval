/* aligned_tables.h - Cache-aligned lookup tables interface */

#ifndef ALIGNED_TABLES_H
#define ALIGNED_TABLES_H

#include <stdint.h>
#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

POKEREVAL_EXPORT void init_aligned_tables(void);
POKEREVAL_EXPORT const uint8_t* get_aligned_straightTable(void);
POKEREVAL_EXPORT const uint32_t* get_aligned_topFiveCardsTable(void);
POKEREVAL_EXPORT const uint8_t* get_aligned_nBitsTable(void);
POKEREVAL_EXPORT const uint8_t* get_aligned_topCardTable(void);

#ifdef USE_ALIGNED_TABLES
  extern const uint8_t* aligned_straightTable_ptr;
  extern const uint32_t* aligned_topFiveCardsTable_ptr;
  extern const uint8_t* aligned_nBitsTable_ptr;
  extern const uint8_t* aligned_topCardTable_ptr;
  #define straightTable (aligned_straightTable_ptr)
  #define topFiveCardsTable (aligned_topFiveCardsTable_ptr)
  #define nBitsTable (aligned_nBitsTable_ptr)
  #define topCardTable (aligned_topCardTable_ptr)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ALIGNED_TABLES_H */

