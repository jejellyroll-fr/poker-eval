/*
 * mpf_compact_storage.c - Compact binary storage for solved game trees
 *
 * Implements the .pe_sol (solved strategy) and .pe_tree (tree definition)
 * storage formats described in issue #145:
 *
 *   - Fixed-point 16-bit quantization of strategy weights (>= 4x smaller than
 *     JSON / raw 64-bit float dumps, < 0.01% EV discrepancy).
 *   - Memory-mapped, read-only loading of .pe_sol files so a solved tree can be
 *     inspected without copying the full strategy array into heap RAM.
 */

#include <poker_eval/engine/solvers/cfr/mpf_compact_storage.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>

#ifdef PE_HAVE_ZSTD
#include <zstd.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#endif

/* ===========================================================================
 * File format
 * ===========================================================================
 *
 * .pe_sol layout (fixed-width little-endian integer fields on disk):
 *
 *   magic[8]      = "PESOL001"
 *   uint32_t version
 *   uint32_t flags        (0 = 16-bit fixed-point quantization)
 *   uint64_t infoset_count
 *   uint64_t reserved
 *   --- per infoset, sequentially: ---
 *     uint64_t key
 *     uint32_t n            (action count)
 *     uint16_t q[ n ]       (quantized probabilities, sum == 65535)
 *
 * The per-infoset records are stored back-to-back so the whole array of
 * (key, n, q[]) tuples can be memory-mapped and read directly, with only the
 * uint16_t -> double dequantization happening on demand.
 */

#define PE_SOL_MAGIC "PESOL001"
#define PE_SOL_VERSION 1

#define PE_TREE_MAGIC "PETREE001"
#define PE_TREE_VERSION 1

#define PE_SOL_QMAX 65535u /* max value of a quantized probability */

typedef struct
{
    char magic[8];
    uint32_t version;
    uint32_t flags;
    uint64_t infoset_count;
    uint64_t reserved;
} pe_sol_header_t;

/* Quantize a probability in [0,1] to a uint16_t in [0, PE_SOL_QMAX]. */
static uint16_t pe_quantize(double p)
{
    if (p < 0.0)
        p = 0.0;
    if (p > 1.0)
        p = 1.0;
    long v = lround(p * (double)PE_SOL_QMAX);
    if (v < 0)
        v = 0;
    if (v > (long)PE_SOL_QMAX)
        v = (long)PE_SOL_QMAX;
    return (uint16_t)v;
}

/* Dequantize a uint16_t back to a probability in [0,1]. */
static double pe_dequantize(uint16_t q)
{
    return (double)q / (double)PE_SOL_QMAX;
}

/* ===========================================================================
 * .pe_sol write
 * =========================================================================== */

typedef struct
{
    FILE *f;
    int failed;
} pe_sol_write_ctx_t;

/* Callback for cfr_storage_iterate: serialize one infoset. */
static void pe_sol_write_cb(uint64_t key,
                            int n,
                            const double *regret,
                            const double *avg,
                            void *user)
{
    (void)regret;
    pe_sol_write_ctx_t *ctx = (pe_sol_write_ctx_t *)user;
    if (ctx->failed || !ctx->f)
        return;
    if (n <= 0 || n > 4096)
        return;

    /* Normalize avg strategy to a probability distribution. */
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += avg[i];
    int uniform = sum <= 0.0;
    if (uniform)
        sum = (double)n;

    uint32_t un = (uint32_t)n;
    if (fwrite(&key, sizeof(uint64_t), 1, ctx->f) != 1 ||
        fwrite(&un, sizeof(uint32_t), 1, ctx->f) != 1)
    {
        ctx->failed = 1;
        return;
    }

    uint16_t *q = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)n);
    if (!q)
    {
        ctx->failed = 1;
        return;
    }
    double scaled_sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        q[i] = pe_quantize(uniform ? (1.0 / (double)n) : (avg[i] / sum));
        scaled_sum += q[i];
    }
    /* Guarantee the quantized row sums to PE_SOL_QMAX by adjusting the largest
     * bucket, eliminating systematic drift in the distribution. */
    if (n > 0 && fabs(scaled_sum - (double)PE_SOL_QMAX) > 0.0)
    {
        long diff = (long)PE_SOL_QMAX - (long)scaled_sum;
        int max_idx = 0;
        uint16_t max_v = 0;
        for (int i = 0; i < n; ++i)
        {
            if (q[i] > max_v)
            {
                max_v = q[i];
                max_idx = i;
            }
        }
        long nv = (long)q[max_idx] + diff;
        if (nv < 0)
            nv = 0;
        if (nv > (long)PE_SOL_QMAX)
            nv = (long)PE_SOL_QMAX;
        q[max_idx] = (uint16_t)nv;
    }

    if (fwrite(q, sizeof(uint16_t), (size_t)n, ctx->f) != (size_t)n)
        ctx->failed = 1;

    free(q);
}

int pe_cfr_save_storage(cfr_storage_t *storage, const char *path)
{
    if (!storage || !path || !*path)
    {
        errno = EINVAL;
        return -1;
    }

    size_t count = cfr_storage_count_infosets(storage);

    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    pe_sol_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    for (size_t i = 0u; i < sizeof(hdr.magic); ++i)
        hdr.magic[i] = PE_SOL_MAGIC[i];
    hdr.version = PE_SOL_VERSION;
    hdr.flags = 0; /* 16-bit fixed-point quantization */
    hdr.infoset_count = (uint64_t)count;
    hdr.reserved = 0;

    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1)
    {
        int err = errno;
        fclose(f);
        errno = err;
        return -1;
    }

    pe_sol_write_ctx_t ctx;
    ctx.f = f;
    ctx.failed = 0;
    cfr_storage_iterate(storage, pe_sol_write_cb, &ctx);

    if (ctx.failed)
    {
        int err = errno ? errno : EIO;
        fclose(f);
        errno = err;
        return -1;
    }

    if (fclose(f) != 0)
        return -1;
    return 0;
}

