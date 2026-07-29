/* aligned_tables.c - Cache-aligned versions of lookup tables */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/utils/aligned_tables.h>

/* External declarations for original lookup tables */
extern uint8 straightTable[8192];
extern uint32 topFiveCardsTable[8192];
extern uint8 nBitsTable[8192];
extern uint8 topCardTable[8192];

#if defined(__GNUC__) || defined(__clang__)
#define CACHE_ALIGNED __attribute__((aligned(64)))
#elif defined(_MSC_VER)
#define CACHE_ALIGNED __declspec(align(64))
#else
#define CACHE_ALIGNED
#endif

static CACHE_ALIGNED uint8_t aligned_straightTable[8192];
static CACHE_ALIGNED uint32_t aligned_topFiveCardsTable[8192];
static CACHE_ALIGNED uint8_t aligned_nBitsTable[8192];
static CACHE_ALIGNED uint8_t aligned_topCardTable[8192];

static int aligned_tables_initialized = 0;

void init_aligned_tables(void)
{
  if (aligned_tables_initialized)
    return;
  memcpy(aligned_straightTable, straightTable, sizeof(aligned_straightTable));
  memcpy(aligned_topFiveCardsTable, topFiveCardsTable, sizeof(aligned_topFiveCardsTable));
  memcpy(aligned_nBitsTable, nBitsTable, sizeof(aligned_nBitsTable));
  memcpy(aligned_topCardTable, topCardTable, sizeof(aligned_topCardTable));
  aligned_tables_initialized = 1;
}

const uint8_t *get_aligned_straightTable(void)
{
  if (!aligned_tables_initialized)
    init_aligned_tables();
  return aligned_straightTable;
}

const uint32_t *get_aligned_topFiveCardsTable(void)
{
  if (!aligned_tables_initialized)
    init_aligned_tables();
  return aligned_topFiveCardsTable;
}

const uint8_t *get_aligned_nBitsTable(void)
{
  if (!aligned_tables_initialized)
    init_aligned_tables();
  return aligned_nBitsTable;
}

const uint8_t *get_aligned_topCardTable(void)
{
  if (!aligned_tables_initialized)
    init_aligned_tables();
  return aligned_topCardTable;
}
