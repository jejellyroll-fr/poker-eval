/*
 * test_monker_mkr.c - MKR-04: MonkerSolver .mkr entry table
 */

#include <poker_eval/solver/pe_monker.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

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

static int write_archive_payloads(const char *path,
                                  const char *const *names,
                                  const unsigned char *const *payloads,
                                  const size_t *payload_sizes,
                                  size_t count)
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
        if (offset < 0 || (uint64_t)offset > UINT32_MAX ||
            payload_sizes[index] > UINT32_MAX)
            goto done;
        offsets[index] = (uint32_t)offset;
        if (put_u32(file, 0x04034b50u) != 0 || put_u16(file, 20u) != 0 ||
            put_u16(file, 0x0008u) != 0 || put_u16(file, 0u) != 0 ||
            put_u16(file, 0u) != 0 || put_u16(file, 0u) != 0 ||
            put_u32(file, 0u) != 0 || put_u32(file, 0u) != 0 ||
            put_u32(file, 0u) != 0 ||
            put_u16(file, name_length(names[index])) != 0 ||
            put_u16(file, 0u) != 0 || put_name(file, names[index], 0) != 0 ||
            (payload_sizes[index] != 0u &&
             fwrite(payloads[index], 1u, payload_sizes[index], file) !=
                 payload_sizes[index]) ||
            put_u32(file, 0x08074b50u) != 0 || put_u32(file, 0u) != 0 ||
            put_u32(file, (uint32_t)payload_sizes[index]) != 0 ||
            put_u32(file, (uint32_t)payload_sizes[index]) != 0)
            goto done;
    }
    {
        long offset = ftell(file);
        if (offset < 0 || (uint64_t)offset > UINT32_MAX)
            goto done;
        central_offset = (uint32_t)offset;
    }
    for (index = 0u; index < count; ++index) {
        if (put_u32(file, 0x02014b50u) != 0 || put_u16(file, 20u) != 0 ||
            put_u16(file, 20u) != 0 || put_u16(file, 0x0008u) != 0 ||
            put_u16(file, 0u) != 0 || put_u16(file, 0u) != 0 ||
            put_u16(file, 0u) != 0 || put_u32(file, 0u) != 0 ||
            put_u32(file, (uint32_t)payload_sizes[index]) != 0 ||
            put_u32(file, (uint32_t)payload_sizes[index]) != 0 ||
            put_u16(file, name_length(names[index])) != 0 ||
            put_u16(file, 0u) != 0 || put_u16(file, 0u) != 0 ||
            put_u16(file, 0u) != 0 || put_u16(file, 0u) != 0 ||
            put_u32(file, 0u) != 0 || put_u32(file, offsets[index]) != 0 ||
            put_name(file, names[index], 0) != 0)
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

typedef struct
{
    unsigned char data[1024];
    size_t size;
} java_fixture_t;

static void java_byte(java_fixture_t *fixture, unsigned char value)
{
    fixture->data[fixture->size++] = value;
}

static void java_be16(java_fixture_t *fixture, uint16_t value)
{
    java_byte(fixture, (unsigned char)(value >> 8));
    java_byte(fixture, (unsigned char)value);
}

static void java_be32(java_fixture_t *fixture, uint32_t value)
{
    java_byte(fixture, (unsigned char)(value >> 24));
    java_byte(fixture, (unsigned char)(value >> 16));
    java_byte(fixture, (unsigned char)(value >> 8));
    java_byte(fixture, (unsigned char)value);
}

static void java_utf(java_fixture_t *fixture, const char *value)
{
    size_t index;
    java_be16(fixture, (uint16_t)strlen(value));
    for (index = 0u; value[index] != '\0'; ++index)
        java_byte(fixture, (unsigned char)value[index]);
}

static void java_classdesc_header(java_fixture_t *fixture,
                                  const char *name,
                                  unsigned char field_type,
                                  const char *field_name)
{
    unsigned index;
    java_byte(fixture, 0xacu);
    java_byte(fixture, 0xedu);
    java_be16(fixture, 5u);
    java_byte(fixture, 0x73u);
    java_byte(fixture, 0x72u);
    java_utf(fixture, name);
    for (index = 0u; index < 8u; ++index)
        java_byte(fixture, 0u);
    java_byte(fixture, 0x02u);
    java_be16(fixture, field_type == 0u ? 0u : 1u);
    if (field_type != 0u) {
        java_byte(fixture, field_type);
        java_utf(fixture, field_name);
    }
    java_byte(fixture, 0x78u);
    java_byte(fixture, 0x70u);
}

static void java_string(java_fixture_t *fixture, const char *value)
{
    java_byte(fixture, 0xacu);
    java_byte(fixture, 0xedu);
    java_be16(fixture, 5u);
    java_byte(fixture, 0x74u);
    java_utf(fixture, value);
}

static void java_integer(java_fixture_t *fixture, int32_t value)
{
    java_classdesc_header(fixture, "java.lang.Integer", 'I', "value");
    java_be32(fixture, (uint32_t)value);
}

static void java_long(java_fixture_t *fixture, int64_t value)
{
    uint64_t bits = (uint64_t)value;
    unsigned index;
    java_classdesc_header(fixture, "java.lang.Long", 'J', "value");
    for (index = 0u; index < 8u; ++index)
        java_byte(fixture, (unsigned char)(bits >> (56u - 8u * index)));
}

static void java_double(java_fixture_t *fixture, double value)
{
    uint64_t bits;
    unsigned index;
    memcpy(&bits, &value, sizeof(bits));
    java_classdesc_header(fixture, "java.lang.Double", 'D', "value");
    for (index = 0u; index < 8u; ++index)
        java_byte(fixture, (unsigned char)(bits >> (56u - 8u * index)));
}

static void java_short_array(java_fixture_t *fixture,
                             const uint16_t *values, size_t count)
{
    size_t index;
    java_byte(fixture, 0xacu);
    java_byte(fixture, 0xedu);
    java_be16(fixture, 5u);
    java_byte(fixture, 0x75u);
    java_byte(fixture, 0x72u);
    java_utf(fixture, "[S");
    for (index = 0u; index < 8u; ++index)
        java_byte(fixture, 0u);
    java_byte(fixture, 0x02u);
    java_be16(fixture, 0u);
    java_byte(fixture, 0x78u);
    java_byte(fixture, 0x70u);
    java_be32(fixture, (uint32_t)count);
    for (index = 0u; index < count; ++index)
        java_be16(fixture, values[index]);
}

static int deflate_fixture(const java_fixture_t *fixture,
                           unsigned char **out_data, size_t *out_size)
{
    uLongf size = compressBound((uLong)fixture->size);
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    int result;
    if (data == NULL)
        return -1;
    result = compress2(data, &size, fixture->data, (uLong)fixture->size,
                       Z_BEST_SPEED);
    if (result != Z_OK) {
        free(data);
        return -1;
    }
    *out_data = data;
    *out_size = (size_t)size;
    return 0;
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

static void test_java_scalars_and_strategy(void)
{
    static const char *names[] = {
        "game", "iterations", "flopBuckets", "rakepercent",
        "rakecap", "rakeflags", "storedstrategy0", "storedstrategy1"
    };
    const char *path = "/tmp/poker_eval_monker_scalars.mkr";
    java_fixture_t game = {{0}, 0u};
    java_fixture_t iterations = {{0}, 0u};
    java_fixture_t buckets = {{0}, 0u};
    java_fixture_t rakepercent = {{0}, 0u};
    java_fixture_t rakecap = {{0}, 0u};
    java_fixture_t rakeflags = {{0}, 0u};
    java_fixture_t strategy = {{0}, 0u};
    java_fixture_t no_strategy = {{0}, 0u};
    const uint16_t frequencies[] = {100u, 200u, 300u, 400u, 500u, 600u};
    unsigned char *compressed_strategy = NULL;
    unsigned char *compressed_null = NULL;
    size_t compressed_strategy_size = 0u;
    size_t compressed_null_size = 0u;
    const unsigned char *payloads[8];
    size_t payload_sizes[8];
    pe_monker_mkr_t archive;
    pe_monker_mkr_metadata_t metadata;
    pe_monker_mkr_strategy_t decoded;

    java_string(&game, "holdem");
    java_long(&iterations, 1234567890123LL);
    java_integer(&buckets, 3);
    java_double(&rakepercent, 0.05);
    java_double(&rakecap, 1500.0);
    java_integer(&rakeflags, 7);
    java_short_array(&strategy, frequencies,
                     sizeof(frequencies) / sizeof(frequencies[0]));
    java_byte(&no_strategy, 0xacu);
    java_byte(&no_strategy, 0xedu);
    java_be16(&no_strategy, 5u);
    java_byte(&no_strategy, 0x70u);
    CHECK(deflate_fixture(&strategy, &compressed_strategy,
                          &compressed_strategy_size) == 0,
          "strategy compression failed");
    CHECK(deflate_fixture(&no_strategy, &compressed_null,
                          &compressed_null_size) == 0,
          "TC_NULL compression failed");
    payloads[0] = game.data;
    payloads[1] = iterations.data;
    payloads[2] = buckets.data;
    payloads[3] = rakepercent.data;
    payloads[4] = rakecap.data;
    payloads[5] = rakeflags.data;
    payloads[6] = compressed_strategy;
    payloads[7] = compressed_null;
    payload_sizes[0] = game.size;
    payload_sizes[1] = iterations.size;
    payload_sizes[2] = buckets.size;
    payload_sizes[3] = rakepercent.size;
    payload_sizes[4] = rakecap.size;
    payload_sizes[5] = rakeflags.size;
    payload_sizes[6] = compressed_strategy_size;
    payload_sizes[7] = compressed_null_size;
    CHECK(write_archive_payloads(path, names, payloads, payload_sizes, 8u) == 0,
          "could not write Java .mkr fixture");
    if (pe_monker_mkr_read(path, &archive) != PE_MONKER_MKR_OK) {
        CHECK(0, "Java .mkr fixture was rejected");
        free(compressed_strategy);
        free(compressed_null);
        return;
    }
    if (pe_monker_mkr_read_metadata(&archive, &metadata) != PE_MONKER_MKR_OK) {
        CHECK(0, "Java scalar entries were not decoded");
    } else {
        CHECK(metadata.game != NULL && strcmp(metadata.game, "holdem") == 0 &&
                  metadata.iterations == 1234567890123LL &&
                  metadata.flop_buckets == 3u && metadata.rakepercent > 0.0499 &&
                  metadata.rakepercent < 0.0501 &&
                  metadata.rakecap == 1500.0 && metadata.rakeflags == 7,
              "decoded scalar values are wrong");
    }
    pe_monker_mkr_metadata_free(&metadata);
    if (pe_monker_mkr_read_strategy(&archive, "storedstrategy0", 3u,
                                    &decoded) != PE_MONKER_MKR_OK) {
        CHECK(0, "storedstrategy0 was not decoded");
    } else {
        CHECK(decoded.bucket_count == 3u && decoded.class_count == 2u &&
                  decoded.frequencies != NULL && decoded.frequencies[0] == 100u &&
                  decoded.frequencies[5] == 600u,
              "storedstrategy0 dimensions or frequencies are wrong");
    }
    pe_monker_mkr_strategy_free(&decoded);
    CHECK(pe_monker_mkr_read_strategy(&archive, "storedstrategy1", 3u,
                                      &decoded) == PE_MONKER_MKR_OK &&
              decoded.bucket_count == 3u && decoded.class_count == 0u,
          "TC_NULL did not produce an empty strategy");
    pe_monker_mkr_strategy_free(&decoded);
    pe_monker_mkr_free(&archive);
    free(compressed_strategy);
    free(compressed_null);
}

int main(void)
{
    test_entry_listing();
    test_encoding_and_corruption();
    test_java_scalars_and_strategy();
    if (failures != 0)
        return 1;
    puts("test_monker_mkr: UTF-16BE ZIP entry listing passed");
    return 0;
}