int pe_cfr_save_storage_zstd(cfr_storage_t *storage, const char *path, int level)
{
#ifndef PE_HAVE_ZSTD
    (void)storage;
    (void)path;
    (void)level;
    errno = ENOTSUP;
    return -1;
#else
    FILE *f;
    unsigned char *raw = NULL;
    unsigned char *compressed = NULL;
    size_t raw_size;
    size_t payload_size;
    size_t compressed_size;
    pe_sol_header_t hdr;
    int result = -1;

    if (!storage || !path || !*path)
    {
        errno = EINVAL;
        return -1;
    }
    /* Reuse the canonical writer so compressed and mmap-readable snapshots
       have byte-for-byte identical records after decompression. If the
       compression step fails, the caller still has a valid uncompressed file. */
    if (pe_cfr_save_storage(storage, path) != 0)
        return -1;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fseek(f, 0L, SEEK_END) != 0)
        goto cleanup_read;
    long end = ftell(f);
    if (end < 0 || (unsigned long)end > (unsigned long)SIZE_MAX)
        goto cleanup_read;
    raw_size = (size_t)end;
    if (raw_size < sizeof(hdr) || fseek(f, 0L, SEEK_SET) != 0)
        goto cleanup_read;
    raw = (unsigned char *)malloc(raw_size);
    if (!raw || fread(raw, 1u, raw_size, f) != raw_size)
        goto cleanup_read;
    for (size_t i = 0u; i < sizeof(hdr); ++i)
        ((unsigned char *)&hdr)[i] = raw[i];
    if (memcmp(hdr.magic, PE_SOL_MAGIC, sizeof(hdr.magic)) != 0 ||
        hdr.version != PE_SOL_VERSION || hdr.flags != 0u)
    {
        errno = EINVAL;
        goto cleanup_read;
    }
    payload_size = raw_size - sizeof(hdr);
    compressed_size = ZSTD_compressBound(payload_size);
    compressed = (unsigned char *)malloc(compressed_size);
    if (!compressed)
    {
        errno = ENOMEM;
        goto cleanup_read;
    }
    compressed_size = ZSTD_compress(compressed, compressed_size,
                                    raw + sizeof(hdr), payload_size,
                                    level == 0 ? 0 : level);
    if (ZSTD_isError(compressed_size))
    {
        errno = EIO;
        goto cleanup_read;
    }
    hdr.flags = 1u;
    hdr.reserved = (uint64_t)payload_size;
    if (fclose(f) != 0)
    {
        f = NULL;
        goto cleanup_read;
    }
    f = fopen(path, "wb");
    if (f == NULL)
        goto cleanup_write;
    if (fwrite(&hdr, sizeof(hdr), 1u, f) != 1u ||
        fwrite(compressed, 1u, compressed_size, f) != compressed_size)
        goto cleanup_write;
    result = 0;

cleanup_write:
    if (f != NULL) {
        if (fclose(f) != 0)
            result = -1;
        f = NULL;
    }
cleanup_read:
    if (f != NULL)
        fclose(f);
    free(compressed);
    free(raw);
    return result;
#endif
}

/* ===========================================================================
 * .pe_sol read (heap copy)
 * =========================================================================== */

int pe_cfr_load_storage(cfr_storage_t *storage, const char *path)
{
    if (!storage || !path || !*path)
    {
        errno = EINVAL;
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    pe_sol_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1)
    {
        int err = errno;
        fclose(f);
        errno = err;
        return -1;
    }
    if (memcmp(hdr.magic, PE_SOL_MAGIC, 8) != 0 || hdr.version != PE_SOL_VERSION)
    {
        fclose(f);
        errno = EINVAL;
        return -1;
    }
    if (hdr.flags == 1u)
    {
#ifdef PE_HAVE_ZSTD
        unsigned char *compressed = NULL;
        unsigned char *payload = NULL;
        size_t compressed_size;
        size_t payload_size;
        long end;
        FILE *decoded;

        if (hdr.reserved == 0u || hdr.reserved > (uint64_t)SIZE_MAX ||
            fseek(f, 0L, SEEK_END) != 0)
        {
            fclose(f);
            errno = EINVAL;
            return -1;
        }
        end = ftell(f);
        if (end < 0 || (uint64_t)end < (uint64_t)sizeof(hdr))
        {
            fclose(f);
            errno = EINVAL;
            return -1;
        }
        compressed_size = (size_t)((uint64_t)end - (uint64_t)sizeof(hdr));
        payload_size = (size_t)hdr.reserved;
        if (fseek(f, (long)sizeof(hdr), SEEK_SET) != 0)
        {
            fclose(f);
            errno = EIO;
            return -1;
        }
        compressed = (unsigned char *)malloc(compressed_size);
        payload = (unsigned char *)malloc(payload_size);
        if (!compressed || !payload ||
            fread(compressed, 1u, compressed_size, f) != compressed_size)
        {
            free(compressed);
            free(payload);
            fclose(f);
            errno = ENOMEM;
            return -1;
        }
        size_t decoded_size = ZSTD_decompress(payload, payload_size,
                                               compressed, compressed_size);
        free(compressed);
        if (ZSTD_isError(decoded_size) || decoded_size != payload_size)
        {
            free(payload);
            fclose(f);
            errno = EINVAL;
            return -1;
        }
        fclose(f);
        f = NULL;
        decoded = tmpfile();
        if (!decoded || fwrite(payload, 1u, payload_size, decoded) != payload_size ||
            fseek(decoded, 0L, SEEK_SET) != 0)
        {
            free(payload);
            if (decoded)
                fclose(decoded);
            errno = EIO;
            return -1;
        }
        free(payload);
        f = decoded;
#else
        fclose(f);
        errno = ENOTSUP;
        return -1;
#endif
    }
    else if (hdr.flags != 0u)
    {
        fclose(f);
        errno = ENOTSUP;
        return -1;
    }

    for (uint64_t idx = 0; idx < hdr.infoset_count; ++idx)
    {
        uint64_t key = 0;
        uint32_t n = 0;
        if (fread(&key, sizeof(uint64_t), 1, f) != 1 ||
            fread(&n, sizeof(uint32_t), 1, f) != 1)
        {
            int err = errno;
            fclose(f);
            errno = err;
            return -1;
        }
        if (n == 0 || n > 4096)
        {
            fclose(f);
            errno = EINVAL;
            return -1;
        }
        uint16_t *q = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)n);
        if (!q)
        {
            fclose(f);
            errno = ENOMEM;
            return -1;
        }
        if (fread(q, sizeof(uint16_t), (size_t)n, f) != (size_t)n)
        {
            int err = errno;
            free(q);
            fclose(f);
            errno = err;
            return -1;
        }

        /* Convert quantized weights back into accumulated avg-strategy values
         * (relative weights) so cfr_storage_get_avg_strategy reproduces the
         * normalized strategy. */
        double deq_sum = 0.0;
        for (uint32_t i = 0; i < n; ++i)
            deq_sum += pe_dequantize(q[i]);
        double *avg = (double *)malloc(sizeof(double) * (size_t)n);
        if (!avg)
        {
            free(q);
            fclose(f);
            errno = ENOMEM;
            return -1;
        }
        for (uint32_t i = 0; i < n; ++i)
        {
            double p = (deq_sum > 0.0) ? (pe_dequantize(q[i]) / deq_sum)
                                       : (1.0 / (double)n);
            avg[i] = p * (double)PE_SOL_QMAX; /* uniform scale; ratios preserved */
        }

        cfr_storage_update_avg(storage, key, (int)n, avg, 1.0);
        free(avg);
        free(q);
    }

    if (fclose(f) != 0)
        return -1;
    return 0;
}

