#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/mpf_compact_storage.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

/* Header shared with the desktop trainer: the GUI parser must agree with
 * mpf_compact_storage.c on magic/version/flags, otherwise compressed or
 * future solutions get misparsed as raw quantized records. */
#include "../tools/pe_sol_format.h"

#define ASSERT_TRUE(cond, msg)                         \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
        {                                              \
            fprintf(stderr, "Assertion failed: %s\n", \
                    msg);                              \
            return 1;                                  \
        }                                              \
    } while (0)

#define ASSERT_NEAR(val, expected, tol, msg)                              \
    do                                                                    \
    {                                                                     \
        double _v = (val), _e = (expected);                               \
        if (fabs(_v - _e) > (tol))                                        \
        {                                                                 \
            fprintf(stderr, "Assertion failed: %s (%.10f != %.10f)\n",   \
                    msg, _v, _e);                                         \
            return 1;                                                     \
        }                                                                 \
    } while (0)

/* ---- Pure format tests (no library involved) ---- */

static void put_u32(unsigned char *buffer, size_t offset, uint32_t value)
{
    for (size_t i = 0; i < 4u; ++i) buffer[offset + i] = (unsigned char)(value >> (i * 8u));
}

static void put_u64(unsigned char *buffer, size_t offset, uint64_t value)
{
    for (size_t i = 0; i < 8u; ++i) buffer[offset + i] = (unsigned char)(value >> (i * 8u));
}

static void make_header(unsigned char *buffer, uint32_t version, uint32_t flags,
                        uint64_t count)
{
    memcpy(buffer, PE_SOL_FMT_MAGIC, 8);
    put_u32(buffer, 8, version);
    put_u32(buffer, 12, flags);
    put_u64(buffer, 16, count);
    put_u64(buffer, 24, 0);
}

static int test_header_rejections(void)
{
    unsigned char buffer[64];
    uint64_t count = 0;

    memset(buffer, 0, sizeof(buffer));
    ASSERT_TRUE(pe_sol_fmt_parse_header(buffer, 10, &count) == PE_SOL_FMT_SHORT_HEADER,
                "short header rejected");

    /* Bad magic. */
    make_header(buffer, PE_SOL_FMT_VERSION, PE_SOL_FMT_FLAG_RAW, 0);
    buffer[0] = 'X';
    ASSERT_TRUE(pe_sol_fmt_parse_header(buffer, sizeof(buffer), &count) == PE_SOL_FMT_BAD_MAGIC,
                "bad magic rejected");

    /* Bad version. */
    make_header(buffer, 99u, PE_SOL_FMT_FLAG_RAW, 0);
    ASSERT_TRUE(pe_sol_fmt_parse_header(buffer, sizeof(buffer), &count) == PE_SOL_FMT_BAD_MAGIC,
                "bad version rejected");

    /* flags == 1 must be reported as compressed, never parsed as raw. */
    make_header(buffer, PE_SOL_FMT_VERSION, PE_SOL_FMT_FLAG_ZSTD, 3);
    ASSERT_TRUE(pe_sol_fmt_parse_header(buffer, sizeof(buffer), &count) == PE_SOL_FMT_COMPRESSED,
                "zstd flag detected");

    /* Unknown flags are rejected too. */
    make_header(buffer, PE_SOL_FMT_VERSION, 7u, 0);
    ASSERT_TRUE(pe_sol_fmt_parse_header(buffer, sizeof(buffer), &count) == PE_SOL_FMT_BAD_FLAGS,
                "unknown flags rejected");

    /* count cannot exceed what fits in the record stream. */
    make_header(buffer, PE_SOL_FMT_VERSION, PE_SOL_FMT_FLAG_RAW, 1000000ull);
    ASSERT_TRUE(pe_sol_fmt_parse_header(buffer, sizeof(buffer), &count) == PE_SOL_FMT_OVERFLOW,
                "implausible count rejected");

    /* Valid header. */
    make_header(buffer, PE_SOL_FMT_VERSION, PE_SOL_FMT_FLAG_RAW, 2);
    ASSERT_TRUE(pe_sol_fmt_parse_header(buffer, sizeof(buffer), &count) == PE_SOL_FMT_OK,
                "valid header accepted");
    ASSERT_TRUE(count == 2, "count round-trips");
    return 0;
}

static int test_dequantize_row(void)
{
    double probs[4];
    /* Quantization drift: 40000 + 20000 = 60000 != 65535, so plain
     * q/65535 would give 0.610/0.305; renormalization must give 2/3, 1/3. */
    const uint16_t drifted[2] = {40000u, 20000u};
    pe_sol_fmt_dequantize_row(drifted, 2, probs);
    ASSERT_NEAR(probs[0], 2.0 / 3.0, 1e-12, "drifted row renormalized [0]");
    ASSERT_NEAR(probs[1], 1.0 / 3.0, 1e-12, "drifted row renormalized [1]");

    /* All-zero row falls back to uniform. */
    const uint16_t zeros[3] = {0u, 0u, 0u};
    pe_sol_fmt_dequantize_row(zeros, 3, probs);
    for (int i = 0; i < 3; ++i)
        ASSERT_NEAR(probs[i], 1.0 / 3.0, 1e-12, "zero row uniform");

    /* Exact row keeps summing to 1. */
    const uint16_t exact[2] = {65535u, 0u};
    pe_sol_fmt_dequantize_row(exact, 2, probs);
    ASSERT_NEAR(probs[0], 1.0, 1e-12, "exact row [0]");
    ASSERT_NEAR(probs[1], 0.0, 1e-12, "exact row [1]");
    return 0;
}

/* ---- Cross-check against the library writer and mmap reader ---- */

