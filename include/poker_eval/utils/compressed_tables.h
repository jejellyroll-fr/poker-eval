/* compressed_tables.h - Compressed and aligned lookup tables interface */

#ifndef COMPRESSED_TABLES_H
#define COMPRESSED_TABLES_H

#include <stdint.h>
#include <stddef.h>
#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

POKEREVAL_EXPORT void init_compressed_tables(void);
POKEREVAL_EXPORT void cleanup_compressed_tables(void);
POKEREVAL_EXPORT uint8_t get_straight_value(uint16_t index);
POKEREVAL_EXPORT uint32_t get_topfive_value(uint16_t index);
POKEREVAL_EXPORT void get_compression_stats(size_t* original_size, size_t* compressed_size);

#ifdef USE_COMPRESSED_TABLES
  #define STRAIGHT_VALUE(i) get_straight_value(i)
  #define TOPFIVE_VALUE(i) get_topfive_value(i)
#else
  #define STRAIGHT_VALUE(i) straightTable[i]
  #define TOPFIVE_VALUE(i) topFiveCardsTable[i]
#endif

#ifdef __cplusplus
}
#endif

#endif /* COMPRESSED_TABLES_H */