/* ===========================================================================
 * .pe_sol memory-mapped read-only view
 * =========================================================================== */

struct pe_sol_mmap_t
{
#if defined(_WIN32)
    HANDLE file_handle;
    HANDLE map_handle;
#else
    int fd;
#endif
    unsigned char *base;
    size_t size;
    size_t infoset_count;
    /* Offsets (from base) to each infoset record, computed at open time. */
    const unsigned char **records;
};

#if defined(_WIN32)
static int pe_sol_map_file(const char *path, pe_sol_mmap_t *view)
{
    view->file_handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (view->file_handle == INVALID_HANDLE_VALUE)
    {
        errno = ENOENT;
        return -1;
    }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(view->file_handle, &sz))
    {
        CloseHandle(view->file_handle);
        errno = EIO;
        return -1;
    }
    view->size = (size_t)sz.QuadPart;
    view->map_handle = CreateFileMappingA(view->file_handle, NULL, PAGE_READONLY,
                                          0, 0, NULL);
    if (view->map_handle == NULL)
    {
        CloseHandle(view->file_handle);
        errno = EIO;
        return -1;
    }
    view->base = (unsigned char *)MapViewOfFile(view->map_handle,
                                                      FILE_MAP_READ, 0, 0, 0);
    if (!view->base)
    {
        CloseHandle(view->map_handle);
        CloseHandle(view->file_handle);
        errno = EIO;
        return -1;
    }
    return 0;
}
#else
static int pe_sol_map_file(const char *path, pe_sol_mmap_t *view)
{
    view->fd = open(path, O_RDONLY);
    if (view->fd < 0)
        return -1;
    struct stat st;
    if (fstat(view->fd, &st) != 0)
    {
        int err = errno;
        close(view->fd);
        errno = err;
        return -1;
    }
    view->size = (size_t)st.st_size;
    if (view->size == 0)
    {
        close(view->fd);
        errno = EINVAL;
        return -1;
    }
    void *m = mmap(NULL, view->size, PROT_READ, MAP_PRIVATE, view->fd, 0);
    if (m == MAP_FAILED)
    {
        int err = errno;
        close(view->fd);
        errno = err;
        return -1;
    }
    view->base = (unsigned char *)m;
    return 0;
}
#endif

