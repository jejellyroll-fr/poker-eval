/* aligned_tables_dynamic.h -- Dynamic allocation of aligned lookup tables */

#ifndef __ALIGNED_TABLES_DYNAMIC_H__
#define __ALIGNED_TABLES_DYNAMIC_H__

#include <poker_eval/core/poker_defs.h>

int init_dynamic_aligned_tables(void);
void cleanup_dynamic_aligned_tables(void);
uint8_t* get_dynamic_aligned_straightTable(void);
uint32_t* get_dynamic_aligned_topFiveCardsTable(void);
uint8_t* get_dynamic_aligned_nBitsTable(void);

#ifdef USE_ALIGNED_TABLES
  extern uint8_t *aligned_straightTable;
  extern uint32_t *aligned_topFiveCardsTable;
  extern uint8_t *aligned_nBitsTable;
  #define straightTable (aligned_straightTable ? aligned_straightTable : get_aligned_straightTable())
  #define topFiveCardsTable (aligned_topFiveCardsTable ? aligned_topFiveCardsTable : get_aligned_topFiveCardsTable())
  #define nBitsTable (aligned_nBitsTable ? aligned_nBitsTable : get_aligned_nBitsTable())
#endif

#endif /* __ALIGNED_TABLES_DYNAMIC_H__ */
