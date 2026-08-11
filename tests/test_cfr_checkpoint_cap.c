#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define ASSERT_TRUE(cond, msg)                         \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
        {                                              \
            fprintf(stderr, "Assertion failed: %s\n",  \
                    msg);                              \
            return 1;                                  \
        }                                              \
    } while (0)

static int make_checkpoint_path(char *buffer, size_t len)
{
    if (!buffer || len == 0)
        return -1;
#ifdef _WIN32
    char tmp_path[MAX_PATH];
    DWORD path_len = GetTempPathA((DWORD)sizeof(tmp_path), tmp_path);
    if (path_len == 0 || path_len > sizeof(tmp_path))
        return -1;
    if (GetTempFileNameA(tmp_path, "cfr", 0, buffer) == 0)
        return -1;
    FILE *f = fopen(buffer, "wb");
    if (!f)
        return -1;
    fclose(f);
    return 0;
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    int written = snprintf(buffer, len, "%s/cfr_cap_testXXXXXX", tmpdir);
    if (written < 0 || (size_t)written >= len)
        return -1;
    int fd = mkstemp(buffer);
    if (fd < 0)
        return -1;
    close(fd);
    return 0;
#endif
}

/* Write a checkpoint header that mirrors the on-disk layout of
   cfr_checkpoint_header_t (mirroring side is intentional: the struct is not
   public, so we reproduce it byte-for-byte here). */
static int write_bad_header(const char *path, uint64_t cap, uint32_t version)
{
    unsigned char hdr[8 + 4 + 4 + 8 + 8 + 8];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr + 0, "CFRCHKPT", 8);
    memcpy(hdr + 8, &version, 4);
    memcpy(hdr + 16, &cap, 8);

    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(hdr, sizeof(hdr), 1, f) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

int main(void)
{
    char path[512];
    errno = 0;

    /* cap too large: previously looped forever in next_pow2 (BUG-04). */
    if (make_checkpoint_path(path, sizeof(path)) != 0)
    {
        fprintf(stderr, "Failed to generate checkpoint path: %s\n",
                strerror(errno));
        return 1;
    }
    uint64_t huge_cap = UINT64_MAX;
    if (write_bad_header(path, huge_cap, 2) != 0)
    {
        fprintf(stderr, "Failed to write checkpoint: %s\n",
                strerror(errno));
        return 1;
    }

    cfr_storage_t *storage = cfr_storage_create();
    ASSERT_TRUE(storage != NULL, "storage allocation");

    uint64_t iteration = 0;
    errno = 0;
    ASSERT_TRUE(cfr_storage_load_checkpoint(storage, path, &iteration) == -1,
                "load with overflowing cap must fail");
    ASSERT_TRUE(errno == EINVAL, "overflowing cap fails with EINVAL");
    ASSERT_TRUE(cfr_storage_count_infosets(storage) == 0, "no entries after failed load");
    remove(path);

    /* cap = 0: invalid, must be rejected before any allocation. */
    if (make_checkpoint_path(path, sizeof(path)) != 0)
        return 1;
    if (write_bad_header(path, 0, 2) != 0)
        return 1;
    errno = 0;
    ASSERT_TRUE(cfr_storage_load_checkpoint(storage, path, &iteration) == -1,
                "load with zero cap must fail");
    ASSERT_TRUE(errno == EINVAL, "zero cap fails with EINVAL");
    remove(path);

    /* cap not a power of two: invalid (saves always write a pow2). */
    if (make_checkpoint_path(path, sizeof(path)) != 0)
        return 1;
    if (write_bad_header(path, 1000, 2) != 0)
        return 1;
    errno = 0;
    ASSERT_TRUE(cfr_storage_load_checkpoint(storage, path, &iteration) == -1,
                "load with non-pow2 cap must fail");
    ASSERT_TRUE(errno == EINVAL, "non-pow2 cap fails with EINVAL");
    remove(path);

    cfr_storage_destroy(storage);

    printf("CFR checkpoint cap validation test passed.\n");
    return 0;
}