/* Bounds-checked little-endian readers. */
static uint64_t pe_rd_u64(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}
static uint32_t pe_rd_u32(const unsigned char *p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= ((uint32_t)p[i]) << (8 * i);
    return v;
}
static uint16_t pe_rd_u16(const unsigned char *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

#define PE_SOL_HDR_SIZE (8 + 4 + 4 + 8 + 8)

int pe_sol_open_mmap(const char *path, pe_sol_mmap_t **out_view)
{
    if (!path || !*path || !out_view)
    {
        errno = EINVAL;
        return -1;
    }
    pe_sol_mmap_t *view = (pe_sol_mmap_t *)calloc(1, sizeof(pe_sol_mmap_t));
    if (!view)
    {
        errno = ENOMEM;
        return -1;
    }
    if (pe_sol_map_file(path, view) != 0)
    {
        int err = errno;
        free(view);
        errno = err;
        return -1;
    }
    if (view->size < PE_SOL_HDR_SIZE ||
        memcmp(view->base, PE_SOL_MAGIC, 8) != 0 ||
        pe_rd_u32(view->base + 8) != PE_SOL_VERSION ||
        pe_rd_u32(view->base + 12) != 0)
    {
#if defined(_WIN32)
        UnmapViewOfFile(view->base);
        CloseHandle(view->map_handle);
        CloseHandle(view->file_handle);
#else
        munmap((void *)view->base, view->size);
        close(view->fd);
#endif
        free(view);
        errno = EINVAL;
        return -1;
    }

    uint64_t raw_infoset_count = pe_rd_u64(view->base + 16);
    if (raw_infoset_count > (uint64_t)SIZE_MAX ||
        raw_infoset_count > SIZE_MAX / sizeof(*view->records) ||
        raw_infoset_count > (view->size - PE_SOL_HDR_SIZE) / 12)
    {
#if defined(_WIN32)
        UnmapViewOfFile(view->base);
        CloseHandle(view->map_handle);
        CloseHandle(view->file_handle);
#else
        munmap((void *)view->base, view->size);
        close(view->fd);
#endif
        free(view);
        errno = EINVAL;
        return -1;
    }
    view->infoset_count = (size_t)raw_infoset_count;

    if (view->infoset_count > 0)
    {
        view->records = (const unsigned char **)malloc(
            sizeof(const unsigned char *) * view->infoset_count);
        if (!view->records)
        {
#if defined(_WIN32)
            UnmapViewOfFile(view->base);
            CloseHandle(view->map_handle);
            CloseHandle(view->file_handle);
#else
            munmap((void *)view->base, view->size);
            close(view->fd);
#endif
            free(view);
            errno = ENOMEM;
            return -1;
        }
    }

    const unsigned char *p = view->base + PE_SOL_HDR_SIZE;
    const unsigned char *end = view->base + view->size;
    int truncated = 0;
    for (size_t i = 0; i < view->infoset_count; ++i)
    {
        if (p + 12 > end)
        {
            /* Record header is incomplete: the file is truncated/corrupt. */
            truncated = 1;
            break;
        }
        view->records[i] = p;
        uint32_t n = pe_rd_u32(p + 8);
        if (n == 0 || n > 4096)
        {
            /* Invalid action count: reject the whole file. */
            truncated = 1;
            break;
        }
        const unsigned char *next = p + 12 + (size_t)n * sizeof(uint16_t);
        if (next > end)
        {
            /* Declared quantized payload runs past the end of the mapping. */
            truncated = 1;
            break;
        }
        p = next;
    }

    if (truncated)
    {
#if defined(_WIN32)
        UnmapViewOfFile(view->base);
        CloseHandle(view->map_handle);
        CloseHandle(view->file_handle);
#else
        munmap((void *)view->base, view->size);
        close(view->fd);
#endif
        free(view->records);
        free(view);
        errno = EINVAL;
        return -1;
    }

    *out_view = view;
    return 0;
}

size_t pe_sol_mmap_infoset_count(const pe_sol_mmap_t *view)
{
    return view ? view->infoset_count : 0;
}

int pe_sol_mmap_get_strategy(const pe_sol_mmap_t *view,
                             size_t index,
                             uint64_t *out_key,
                             int max_actions,
                             double *out_probs,
                             int *out_n)
{
    if (!view || !out_probs || index >= view->infoset_count)
    {
        errno = EINVAL;
        return -1;
    }
    const unsigned char *rec = view->records[index];
    uint64_t key = pe_rd_u64(rec);
    uint32_t n = pe_rd_u32(rec + 8);
    /* Compare as unsigned: n is uint32_t, so a value above INT_MAX would make
     * (int)n negative and slip past a signed comparison, then let the write
     * loop run off the end of the caller's out_probs buffer (stack-buffer
     * overflow). The loader already caps n at 4096, but reject defensively. */
    if (n > (uint32_t)max_actions)
    {
        errno = ERANGE;
        return -1;
    }
    if (out_key)
        *out_key = key;
    if (out_n)
        *out_n = (int)n;

    const unsigned char *q = rec + 12;
    double deq_sum = 0.0;
    for (uint32_t i = 0; i < n; ++i)
        deq_sum += pe_dequantize(pe_rd_u16(q + (size_t)i * 2));
    /* Never write past max_actions even if the caller disagrees with n. */
    uint32_t w = n < (uint32_t)max_actions ? n : (uint32_t)max_actions;
    for (uint32_t i = 0; i < w; ++i)
    {
        double pr = (deq_sum > 0.0)
                        ? (pe_dequantize(pe_rd_u16(q + (size_t)i * 2)) / deq_sum)
                        : (1.0 / (double)n);
        out_probs[i] = pr;
    }
    return 0;
}

void pe_sol_close_mmap(pe_sol_mmap_t *view)
{
    if (!view)
        return;
    free(view->records);
#if defined(_WIN32)
    if (view->base)
        UnmapViewOfFile(view->base);
    if (view->map_handle)
        CloseHandle(view->map_handle);
    if (view->file_handle != INVALID_HANDLE_VALUE)
        CloseHandle(view->file_handle);
#else
    if (view->base)
        munmap((void *)view->base, view->size);
    if (view->fd >= 0)
        close(view->fd);
#endif
    free(view);
}

/* ===========================================================================
 * .pe_tree compact serialization
 * ===========================================================================
 *
 * A compact binary encoding of an mpf_tree_def_t. Records are written
 * sequentially with length-prefixed strings, so the file is far smaller than
 * the equivalent JSON and round-trips exactly.
 *
 * Layout:
 *   magic[8]="PETREE001"
 *   uint32 version
 *   int32  tree_version
 *   int32  root_index
 *   int32  node_count
 *   --- nodes[] ---
 *   int32  profile_count
 *   --- bet profiles[] ---
 *   int32  range_profile_count
 *   --- range profiles[] ---
 */

struct pe_tree_writer
{
    FILE *f;
    int failed;
};

static int pe_wr_i32(struct pe_tree_writer *w, int32_t v)
{
    uint32_t u = (uint32_t)v;
    unsigned char b[4];
    b[0] = (unsigned char)(u & 0xFF);
    b[1] = (unsigned char)((u >> 8) & 0xFF);
    b[2] = (unsigned char)((u >> 16) & 0xFF);
    b[3] = (unsigned char)((u >> 24) & 0xFF);
    if (fwrite(b, 1, 4, w->f) != 4)
    {
        w->failed = 1;
        return -1;
    }
    return 0;
}

static int pe_wr_u64(struct pe_tree_writer *w, uint64_t u)
{
    unsigned char b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = (unsigned char)((u >> (8 * i)) & 0xFF);
    if (fwrite(b, 1, 8, w->f) != 8)
    {
        w->failed = 1;
        return -1;
    }
    return 0;
}

static int pe_wr_str(struct pe_tree_writer *w, const char *s)
{
    if (!s)
        return pe_wr_i32(w, -1);
    size_t len = strlen(s);
    if (len > 0x7FFFFFFF)
        len = 0x7FFFFFFF;
    if (pe_wr_i32(w, (int32_t)len) != 0)
        return -1;
    if (len > 0 && fwrite(s, 1, len, w->f) != len)
    {
        w->failed = 1;
        return -1;
    }
    return 0;
}

static int pe_wr_dbl(struct pe_tree_writer *w, double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof(u));
    unsigned char b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = (unsigned char)((u >> (8 * i)) & 0xFF);
    if (fwrite(b, 1, 8, w->f) != 8)
    {
        w->failed = 1;
        return -1;
    }
    return 0;
}

