/*
 * test_monker_mkr.c - MKR-04: MonkerSolver .mkr entry table
 */

#include <poker_eval/solver/pe_monker.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            failures++;                                            \
        }                                                          \
    } while (0)

static int put_u16(FILE *file, uint16_t value)
{
    unsigned char bytes[2];
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes) ? 0 : -1;
}

static int put_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4];
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes) ? 0 : -1;
}

static int put_name(FILE *file, const char *name, int utf16le)
{
    size_t index;
    unsigned char bom[2] = {0xfeu, 0xffu};

    if (utf16le) {
        bom[0] = 0xffu;
        bom[1] = 0xfeu;
    }
    if (fwrite(bom, 1u, sizeof(bom), file) != sizeof(bom))
        return -1;
    for (index = 0u; name[index] != '\0'; ++index) {
        unsigned char pair[2];
        if (utf16le) {
            pair[0] = (unsigned char)name[index];
            pair[1] = 0u;
        } else {
            pair[0] = 0u;
            pair[1] = (unsigned char)name[index];
        }
        if (fwrite(pair, 1u, sizeof(pair), file) != sizeof(pair))
            return -1;
    }
    return 0;
}

static uint16_t name_length(const char *name)
{
    return (uint16_t)(2u + 2u * strlen(name));
}

