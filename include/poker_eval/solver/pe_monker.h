/*
 * pe_monker.h - MonkerSolver .tree header reader (MKR-01)
 */

#ifndef POKER_EVAL_PE_MONKER_H
#define POKER_EVAL_PE_MONKER_H

#include <stdint.h>
#include <stddef.h>
#include <poker_eval/range.h>

struct mpf_tree_def_t;

#ifdef __cplusplus
extern "C" {
#endif

#define PE_MONKER_MAX_PLAYERS 8u

typedef enum
{
    PE_MONKER_OK = 0,
    PE_MONKER_ERR_NULL_ARGUMENT,
    PE_MONKER_ERR_OPEN,
    PE_MONKER_ERR_IO,
    PE_MONKER_ERR_TRUNCATED,
    PE_MONKER_ERR_BAD_SIGNATURE,
    PE_MONKER_ERR_INVALID_HEADER,
    PE_MONKER_ERR_INVALID_ACTION,
    PE_MONKER_ERR_INVALID_TOPOLOGY,
    PE_MONKER_ERR_DEPTH_LIMIT,
    PE_MONKER_ERR_TOO_LARGE
} pe_monker_status_t;

typedef struct
{
    int64_t signature;
    int32_t internal_format;
    uint32_t player_count;
    int32_t first_to_act;
    int32_t street;

    /* Committed is present in the wire header only at street zero. The
       reader clears it for later streets so callers never consume stale data. */
    double committed[PE_MONKER_MAX_PLAYERS];
    double dead_money;
    double stacks[PE_MONKER_MAX_PLAYERS];
} pe_monker_tree_header_t;

typedef struct
{
    uint32_t player_count;
    uint32_t combo_count;
    pe_range_t **players;
} pe_monker_range_set_t;

typedef enum
{
    PE_MONKER_MKR_OK = 0,
    PE_MONKER_MKR_ERR_NULL_ARGUMENT,
    PE_MONKER_MKR_ERR_OPEN,
    PE_MONKER_MKR_ERR_IO,
    PE_MONKER_MKR_ERR_BAD_ARCHIVE,
    PE_MONKER_MKR_ERR_TRUNCATED,
    PE_MONKER_MKR_ERR_BAD_ENCODING,
    PE_MONKER_MKR_ERR_UTF16LE_BOM,
    PE_MONKER_MKR_ERR_UNSUPPORTED,
    PE_MONKER_MKR_ERR_NO_MEMORY,
    PE_MONKER_MKR_ERR_TOO_LARGE
} pe_monker_mkr_status_t;

typedef struct
{
    char *name;
    uint16_t flags;
    uint16_t method;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
} pe_monker_mkr_entry_t;

typedef struct
{
    pe_monker_mkr_entry_t *entries;
    size_t count;
    void *private_data;
} pe_monker_mkr_t;

typedef struct
{
    char *game;
    int64_t iterations;
    uint32_t flop_buckets;
    double rakepercent;
    double rakecap;
    int32_t rakeflags;
} pe_monker_mkr_metadata_t;

typedef struct
{
    uint32_t bucket_count;
    uint32_t class_count;
    uint16_t *frequencies;
} pe_monker_mkr_strategy_t;

/** Read the fixed header at the beginning of a MonkerSolver .tree file. */
pe_monker_status_t pe_monker_tree_read_header(
    const char *path, pe_monker_tree_header_t *out);

const char *pe_monker_status_string(pe_monker_status_t status);

/**
 * Read the recursive node stream following the fixed header.
 *
 * The returned definition is owned by the caller and is released with
 * mpf_tree_free(). The stream is deliberately limited to the compact node
 * framing used by the reader: one action byte and one child-count byte per
 * node, in preorder.
 */
pe_monker_status_t pe_monker_tree_load(const char *path,
                                       struct mpf_tree_def_t **out_tree);

/** Read the optional fixed-point range block following a .tree node stream. */
pe_monker_status_t pe_monker_tree_read_ranges(const char *path,
                                              pe_monker_range_set_t *out);

void pe_monker_range_set_free(pe_monker_range_set_t *ranges);

/** Read the central directory of a MonkerSolver .mkr ZIP container. */
pe_monker_mkr_status_t pe_monker_mkr_read(const char *path,
                                          pe_monker_mkr_t *out);

void pe_monker_mkr_free(pe_monker_mkr_t *archive);

/** Extract one entry after validating and inflating its ZIP payload. */
pe_monker_mkr_status_t pe_monker_mkr_entry_read(
    const pe_monker_mkr_t *archive, size_t index,
    unsigned char **out_data, size_t *out_size);

/** Read the scalar Java-serialized entries in a saved run. */
pe_monker_mkr_status_t pe_monker_mkr_read_metadata(
    const pe_monker_mkr_t *archive, pe_monker_mkr_metadata_t *out);

void pe_monker_mkr_metadata_free(pe_monker_mkr_metadata_t *metadata);

/** Read a storedstrategyN entry; TC_NULL yields an empty strategy. */
pe_monker_mkr_status_t pe_monker_mkr_read_strategy(
    const pe_monker_mkr_t *archive, const char *entry_name,
    uint32_t bucket_count, pe_monker_mkr_strategy_t *out);

void pe_monker_mkr_strategy_free(pe_monker_mkr_strategy_t *strategy);

const char *pe_monker_mkr_status_string(pe_monker_mkr_status_t status);

typedef enum
{
    PE_MONKER_FILTER_OP_NONE = 0,
    PE_MONKER_FILTER_OP_GT,
    PE_MONKER_FILTER_OP_LT,
    PE_MONKER_FILTER_OP_EQ,
    PE_MONKER_FILTER_OP_GE,
    PE_MONKER_FILTER_OP_LE
} pe_monker_filter_operator_t;

typedef enum
{
    PE_MONKER_FILTER_OK = 0,
    PE_MONKER_FILTER_ERR_NULL_ARGUMENT,
    PE_MONKER_FILTER_ERR_EMPTY,
    PE_MONKER_FILTER_ERR_SYNTAX,
    PE_MONKER_FILTER_ERR_UNKNOWN_KEYWORD,
    PE_MONKER_FILTER_ERR_BAD_VALUE,
    PE_MONKER_FILTER_ERR_NO_MEMORY,
    PE_MONKER_FILTER_ERR_TOO_DEEP
} pe_monker_filter_status_t;

typedef struct
{
    char *keyword;
    char *value;
    pe_monker_filter_operator_t operator;
    unsigned negated;
    unsigned previous;
} pe_monker_filter_atom_t;

typedef struct
{
    pe_monker_filter_atom_t *atoms;
    size_t atom_count;
    size_t max_depth;
} pe_monker_filter_t;

pe_monker_filter_status_t pe_monker_filter_parse(
    const char *expression, pe_monker_filter_t *out);

void pe_monker_filter_free(pe_monker_filter_t *filter);

const char *pe_monker_filter_status_string(pe_monker_filter_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_MONKER_H */