static int pe_write_bet_profile(struct pe_tree_writer *w,
                                const mpf_tree_bet_profile_t *p)
{
    if (pe_wr_str(w, p->id) != 0)
        return -1;
    if (pe_wr_i32(w, p->bet_size_count) != 0)
        return -1;
    for (int i = 0; i < p->bet_size_count; ++i)
        if (pe_wr_dbl(w, p->bet_sizes[i]) != 0)
            return -1;
    if (pe_wr_i32(w, p->use_pot_sizing) != 0)
        return -1;
    return 0;
}

static int pe_write_range_profile(struct pe_tree_writer *w,
                                  const mpf_tree_range_profile_t *p)
{
    if (pe_wr_str(w, p->id) != 0)
        return -1;
    if (pe_wr_i32(w, p->player) != 0)
        return -1;
    if (pe_wr_i32(w, p->street) != 0)
        return -1;
    if (pe_wr_i32(w, p->street_defined) != 0)
        return -1;
    if (pe_wr_i32(w, p->combo_count) != 0)
        return -1;
    for (int i = 0; i < p->combo_count; ++i)
    {
        if (pe_wr_str(w, p->combos[i].hand) != 0)
            return -1;
        if (pe_wr_dbl(w, p->combos[i].weight) != 0)
            return -1;
    }
    if (pe_wr_i32(w, p->alias_count) != 0)
        return -1;
    for (int i = 0; i < p->alias_count; ++i)
        if (pe_wr_str(w, p->aliases[i]) != 0)
            return -1;
    return 0;
}

static int pe_write_actions(struct pe_tree_writer *w,
                            const mpf_tree_action_t *acts,
                            int count)
{
    if (pe_wr_i32(w, count) != 0)
        return -1;
    for (int i = 0; i < count; ++i)
    {
        if (pe_wr_i32(w, acts[i].type) != 0)
            return -1;
        if (pe_wr_i32(w, acts[i].size_index) != 0)
            return -1;
        if (pe_wr_dbl(w, acts[i].weight) != 0)
            return -1;
        if (pe_wr_i32(w, acts[i].next_index) != 0)
            return -1;
        if (pe_wr_str(w, acts[i].next_id) != 0)
            return -1;
    }
    return 0;
}

