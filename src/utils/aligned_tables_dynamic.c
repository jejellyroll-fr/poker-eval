/* aligned_tables_dynamic.c -- Dynamic allocation of aligned lookup tables */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/utils/aligned_tables_dynamic.h>

extern uint8_t straightTable[8192];
extern uint32_t topFiveCardsTable[8192];
extern uint8_t nBitsTable[8192];

uint8_t *aligned_straightTable = NULL;
uint32_t *aligned_topFiveCardsTable = NULL;
uint8_t *aligned_nBitsTable = NULL;

static int tables_initialized = 0;

static int pe_posix_memalign(void **ptr, size_t alignment, size_t size) {
#if defined(_WIN32)
    if (!ptr) {
        return EINVAL;
    }
    *ptr = _aligned_malloc(size, alignment);
    return *ptr ? 0 : ENOMEM;
#else
    return posix_memalign(ptr, alignment, size);
#endif
}

static void pe_aligned_free(void *ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

int init_dynamic_aligned_tables(void) {
    int ret;
    if (tables_initialized) return 0;
    ret = pe_posix_memalign((void**)&aligned_straightTable, 64, 8192 * sizeof(uint8_t));
    if (ret != 0) { fprintf(stderr, "Failed to alloc straightTable: %s\n", strerror(ret)); return -1; }
    ret = pe_posix_memalign((void**)&aligned_topFiveCardsTable, 64, 8192 * sizeof(uint32_t));
    if (ret != 0) { fprintf(stderr, "Failed to alloc topFiveCards: %s\n", strerror(ret)); pe_aligned_free(aligned_straightTable); aligned_straightTable=NULL; return -1; }
    ret = pe_posix_memalign((void**)&aligned_nBitsTable, 64, 8192 * sizeof(uint8_t));
    if (ret != 0) { fprintf(stderr, "Failed to alloc nBits: %s\n", strerror(ret)); pe_aligned_free(aligned_straightTable); pe_aligned_free(aligned_topFiveCardsTable); aligned_straightTable=NULL; aligned_topFiveCardsTable=NULL; return -1; }
    memcpy(aligned_straightTable, straightTable, sizeof(straightTable));
    memcpy(aligned_topFiveCardsTable, topFiveCardsTable, sizeof(topFiveCardsTable));
    memcpy(aligned_nBitsTable, nBitsTable, sizeof(nBitsTable));
    tables_initialized = 1;
    return 0;
}

void cleanup_dynamic_aligned_tables(void) {
    if (aligned_straightTable) { pe_aligned_free(aligned_straightTable); aligned_straightTable = NULL; }
    if (aligned_topFiveCardsTable) { pe_aligned_free(aligned_topFiveCardsTable); aligned_topFiveCardsTable = NULL; }
    if (aligned_nBitsTable) { pe_aligned_free(aligned_nBitsTable); aligned_nBitsTable = NULL; }
    tables_initialized = 0;
}

uint8_t* get_dynamic_aligned_straightTable(void) {
    if (!tables_initialized) init_dynamic_aligned_tables();
    return aligned_straightTable ? aligned_straightTable : straightTable;
}

uint32_t* get_dynamic_aligned_topFiveCardsTable(void) {
    if (!tables_initialized) init_dynamic_aligned_tables();
    return aligned_topFiveCardsTable ? aligned_topFiveCardsTable : topFiveCardsTable;
}

uint8_t* get_dynamic_aligned_nBitsTable(void) {
    if (!tables_initialized) init_dynamic_aligned_tables();
    return aligned_nBitsTable ? aligned_nBitsTable : nBitsTable;
}