static int write_archive(const char *path,
                         const char *const *names,
                         size_t count,
                         int utf16le)
{
    FILE *file = fopen(path, "wb");
    uint32_t offsets[8];
    uint32_t central_offset;
    uint32_t central_size;
    size_t index;
    int ok = 0;

    if (file == NULL || count > 8u)
        goto done;
    for (index = 0u; index < count; ++index) {
        long offset = ftell(file);
        if (offset < 0 || (uint64_t)offset > UINT32_MAX)
            goto done;
        offsets[index] = (uint32_t)offset;
        if (put_u32(file, 0x04034b50u) != 0 ||
            put_u16(file, 20u) != 0 ||
            put_u16(file, 0x0008u) != 0 ||
            put_u16(file, 0u) != 0 || put_u16(file, 0u) != 0 ||
            put_u16(file, 0u) != 0 || put_u32(file, 0u) != 0 ||
            put_u32(file, 0u) != 0 || put_u32(file, 0u) != 0 ||
            put_u16(file, name_length(names[index])) != 0 ||
            put_u16(file, 0u) != 0 || put_name(file, names[index], utf16le) != 0 ||
            fputc('x', file) == EOF || put_u32(file, 0x08074b50u) != 0 ||
            put_u32(file, 0u) != 0 || put_u32(file, 1u) != 0 ||
            put_u32(file, 1u) != 0)
            goto done;
    }

    {
        long offset = ftell(file);
        if (offset < 0 || (uint64_t)offset > UINT32_MAX)
            goto done;
        central_offset = (uint32_t)offset;
    }
    for (index = 0u; index < count; ++index) {
        if (put_u32(file, 0x02014b50u) != 0 ||
            put_u16(file, 20u) != 0 || put_u16(file, 20u) != 0 ||
            put_u16(file, 0x0008u) != 0 || put_u16(file, 0u) != 0 ||
            put_u16(file, 0u) != 0 || put_u16(file, 0u) != 0 ||
            put_u32(file, 0u) != 0 || put_u32(file, 1u) != 0 ||
            put_u32(file, 1u) != 0 ||
            put_u16(file, name_length(names[index])) != 0 ||
            put_u16(file, 0u) != 0 || put_u16(file, 0u) != 0 ||
            put_u16(file, 0u) != 0 || put_u16(file, 0u) != 0 ||
            put_u32(file, 0u) != 0 || put_u32(file, offsets[index]) != 0 ||
            put_name(file, names[index], utf16le) != 0)
            goto done;
    }
    {
        long end = ftell(file);
        if (end < 0 || (uint64_t)end < central_offset ||
            (uint64_t)end - central_offset > UINT32_MAX)
            goto done;
        central_size = (uint32_t)((uint64_t)end - central_offset);
    }
    if (put_u32(file, 0x06054b50u) != 0 || put_u16(file, 0u) != 0 ||
        put_u16(file, 0u) != 0 || put_u16(file, (uint16_t)count) != 0 ||
        put_u16(file, (uint16_t)count) != 0 || put_u32(file, central_size) != 0 ||
        put_u32(file, central_offset) != 0 || put_u16(file, 0u) != 0)
        goto done;
    ok = 1;

done:
    if (file != NULL && fclose(file) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

static int write_bytes(const char *path, const unsigned char *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (file == NULL)
        return -1;
    ok = fwrite(bytes, 1u, length, file) == length && fclose(file) == 0;
    return ok ? 0 : -1;
}

static void test_entry_listing(void)
{
    static const char *names[] = {
        "tree", "storedstrategy0", "storedstrategy1", "storedstrategy2",
        "storedstrategy3", "iterations", "flopBuckets"
    };
    const char *path = "/tmp/poker_eval_monker_fixture.mkr";
    pe_monker_mkr_t archive;
    size_t index;

    CHECK(write_archive(path, names, sizeof(names) / sizeof(names[0]), 0) == 0,
          "could not write .mkr fixture");
    CHECK(pe_monker_mkr_read(path, &archive) == PE_MONKER_MKR_OK,
          "valid .mkr fixture was rejected");
    if (failures != 0 && archive.entries == NULL)
        return;
    CHECK(archive.count == sizeof(names) / sizeof(names[0]),
          "wrong .mkr entry count: %zu", archive.count);
    for (index = 0u; index < archive.count && index < sizeof(names) / sizeof(names[0]);
         ++index) {
        CHECK(strcmp(archive.entries[index].name, names[index]) == 0,
              "entry %zu name mismatch: %s", index, archive.entries[index].name);
        CHECK((archive.entries[index].flags & 0x0008u) != 0u &&
                  archive.entries[index].method == 0u &&
                  archive.entries[index].compressed_size == 1u &&
                  archive.entries[index].uncompressed_size == 1u,
              "entry %zu metadata mismatch", index);
    }
    pe_monker_mkr_free(&archive);
}

static void test_encoding_and_corruption(void)
{
    static const char *names[] = {"tree"};
    const char *path = "/tmp/poker_eval_monker_bad_name.mkr";
    pe_monker_mkr_t archive;
    const unsigned char truncated[] = {0x50u, 0x4bu, 0x05u, 0x06u};

    CHECK(write_archive(path, names, 1u, 1) == 0,
          "could not write UTF-16LE fixture");
    CHECK(pe_monker_mkr_read(path, &archive) == PE_MONKER_MKR_ERR_UTF16LE_BOM,
          "UTF-16LE BOM did not receive its dedicated error");
    CHECK(strstr(pe_monker_mkr_status_string(PE_MONKER_MKR_ERR_UTF16LE_BOM),
                 "UTF-16LE") != NULL,
          "UTF-16LE error message is not distinct");
    pe_monker_mkr_free(&archive);

    CHECK(write_bytes(path, truncated, sizeof(truncated)) == 0,
          "could not write truncated fixture");
    CHECK(pe_monker_mkr_read(path, &archive) == PE_MONKER_MKR_ERR_TRUNCATED,
          "truncated archive was not rejected explicitly");
    CHECK(strcmp(pe_monker_mkr_status_string(PE_MONKER_MKR_ERR_TRUNCATED),
                 pe_monker_mkr_status_string(PE_MONKER_MKR_ERR_UTF16LE_BOM)) != 0,
          "truncated and UTF-16LE errors share a message");
    pe_monker_mkr_free(&archive);

    CHECK(pe_monker_mkr_read(NULL, &archive) == PE_MONKER_MKR_ERR_NULL_ARGUMENT,
          "NULL path was not rejected");
}

int main(void)
{
    test_entry_listing();
    test_encoding_and_corruption();
    if (failures != 0)
        return 1;
    puts("test_monker_mkr: UTF-16BE ZIP entry listing passed");
    return 0;
}