static int pe_write_node(struct pe_tree_writer *w, const mpf_tree_node_t *n)
{
    if (pe_wr_str(w, n->id) != 0)
        return -1;
    if (pe_wr_i32(w, n->type) != 0)
        return -1;
    if (pe_wr_i32(w, n->street) != 0)
        return -1;
    if (pe_wr_i32(w, n->acting_player) != 0)
        return -1;
    if (pe_wr_i32(w, n->bet_size_count) != 0)
        return -1;
    for (int i = 0; i < n->bet_size_count; ++i)
        if (pe_wr_dbl(w, n->bet_sizes[i]) != 0)
            return -1;
    if (pe_wr_i32(w, n->use_pot_sizing) != 0)
        return -1;
    if (pe_write_actions(w, n->actions, n->action_count) != 0)
        return -1;
    if (pe_wr_str(w, n->bet_profile_id) != 0)
        return -1;
    if (pe_wr_str(w, n->range_profile_id) != 0)
        return -1;
    if (pe_wr_i32(w, n->has_snapshot) != 0)
        return -1;
    if (n->has_snapshot)
    {
        const mpf_tree_snapshot_t *s = &n->snapshot;
        if (s->board_len < 0 || s->board_len > 5 ||
            s->stacks_len < 0 || s->stacks_len > MPF_MAX_PLAYERS ||
            s->invested_len < 0 || s->invested_len > MPF_MAX_PLAYERS ||
            s->round_contrib_len < 0 || s->round_contrib_len > MPF_MAX_PLAYERS ||
            s->active_len < 0 || s->active_len > MPF_MAX_PLAYERS ||
            s->acted_len < 0 || s->acted_len > MPF_MAX_PLAYERS)
            return -1;
        /* defined + all has_* flags */
        if (pe_wr_i32(w, s->defined) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_street) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_num_players) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_to_act) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_first_to_act) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_pot) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_to_call) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_current_bet) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_raises_made) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_board) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_board_revealed) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_stacks) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_invested) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_round_contrib) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_active) != 0)
            return -1;
        if (pe_wr_i32(w, s->has_acted) != 0)
            return -1;
        /* scalars (bounded lengths) */
        if (pe_wr_i32(w, s->board_len) != 0)
            return -1;
        if (pe_wr_i32(w, s->stacks_len) != 0)
            return -1;
        if (pe_wr_i32(w, s->invested_len) != 0)
            return -1;
        if (pe_wr_i32(w, s->round_contrib_len) != 0)
            return -1;
        if (pe_wr_i32(w, s->active_len) != 0)
            return -1;
        if (pe_wr_i32(w, s->acted_len) != 0)
            return -1;
        if (pe_wr_i32(w, (int32_t)s->street) != 0)
            return -1;
        if (pe_wr_i32(w, s->num_players) != 0)
            return -1;
        if (pe_wr_i32(w, s->to_act) != 0)
            return -1;
        if (pe_wr_i32(w, s->first_to_act) != 0)
            return -1;
        if (pe_wr_dbl(w, s->pot) != 0)
            return -1;
        if (pe_wr_dbl(w, s->to_call) != 0)
            return -1;
        if (pe_wr_dbl(w, s->current_bet) != 0)
            return -1;
        if (pe_wr_i32(w, s->raises_made) != 0)
            return -1;
        if (pe_wr_i32(w, s->board_revealed) != 0)
            return -1;
        /* board cards (fixed 5) */
        for (int i = 0; i < 5; ++i)
            if (pe_wr_i32(w, s->board_cards[i]) != 0)
                return -1;
        /* per-player arrays (bounded by MPF_MAX_PLAYERS at read time) */
        for (int i = 0; i < s->stacks_len; ++i)
            if (pe_wr_dbl(w, s->stacks[i]) != 0)
                return -1;
        for (int i = 0; i < s->invested_len; ++i)
            if (pe_wr_dbl(w, s->invested[i]) != 0)
                return -1;
        for (int i = 0; i < s->round_contrib_len; ++i)
            if (pe_wr_dbl(w, s->round_contrib[i]) != 0)
                return -1;
        for (int i = 0; i < s->active_len; ++i)
            if (pe_wr_i32(w, s->active[i]) != 0)
                return -1;
        for (int i = 0; i < s->acted_len; ++i)
            if (pe_wr_i32(w, s->acted[i]) != 0)
                return -1;
    }
    if (pe_wr_u64(w, n->state_key) != 0)
        return -1;
    return 0;
}

int pe_tree_save(const mpf_tree_def_t *tree, const char *path)
{
    if (!tree || !path || !*path)
    {
        errno = EINVAL;
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    struct
    {
        char magic[8];
        uint32_t version;
    } hdr;
    for (size_t i = 0u; i < sizeof(hdr.magic); ++i)
        hdr.magic[i] = PE_TREE_MAGIC[i];
    hdr.version = PE_TREE_VERSION;
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1)
    {
        int err = errno;
        fclose(f);
        errno = err;
        return -1;
    }

    struct pe_tree_writer w;
    w.f = f;
    w.failed = 0;

    if (pe_wr_i32(&w, tree->version) != 0 ||
        pe_wr_i32(&w, tree->root_index) != 0 ||
        pe_wr_i32(&w, tree->node_count) != 0)
    {
        fclose(f);
        errno = EIO;
        return -1;
    }
    for (int i = 0; i < tree->node_count && !w.failed; ++i)
        pe_write_node(&w, &tree->nodes[i]);

    if (pe_wr_i32(&w, tree->profile_count) != 0)
        w.failed = 1;
    for (int i = 0; i < tree->profile_count && !w.failed; ++i)
        if (pe_write_bet_profile(&w, &tree->profiles[i]) != 0)
            break;

    if (pe_wr_i32(&w, tree->range_profile_count) != 0)
        w.failed = 1;
    for (int i = 0; i < tree->range_profile_count && !w.failed; ++i)
        if (pe_write_range_profile(&w, &tree->range_profiles[i]) != 0)
            break;

    if (w.failed)
    {
        int err = errno ? errno : EIO;
        fclose(f);
        errno = err;
        return -1;
    }
    if (fclose(f) != 0)
        return -1;
    return 0;
}

/* ---- .pe_tree reader ---- */

typedef struct
{
    const unsigned char *p;
    const unsigned char *end;
    int failed;
} pe_tree_reader_t;

static int32_t pe_rd_i32(pe_tree_reader_t *r)
{
    if (r->p + 4 > r->end)
    {
        r->failed = 1;
        return 0;
    }
    uint32_t u = pe_rd_u32(r->p);
    r->p += 4;
    return (int32_t)u;
}

static double pe_rd_double(pe_tree_reader_t *r)
{
    if (r->p + 8 > r->end)
    {
        r->failed = 1;
        return 0.0;
    }
    uint64_t u = pe_rd_u64(r->p);
    double d;
    memcpy(&d, &u, sizeof(d));
    r->p += 8;
    return d;
}

static char *pe_rd_string(pe_tree_reader_t *r)
{
    int32_t len = pe_rd_i32(r);
    if (r->failed)
        return NULL;
    if (len < 0)
        return NULL;
    char *s = (char *)malloc((size_t)len + 1);
    if (!s)
    {
        r->failed = 1;
        return NULL;
    }
    if (len > 0)
    {
        if (r->p + (size_t)len > r->end)
        {
            free(s);
            r->failed = 1;
            return NULL;
        }
        memcpy(s, r->p, (size_t)len);
        r->p += (size_t)len;
    }
    s[len] = '\0';
    return s;
}