#if !defined(_WIN32)
static int make_tmp_path(char *buffer, size_t len, const char *suffix)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
    int written = snprintf(buffer, len, "%s/pe_sol_fmt_XXXXXX", tmpdir);
    if (written < 0 || (size_t)written >= len) return -1;
    int fd = mkstemp(buffer);
    if (fd < 0) return -1;
    close(fd);
    size_t cur = strlen(buffer);
    if (cur + strlen(suffix) >= len) return -1;
    strcat(buffer, suffix);
    return 0;
}
#else
static int make_tmp_path(char *buffer, size_t len, const char *suffix)
{
    char temp[MAX_PATH];
    char file[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, temp)) return -1;
    if (!GetTempFileNameA(temp, "pes", 0, file)) return -1;
    int written = snprintf(buffer, len, "%s%s", file, suffix);
    return (written < 0 || (size_t)written >= len) ? -1 : 0;
}
#endif

static unsigned char *read_whole_file(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    long size;
    unsigned char *data;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    if (size < 0) { fclose(file); return NULL; }
    data = (unsigned char *)malloc((size_t)size);
    if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size)
    {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)size;
    return data;
}

static int test_library_cross_check(void)
{
    char sol_path[512];
    ASSERT_TRUE(make_tmp_path(sol_path, sizeof(sol_path), ".pe_sol") == 0, "tmp path");

    cfr_storage_t *storage = cfr_storage_create();
    ASSERT_TRUE(storage != NULL, "storage allocation");
    const uint64_t KEY_A = 0x1234ull;
    const uint64_t KEY_B = 0x4567ull;
    double avg_a[2] = {1.0, 3.0};      /* 0.25 / 0.75 */
    double avg_b[3] = {0.1, 0.2, 0.7}; /* 0.1 / 0.2 / 0.7 */
    cfr_storage_update_avg(storage, KEY_A, 2, avg_a, 1.0);
    cfr_storage_update_avg(storage, KEY_B, 3, avg_b, 1.0);
    ASSERT_TRUE(pe_cfr_save_storage(storage, sol_path) == 0, "library save");

    /* The shared GUI parser must accept what the library wrote. */
    size_t size = 0;
    unsigned char *data = read_whole_file(sol_path, &size);
    ASSERT_TRUE(data != NULL, "read saved file");
    uint64_t count = 0;
    ASSERT_TRUE(pe_sol_fmt_parse_header(data, size, &count) == PE_SOL_FMT_OK,
                "library file accepted by shared parser");
    ASSERT_TRUE(count == 2, "library infoset count");

    /* Walk the record stream with the shared helpers and compare with the
     * library mmap reader, which is the reference dequantizer. */
    pe_sol_mmap_t *view = NULL;
    ASSERT_TRUE(pe_sol_open_mmap(sol_path, &view) == 0, "mmap open");
    ASSERT_TRUE(pe_sol_mmap_infoset_count(view) == (size_t)count, "mmap count matches");

    size_t offset = PE_SOL_FMT_HEADER_SIZE;
    for (uint64_t i = 0; i < count; ++i)
    {
        uint64_t key = pe_sol_fmt_u64(data, offset);
        uint32_t actions = pe_sol_fmt_u32(data, offset + 8u);
        uint16_t quantized[8];
        double gui_probs[8];
        double lib_probs[8];
        uint64_t lib_key = 0;
        int lib_n = 0;
        offset += 12u;
        ASSERT_TRUE(actions <= 8u, "action count in range");
        for (uint32_t a = 0; a < actions; ++a)
            quantized[a] = pe_sol_fmt_u16(data, offset + a * 2u);
        offset += (size_t)actions * 2u;
        pe_sol_fmt_dequantize_row(quantized, (int)actions, gui_probs);
        ASSERT_TRUE(pe_sol_mmap_get_strategy(view, (size_t)i, &lib_key, 8,
                                             lib_probs, &lib_n) == 0,
                    "mmap strategy read");
        ASSERT_TRUE(lib_key == key, "keys agree");
        ASSERT_TRUE(lib_n == (int)actions, "action counts agree");
        for (uint32_t a = 0; a < actions; ++a)
            ASSERT_NEAR(gui_probs[a], lib_probs[a], 1e-12,
                        "GUI parser matches mmap reader");
    }
    pe_sol_close_mmap(view);
    free(data);

    /* A zstd save must be reported COMPRESSED by the shared parser. */
    {
        char zstd_path[512];
        ASSERT_TRUE(make_tmp_path(zstd_path, sizeof(zstd_path), ".pe_sol") == 0,
                    "zstd tmp path");
        int zrc = pe_cfr_save_storage_zstd(storage, zstd_path, 3);
        if (zrc == 0)
        {
            size_t zsize = 0;
            unsigned char *zdata = read_whole_file(zstd_path, &zsize);
            uint64_t zcount = 0;
            ASSERT_TRUE(zdata != NULL, "read zstd file");
            ASSERT_TRUE(pe_sol_fmt_parse_header(zdata, zsize, &zcount) == PE_SOL_FMT_COMPRESSED,
                        "zstd file detected as compressed");
            free(zdata);
        }
        else
        {
            ASSERT_TRUE(errno == ENOTSUP, "zstd unavailable explains failure");
            printf("  zstd not available; compressed-detection check skipped\n");
        }
        remove(zstd_path);
    }

    remove(sol_path);
    cfr_storage_destroy(storage);
    return 0;
}

int main(void)
{
    if (test_header_rejections() != 0) return 1;
    if (test_dequantize_row() != 0) return 1;
    if (test_library_cross_check() != 0) return 1;
    puts("pe_sol format tests passed");
    return 0;
}
