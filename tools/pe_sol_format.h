#ifndef PE_SOL_FORMAT_H
#define PE_SOL_FORMAT_H

/*
 * Minimal .pe_sol (PESOL001) header validation and row dequantization.
 * Shared by the desktop trainer (which links SDL2 only) and its unit tests.
 * Field layout mirrors src/engine/solvers/cfr/mpf_compact_storage.c:
 *
 *   magic[8] = "PESOL001", uint32 version, uint32 flags,
 *   uint64 infoset_count, uint64 reserved   (32 bytes total)
 *   then per infoset: uint64 key, uint32 n, uint16 q[n].
 *
 * flags: 0 = raw uint16 quantization, 1 = zstd-compressed payload.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PE_SOL_FMT_MAGIC "PESOL001"
#define PE_SOL_FMT_VERSION 1u
#define PE_SOL_FMT_HEADER_SIZE 32u
#define PE_SOL_FMT_FLAG_RAW 0u
#define PE_SOL_FMT_FLAG_ZSTD 1u

typedef enum {
    PE_SOL_FMT_OK = 0,
    PE_SOL_FMT_SHORT_HEADER, /* fewer than 32 bytes */
    PE_SOL_FMT_BAD_MAGIC,    /* magic or version mismatch */
    PE_SOL_FMT_COMPRESSED,   /* flags == 1: zstd payload */
    PE_SOL_FMT_BAD_FLAGS,    /* flags not in {0, 1} */
    PE_SOL_FMT_OVERFLOW      /* infoset_count cannot fit the file */
} pe_sol_fmt_status_t;

static uint32_t pe_sol_fmt_u32(const unsigned char *data, size_t offset)
{
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1u] << 8) |
           ((uint32_t)data[offset + 2u] << 16) |
           ((uint32_t)data[offset + 3u] << 24);
}

static uint16_t pe_sol_fmt_u16(const unsigned char *data, size_t offset)
{
    return (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8));
}

static uint64_t pe_sol_fmt_u64(const unsigned char *data, size_t offset)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8u; ++i)
        value |= (uint64_t)data[offset + i] << (i * 8u);
    return value;
}

/*
 * Validate magic/version/flags and return the infoset count. Every reader
 * must go through this so a compressed solution is rejected instead of
 * being misparsed as raw quantized records.
 */
static pe_sol_fmt_status_t pe_sol_fmt_parse_header(const unsigned char *data,
                                                   size_t size,
                                                   uint64_t *out_count)
{
    uint32_t flags;
    uint64_t count;
    if (data == NULL || out_count == NULL) return PE_SOL_FMT_SHORT_HEADER;
    if (size < PE_SOL_FMT_HEADER_SIZE) return PE_SOL_FMT_SHORT_HEADER;
    if (memcmp(data, PE_SOL_FMT_MAGIC, 8) != 0) return PE_SOL_FMT_BAD_MAGIC;
    if (pe_sol_fmt_u32(data, 8) != PE_SOL_FMT_VERSION) return PE_SOL_FMT_BAD_MAGIC;
    flags = pe_sol_fmt_u32(data, 12);
    if (flags == PE_SOL_FMT_FLAG_ZSTD) return PE_SOL_FMT_COMPRESSED;
    if (flags != PE_SOL_FMT_FLAG_RAW) return PE_SOL_FMT_BAD_FLAGS;
    count = pe_sol_fmt_u64(data, 16);
    /* Each record is at least 12 bytes (key + action count). */
    if (count > (size - PE_SOL_FMT_HEADER_SIZE) / 12u) return PE_SOL_FMT_OVERFLOW;
    *out_count = count;
    return PE_SOL_FMT_OK;
}

/*
 * Dequantize one quantized row and renormalize it to a probability
 * distribution, matching pe_sol_mmap_get_strategy: quantization keeps the
 * row sum near 65535 but not exactly on it, and an all-zero row falls back
 * to the uniform strategy.
 */
static void pe_sol_fmt_dequantize_row(const uint16_t *quantized, int actions,
                                      double *out_probabilities)
{
    double sum = 0.0;
    int i;
    if (actions <= 0 || quantized == NULL || out_probabilities == NULL) return;
    for (i = 0; i < actions; ++i) sum += (double)quantized[i];
    for (i = 0; i < actions; ++i) {
        out_probabilities[i] = (sum > 0.0)
                                   ? (double)quantized[i] / sum
                                   : 1.0 / (double)actions;
    }
}

#endif /* PE_SOL_FORMAT_H */