static mpf_tree_bet_profile_t *pe_read_bet_profiles(pe_tree_reader_t *r, int *count)
{
    int n = pe_rd_i32(r);
    if (r->failed || n < 0)
    {
        *count = 0;
        return NULL;
    }
    *count = n;
    if (n == 0)
        return NULL;
    mpf_tree_bet_profile_t *arr = (mpf_tree_bet_profile_t *)calloc(
        (size_t)n, sizeof(mpf_tree_bet_profile_t));
    if (!arr)
    {
        r->failed = 1;
        return NULL;
    }
    for (int i = 0; i < n; ++i)
    {
        arr[i].id = pe_rd_string(r);
        arr[i].bet_size_count = pe_rd_i32(r);
        if (arr[i].bet_size_count > 0)
        {
            arr[i].bet_sizes = (double *)malloc(sizeof(double) * (size_t)arr[i].bet_size_count);
            if (!arr[i].bet_sizes)
            {
                r->failed = 1;
                return arr;
            }
            for (int j = 0; j < arr[i].bet_size_count; ++j)
                arr[i].bet_sizes[j] = pe_rd_double(r);
        }
        arr[i].use_pot_sizing = pe_rd_i32(r);
    }
    return arr;
}

static mpf_tree_range_profile_t *pe_read_range_profiles(pe_tree_reader_t *r, int *count)
{
    int n = pe_rd_i32(r);
    if (r->failed || n < 0)
    {
        *count = 0;
        return NULL;
    }
    *count = n;
    if (n == 0)
        return NULL;
    mpf_tree_range_profile_t *arr = (mpf_tree_range_profile_t *)calloc(
        (size_t)n, sizeof(mpf_tree_range_profile_t));
    if (!arr)
    {
        r->failed = 1;
        return NULL;
    }
    for (int i = 0; i < n; ++i)
    {
        arr[i].id = pe_rd_string(r);
        arr[i].player = pe_rd_i32(r);
        arr[i].street = (mpf_street_t)(int)pe_rd_i32(r);
        arr[i].street_defined = pe_rd_i32(r);
        arr[i].combo_count = pe_rd_i32(r);
        if (arr[i].combo_count > 0)
        {
            arr[i].combos = (mpf_tree_range_combo_t *)calloc(
                (size_t)arr[i].combo_count, sizeof(mpf_tree_range_combo_t));
            if (!arr[i].combos)
            {
                r->failed = 1;
                return arr;
            }
            for (int j = 0; j < arr[i].combo_count; ++j)
            {
                arr[i].combos[j].hand = pe_rd_string(r);
                arr[i].combos[j].weight = pe_rd_double(r);
            }
        }
        arr[i].alias_count = pe_rd_i32(r);
        if (arr[i].alias_count > 0)
        {
            arr[i].aliases = (char **)calloc((size_t)arr[i].alias_count, sizeof(char *));
            if (!arr[i].aliases)
            {
                r->failed = 1;
                return arr;
            }
            for (int j = 0; j < arr[i].alias_count; ++j)
                arr[i].aliases[j] = pe_rd_string(r);
        }
    }
    return arr;
}

static int pe_read_actions(pe_tree_reader_t *r, mpf_tree_node_t *n)
{
    n->action_count = pe_rd_i32(r);
    if (r->failed || n->action_count < 0)
        return -1;
    if (n->action_count == 0)
    {
        n->actions = NULL;
        return 0;
    }
    n->actions = (mpf_tree_action_t *)calloc((size_t)n->action_count,
                                             sizeof(mpf_tree_action_t));
    if (!n->actions)
    {
        r->failed = 1;
        return -1;
    }
    for (int i = 0; i < n->action_count; ++i)
    {
        n->actions[i].type = (mpf_tree_action_type_t)(int)pe_rd_i32(r);
        n->actions[i].size_index = pe_rd_i32(r);
        n->actions[i].weight = pe_rd_double(r);
        n->actions[i].next_index = pe_rd_i32(r);
        n->actions[i].next_id = pe_rd_string(r);
    }
    return 0;
}

