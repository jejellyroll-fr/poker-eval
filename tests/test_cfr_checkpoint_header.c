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
    int written = snprintf(buffer, len, "%s/cfr_hdr_testXXXXXX", tmpdir);
    if (written < 0 || (size_t)written >= len)
        return -1;
    int fd = mkstemp(buffer);
    if (fd < 0)
        return -1;
    close(fd);
    return 0;
#endif
}

/* Mirrors the on-disk layout of cfr_checkpoint_header_t (struct not
   public). magic[8] + version u32 + reserved u32 + cap u64 +
   entry_count u64 + iteration u64. */
static int write_header(const char *path,
                        uint64_t cap,
                        uint64_t entry_count,
                        uint32_t version)
{
    unsigned char hdr[8 + 4 + 4 + 8 + 8 + 8];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr + 0, "CFRCHKPT", 8);
    memcpy(hdr + 8, &version, 4);
    memcpy(hdr + 16, &cap, 8);
    memcpy(hdr + 24, &entry_count, 8);

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

    /* entry_count > cap: table would fill up and get_entry would probe
       forever. Must be rejected up front. */
    if (make_checkpoint_path(path, sizeof(path)) != 0)
        return 1;
    if (write_header(path, 1024, 2048, 2) != 0)
        return 1;
    cfr_storage_t *s1 = cfr_storage_create();
    ASSERT_TRUE(s1 != NULL, "storage allocation");
    uint64_t iteration = 0;
    errno = 0;
    ASSERT_TRUE(cfr_storage_load_checkpoint(s1, path, &iteration) == -1,
                "entry_count > cap must fail");
    ASSERT_TRUE(errno == EINVAL, "entry_count > cap fails with EINVAL");
    ASSERT_TRUE(cfr_storage_count_infosets(s1) == 0,
                "no entries after failed load");
    cfr_storage_destroy(s1);
    remove(path);

    /* cap above the compiled-in bound: rejected before allocation. */
    if (make_checkpoint_path(path, sizeof(path)) != 0)
        return 1;
    if (write_header(path, (uint64_t)1u << 27, 1, 2) != 0)
        return 1;
    cfr_storage_t *s2 = cfr_storage_create();
    ASSERT_TRUE(s2 != NULL, "storage allocation");
    errno = 0;
    ASSERT_TRUE(cfr_storage_load_checkpoint(s2, path, &iteration) == -1,
                "oversized cap must fail");
    ASSERT_TRUE(errno == EINVAL, "oversized cap fails with EINVAL");
    cfr_storage_destroy(s2);
    remove(path);

    /* cap not a power of two: rejected. */
    if (make_checkpoint_path(path, sizeof(path)) != 0)
        return 1;
    if (write_header(path, 1000, 1, 2) != 0)
        return 1;
    cfr_storage_t *s3 = cfr_storage_create();
    ASSERT_TRUE(s3 != NULL, "storage allocation");
    errno = 0;
    ASSERT_TRUE(cfr_storage_load_checkpoint(s3, path, &iteration) == -1,
                "non-pow2 cap must fail");
    ASSERT_TRUE(errno == EINVAL, "non-pow2 cap fails with EINVAL");
    cfr_storage_destroy(s3);
    remove(path);

    /* Valid header but truncated body: must fail, not crash.  We write a
       valid header (cap 1024, one entry promised) and no entry payload. */
    if (make_checkpoint_path(path, sizeof(path)) != 0)
        return 1;
    if (write_header(path, 1024, 1, 2) != 0)
        return 1;
    cfr_storage_t *s4 = cfr_storage_create();
    ASSERT_TRUE(s4 != NULL, "storage allocation");
    errno = 0;
    ASSERT_TRUE(cfr_storage_load_checkpoint(s4, path, &iteration) == -1,
                "truncated body must fail");
    cfr_storage_destroy(s4);
    remove(path);

    printf("CFR checkpoint header validation test passed.\n");
    return 0;
}