static mpf_tree_node_t *pe_read_nodes(pe_tree_reader_t *r, int *count)
{
    int n = pe_rd_i32(r);
    if (r->failed || n < 0)
    {
        *count = 0;
        return NULL;
    }
    *count = n;
    if (n == 0)
        return NULL;
    mpf_tree_node_t *arr = (mpf_tree_node_t *)calloc((size_t)n, sizeof(mpf_tree_node_t));
    if (!arr)
    {
        r->failed = 1;
        return NULL;
    }
#if !defined(_WIN32)
    for (int i = 0; i < n; ++i)
    {
        if (pthread_mutex_init(&arr[i].cache_lock, NULL) != 0)
        {
            for (int j = 0; j < i; ++j)
                pthread_mutex_destroy(&arr[j].cache_lock);
            free(arr);
            r->failed = 1;
            return NULL;
        }
    }
#endif
    for (int i = 0; i < n; ++i)
    {
        mpf_tree_node_t *nd = &arr[i];
        nd->id = pe_rd_string(r);
        nd->type = (mpf_tree_node_type_t)(int)pe_rd_i32(r);
        nd->street = (mpf_street_t)(int)pe_rd_i32(r);
        nd->acting_player = pe_rd_i32(r);
        nd->bet_size_count = pe_rd_i32(r);
        if (nd->bet_size_count > 0)
        {
            nd->bet_sizes = (double *)malloc(sizeof(double) * (size_t)nd->bet_size_count);
            if (!nd->bet_sizes)
            {
                r->failed = 1;
                return arr;
            }
            for (int j = 0; j < nd->bet_size_count; ++j)
                nd->bet_sizes[j] = pe_rd_double(r);
        }
        nd->use_pot_sizing = pe_rd_i32(r);
        if (pe_read_actions(r, nd) != 0)
            return arr;
        nd->bet_profile_id = pe_rd_string(r);
        nd->range_profile_id = pe_rd_string(r);
        nd->has_snapshot = pe_rd_i32(r);
        if (nd->has_snapshot)
        {
            mpf_tree_snapshot_t *s = &nd->snapshot;
            s->defined = pe_rd_i32(r);
            s->has_street = pe_rd_i32(r);
            s->has_num_players = pe_rd_i32(r);
            s->has_to_act = pe_rd_i32(r);
            s->has_first_to_act = pe_rd_i32(r);
            s->has_pot = pe_rd_i32(r);
            s->has_to_call = pe_rd_i32(r);
            s->has_current_bet = pe_rd_i32(r);
            s->has_raises_made = pe_rd_i32(r);
            s->has_board = pe_rd_i32(r);
            s->has_board_revealed = pe_rd_i32(r);
            s->has_stacks = pe_rd_i32(r);
            s->has_invested = pe_rd_i32(r);
            s->has_round_contrib = pe_rd_i32(r);
            s->has_active = pe_rd_i32(r);
            s->has_acted = pe_rd_i32(r);
            s->board_len = pe_rd_i32(r);
            s->stacks_len = pe_rd_i32(r);
            s->invested_len = pe_rd_i32(r);
            s->round_contrib_len = pe_rd_i32(r);
            s->active_len = pe_rd_i32(r);
            s->acted_len = pe_rd_i32(r);
            s->street = (mpf_street_t)(int)pe_rd_i32(r);
            s->num_players = pe_rd_i32(r);
            s->to_act = pe_rd_i32(r);
            s->first_to_act = pe_rd_i32(r);
            s->pot = pe_rd_double(r);
            s->to_call = pe_rd_double(r);
            s->current_bet = pe_rd_double(r);
            s->raises_made = pe_rd_i32(r);
            s->board_revealed = pe_rd_i32(r);
            /* board cards: fixed 5, reject malformed length */
            if (s->board_len < 0 || s->board_len > 5)
            {
                r->failed = 1;
                return arr;
            }
            for (int j = 0; j < 5; ++j)
                s->board_cards[j] = pe_rd_i32(r);
            /* per-player arrays: bound against MPF_MAX_PLAYERS */
            if (s->stacks_len < 0 || s->stacks_len > MPF_MAX_PLAYERS ||
                s->invested_len < 0 || s->invested_len > MPF_MAX_PLAYERS ||
                s->round_contrib_len < 0 || s->round_contrib_len > MPF_MAX_PLAYERS ||
                s->active_len < 0 || s->active_len > MPF_MAX_PLAYERS ||
                s->acted_len < 0 || s->acted_len > MPF_MAX_PLAYERS)
            {
                r->failed = 1;
                return arr;
            }
            for (int j = 0; j < s->stacks_len; ++j)
                s->stacks[j] = pe_rd_double(r);
            for (int j = 0; j < s->invested_len; ++j)
                s->invested[j] = pe_rd_double(r);
            for (int j = 0; j < s->round_contrib_len; ++j)
                s->round_contrib[j] = pe_rd_double(r);
            for (int j = 0; j < s->active_len; ++j)
                s->active[j] = pe_rd_i32(r);
            for (int j = 0; j < s->acted_len; ++j)
                s->acted[j] = pe_rd_i32(r);
        }
        if (r->p + 8 > r->end)
        {
            r->failed = 1;
            return arr;
        }
        nd->state_key = pe_rd_u64(r->p);
        r->p += 8;
    }
    return arr;
}

mpf_tree_def_t *pe_tree_load(const char *path)
{
    if (!path || !*path)
    {
        errno = EINVAL;
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    unsigned char magic[8];
    uint32_t version = 0;
    if (fread(magic, 1, 8, f) != 8 || fread(&version, 4, 1, f) != 1)
    {
        fclose(f);
        errno = EINVAL;
        return NULL;
    }
    if (memcmp(magic, PE_TREE_MAGIC, 8) != 0 || version != PE_TREE_VERSION)
    {
        fclose(f);
        errno = EINVAL;
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        errno = EIO;
        return NULL;
    }
    long fsize = ftell(f);
    if (fsize < 0)
    {
        fclose(f);
        errno = EIO;
        return NULL;
    }
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc((size_t)fsize);
    if (!buf)
    {
        fclose(f);
        errno = ENOMEM;
        return NULL;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize)
    {
        free(buf);
        fclose(f);
        errno = EIO;
        return NULL;
    }
    fclose(f);

    pe_tree_reader_t r;
    r.p = buf + 12; /* skip magic + version */
    r.end = buf + fsize;
    r.failed = 0;

    mpf_tree_def_t *tree = (mpf_tree_def_t *)calloc(1, sizeof(mpf_tree_def_t));
    if (!tree)
    {
        free(buf);
        errno = ENOMEM;
        return NULL;
    }

    tree->version = pe_rd_i32(&r);
    tree->root_index = pe_rd_i32(&r);
    tree->nodes = pe_read_nodes(&r, &tree->node_count);

    tree->profiles = pe_read_bet_profiles(&r, &tree->profile_count);
    tree->range_profiles = pe_read_range_profiles(&r, &tree->range_profile_count);

    if (tree->node_count > 0 && tree->nodes)
    {
        int ri = (tree->root_index >= 0 && tree->root_index < tree->node_count)
                     ? tree->root_index
                     : 0;
        tree->root_id = tree->nodes[ri].id ? strdup(tree->nodes[ri].id) : NULL;
    }

    if (r.failed)
    {
        mpf_tree_free(tree);
        free(buf);
        errno = EINVAL;
        return NULL;
    }
    free(buf);
    return tree;
}
