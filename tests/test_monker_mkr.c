/*
 * test_monker_mkr.c - MKR-04: .mkr entry table
 */

#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

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
    uint32_t offsets[16];
    uint32_t central_offset;
    uint32_t central_size;
    size_t index;
    int ok = 0;

    if (file == NULL || count > 16u)
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
    uint32_t offsets[16];
    uint32_t central_offset;
    uint32_t central_size;
    size_t index;
    int ok = 0;

    if (file == NULL || count > 16u)
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

/*
 * Handle numbering.
 *
 * Java writes a class descriptor once and refers back to it; the reference is
 * a number, and the numbering counts *every* class descriptor, string, object
 * and array in stream order. A reader that forgets to count one does not fail
 * — it resolves the reference to a neighbouring entry and decodes an array as
 * the wrong type. So this fixture puts an array between a descriptor and the
 * reference to it, which is only decodable if the array was counted.
 */
static void java_stream_begin(java_fixture_t *fixture)
{
    java_byte(fixture, 0xacu);
    java_byte(fixture, 0xedu);
    java_be16(fixture, 5u);
}

static void java_block_int(java_fixture_t *fixture, int32_t value)
{
    java_byte(fixture, 0x77u);
    java_byte(fixture, 4u);
    java_be32(fixture, (uint32_t)value);
}

/* TC_ARRAY + a fresh TC_CLASSDESC. Consumes two handles: descriptor, array. */
static void java_array_new(java_fixture_t *fixture, const char *type)
{
    unsigned index;
    java_byte(fixture, 0x75u);
    java_byte(fixture, 0x72u);
    java_utf(fixture, type);
    for (index = 0u; index < 8u; ++index)
        java_byte(fixture, 0u);
    java_byte(fixture, 0x02u);
    java_be16(fixture, 0u);
    java_byte(fixture, 0x78u);
    java_byte(fixture, 0x70u);
}

/* TC_ARRAY + TC_REFERENCE to a descriptor already in the stream. */
static void java_array_ref(java_fixture_t *fixture, uint32_t handle)
{
    java_byte(fixture, 0x75u);
    java_byte(fixture, 0x71u);
    java_be32(fixture, handle);
}

static void test_java_scalars_and_strategy(void)
{
    static const char *const names[] = {
        "game", "iterations", "flopBuckets", "rakepercent", "rakecap",
        "rakeflags", "version", "iscount", "isoLevel",
        "storedstrategy0", "storedstrategy1"
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
    java_fixture_t version = {{0}, 0u};
    java_fixture_t iscount = {{0}, 0u};
    java_fixture_t iso_level = {{0}, 0u};
    unsigned char *compressed_strategy = NULL;
    unsigned char *compressed_null = NULL;
    size_t compressed_strategy_size = 0u;
    size_t compressed_null_size = 0u;
    const unsigned char *payloads[11];
    size_t payload_sizes[11];
    pe_monker_mkr_t archive;
    pe_monker_mkr_metadata_t metadata;
    pe_monker_mkr_strategy_t decoded;
    unsigned index;

    /* "game" is an Integer in every real saved run, not a name. */
    java_integer(&game, 1);
    java_long(&iterations, 1234567890123LL);
    java_integer(&buckets, 3);
    java_double(&rakepercent, 0.05);
    java_double(&rakecap, 1500.0);
    java_integer(&rakeflags, 7);
    java_long(&version, 20109);
    java_long(&iscount, 230048);
    java_integer(&iso_level, 0);

    /*
     * The shape a real storedstrategy0 has: a leading block-data integer, then
     * one slot per node — an array where the node decides, nothing where it
     * does not — with byte arrays and int arrays both present.
     *
     *   handle 0x7E0000  descriptor "[B"
     *   handle 0x7E0001  first byte array
     *   handle 0x7E0002  second byte array   (descriptor by reference)
     *   handle 0x7E0003  descriptor "[I"
     *   handle 0x7E0004  int array
     *   handle 0x7E0005  int array, descriptor by reference to 0x7E0003
     *
     * The last slot is the one that pins the numbering. Reaching 0x7E0003 for
     * the "[I" descriptor is only correct if the three arrays before it each
     * took a handle of their own; a reader that counts descriptors alone puts
     * "[I" at 0x7E0001 and resolves this reference to the byte array's.
     */
    java_stream_begin(&strategy);
    java_block_int(&strategy, 30);
    java_array_new(&strategy, "[B");
    java_be32(&strategy, 4u);
    for (index = 0u; index < 4u; ++index)
        java_byte(&strategy, (unsigned char)(0x10u + index));
    java_byte(&strategy, 0x70u);                 /* absent slot */
    java_array_ref(&strategy, 0x7E0000u);        /* "[B" again */
    java_be32(&strategy, 4u);
    for (index = 0u; index < 4u; ++index)
        java_byte(&strategy, (unsigned char)(0x20u + index));
    java_array_new(&strategy, "[I");
    java_be32(&strategy, 2u);
    java_be32(&strategy, 0xFFFFFFFFu);           /* -1 */
    java_be32(&strategy, 7u);
    java_array_ref(&strategy, 0x7E0003u);        /* "[I" again */
    java_be32(&strategy, 1u);
    java_be32(&strategy, 99u);

    java_stream_begin(&no_strategy);
    java_block_int(&no_strategy, 30);
    java_byte(&no_strategy, 0x70u);
    java_byte(&no_strategy, 0x70u);

    CHECK(deflate_fixture(&strategy, &compressed_strategy,
                          &compressed_strategy_size) == 0,
          "strategy compression failed");
    CHECK(deflate_fixture(&no_strategy, &compressed_null,
                          &compressed_null_size) == 0,
          "empty-strategy compression failed");
    payloads[0] = game.data;
    payloads[1] = iterations.data;
    payloads[2] = buckets.data;
    payloads[3] = rakepercent.data;
    payloads[4] = rakecap.data;
    payloads[5] = rakeflags.data;
    payloads[6] = version.data;
    payloads[7] = iscount.data;
    payloads[8] = iso_level.data;
    payloads[9] = compressed_strategy;
    payloads[10] = compressed_null;
    payload_sizes[0] = game.size;
    payload_sizes[1] = iterations.size;
    payload_sizes[2] = buckets.size;
    payload_sizes[3] = rakepercent.size;
    payload_sizes[4] = rakecap.size;
    payload_sizes[5] = rakeflags.size;
    payload_sizes[6] = version.size;
    payload_sizes[7] = iscount.size;
    payload_sizes[8] = iso_level.size;
    payload_sizes[9] = compressed_strategy_size;
    payload_sizes[10] = compressed_null_size;
    CHECK(write_archive_payloads(path, names, payloads, payload_sizes, 11u) == 0,
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
        CHECK(metadata.game == 1 &&
                  metadata.iterations == 1234567890123LL &&
                  metadata.flop_buckets == 3u && metadata.rakepercent > 0.0499 &&
                  metadata.rakepercent < 0.0501 &&
                  metadata.rakecap == 1500.0 && metadata.rakeflags == 7 &&
                  metadata.version == 20109 && metadata.iscount == 230048 &&
                  metadata.iso_level == 0,
              "decoded scalar values are wrong");
    }
    pe_monker_mkr_metadata_free(&metadata);

    if (pe_monker_mkr_read_strategy(&archive, "storedstrategy0",
                                    &decoded) != PE_MONKER_MKR_OK) {
        CHECK(0, "storedstrategy0 was not decoded");
    } else {
        CHECK(decoded.bucket_count == 30,
              "the leading block-data integer was not read as the bucket count");
        CHECK(decoded.slot_count == 5u, "expected 5 slots, got %u",
              decoded.slot_count);
        if (decoded.slot_count == 5u) {
            CHECK(decoded.slots[0].kind == PE_MONKER_SLOT_BYTES &&
                      decoded.slots[0].count == 4u &&
                      decoded.slots[0].bytes[0] == 0x10u,
                  "the first byte slot is wrong");
            CHECK(decoded.slots[1].kind == PE_MONKER_SLOT_ABSENT,
                  "an absent slot did not come back absent");
            /* This is the one that needs the handle table: its descriptor is a
               reference across an array that also took a handle. */
            CHECK(decoded.slots[2].kind == PE_MONKER_SLOT_BYTES &&
                      decoded.slots[2].count == 4u &&
                      decoded.slots[2].bytes[3] == 0x23u,
                  "a referenced class descriptor did not resolve to [B");
            CHECK(decoded.slots[3].kind == PE_MONKER_SLOT_INTS &&
                      decoded.slots[3].count == 2u &&
                      decoded.slots[3].ints[0] == -1 &&
                      decoded.slots[3].ints[1] == 7,
                  "the int slot is wrong");
            /* Only decodable if arrays are counted as handles too. */
            CHECK(decoded.slots[4].kind == PE_MONKER_SLOT_INTS &&
                      decoded.slots[4].count == 1u &&
                      decoded.slots[4].ints[0] == 99,
                  "handles were numbered without counting the arrays");
        }
    }
    pe_monker_mkr_strategy_free(&decoded);

    CHECK(pe_monker_mkr_read_strategy(&archive, "storedstrategy1",
                                      &decoded) == PE_MONKER_MKR_OK &&
              decoded.bucket_count == 30 && decoded.slot_count == 2u &&
              decoded.slots[0].kind == PE_MONKER_SLOT_ABSENT &&
              decoded.slots[1].kind == PE_MONKER_SLOT_ABSENT,
          "an unsolved street did not come back as absent slots");
    pe_monker_mkr_strategy_free(&decoded);
    pe_monker_mkr_free(&archive);
    free(compressed_strategy);
    free(compressed_null);
}

/*
 * Which node a slot belongs to.
 *
 * The order is preorder with children visited last to first, so this fixture
 * is built to tell that apart from plain preorder rather than merely be
 * consistent with it. The tree is
 *
 *      0 ---- call -----> 1 (leaf)
 *        \-- 50% pot --> 2 ---- fold --> 3 (leaf)
 *                          \-- call --> 4 (leaf)
 *
 * Plain preorder visits 0,1,2,3,4 and gives the presence pattern "A.A..".
 * Last-to-first visits 0,2,4,3,1 and gives "AA...". The two disagree at every
 * slot but the first, so a fixture matching one cannot match the other.
 */
static void tree_be16(unsigned char *buffer, size_t *at, unsigned value)
{
    buffer[(*at)++] = (unsigned char)(value >> 8u);
    buffer[(*at)++] = (unsigned char)(value & 0xFFu);
}

static void tree_be32(unsigned char *buffer, size_t *at, uint32_t value)
{
    unsigned index;
    for (index = 0u; index < 4u; ++index)
        buffer[(*at)++] = (unsigned char)(value >> (8u * (3u - index)));
}

static size_t write_bind_tree(const char *path)
{
    unsigned char bytes[128];
    size_t at = 0u;
    FILE *file;
    unsigned index;

    for (index = 0u; index < 4u; ++index)
        bytes[at++] = 0u;
    tree_be32(bytes, &at, 33487u);       /* signature (low half of the i64) */
    tree_be32(bytes, &at, 1u);           /* internal format */
    tree_be32(bytes, &at, 2u);           /* players */
    tree_be32(bytes, &at, 0u);           /* first to act */
    tree_be32(bytes, &at, 1u);           /* street 1: no committed block */
    tree_be32(bytes, &at, 0u);           /* dead money */
    tree_be32(bytes, &at, 10000u);
    tree_be32(bytes, &at, 10000u);
    tree_be16(bytes, &at, 2u);           /* root: 2 children */
    tree_be16(bytes, &at, 1u);           /*   call */
    tree_be16(bytes, &at, 0u);           /*   leaf */
    tree_be16(bytes, &at, 40050u);       /*   half pot */
    tree_be16(bytes, &at, 2u);           /*   node with 2 children */
    tree_be16(bytes, &at, 0u);           /*     fold */
    tree_be16(bytes, &at, 0u);           /*     leaf */
    tree_be16(bytes, &at, 1u);           /*     call */
    tree_be16(bytes, &at, 0u);           /*     leaf */
    bytes[at++] = 0u;                    /* no ranges */

    file = fopen(path, "wb");
    if (file == NULL)
        return 0u;
    if (fwrite(bytes, 1u, at, file) != at || fclose(file) != 0)
        return 0u;
    return at;
}

/*
 * `pattern` is one character per slot: 'A' an array, '.' an absent slot. Each
 * array holds two values, except that the array at `odd_slot` holds four —
 * which is how a strategy whose slots disagree about the class count is made.
 */
static void build_bind_strategy_sized(java_fixture_t *fixture,
                                      const char *pattern, int odd_slot)
{
    size_t index;
    int first = 1;
    java_stream_begin(fixture);
    java_block_int(fixture, 30);
    for (index = 0u; pattern[index] != '\0'; ++index) {
        uint32_t count = ((int)index == odd_slot) ? 4u : 2u;
        uint32_t value;
        if (pattern[index] == '.') {
            java_byte(fixture, 0x70u);
            continue;
        }
        if (first) {
            java_array_new(fixture, "[B");
            first = 0;
        } else {
            java_array_ref(fixture, 0x7E0000u);
        }
        java_be32(fixture, count);
        for (value = 0u; value < count; value += 2u) {
            java_byte(fixture, 200u);
            java_byte(fixture, 56u);
        }
    }
}

static void build_bind_strategy(java_fixture_t *fixture, const char *pattern)
{
    build_bind_strategy_sized(fixture, pattern, -1);
}

static void test_slot_to_node_binding(void)
{
    const char *tree_path = "/tmp/poker_eval_monker_bind.tree";
    const char *archive_path = "/tmp/poker_eval_monker_bind.mkr";
    static const char *const names[] = {
        "storedstrategy0", "storedstrategy1", "storedstrategy2",
        "storedstrategy3"
    };
    java_fixture_t good = {{0}, 0u};
    java_fixture_t wrong = {{0}, 0u};
    java_fixture_t short_run = {{0}, 0u};
    java_fixture_t ragged = {{0}, 0u};
    unsigned char *good_z = NULL;
    unsigned char *wrong_z = NULL;
    unsigned char *short_z = NULL;
    unsigned char *ragged_z = NULL;
    size_t good_size = 0u, wrong_size = 0u, short_size = 0u, ragged_size = 0u;
    const unsigned char *payloads[4];
    size_t payload_sizes[4];
    mpf_tree_def_t *tree = NULL;
    pe_monker_mkr_t archive;
    pe_monker_mkr_strategy_t strategy;
    int32_t map[8];

    CHECK(write_bind_tree(tree_path) > 0u, "bind tree fixture write failed");
    if (pe_monker_tree_load(tree_path, &tree) != PE_MONKER_OK || tree == NULL) {
        CHECK(0, "bind tree fixture did not load");
        return;
    }
    CHECK(tree->node_count == 5, "bind tree has %d nodes, expected 5",
          tree->node_count);

    build_bind_strategy(&good, "AA...");    /* children last to first */
    build_bind_strategy(&wrong, "A.A..");   /* plain preorder */
    /* Three slots for a five-node tree. Every slot on its own is consistent
       with the walk — 0, 2, 4 — so only counting them catches this. */
    build_bind_strategy(&short_run, "AA.");
    /* Same shape as the good one, but the second array is twice as long. Both
       slots bind; only comparing their lengths catches it. */
    build_bind_strategy_sized(&ragged, "AA...", 1);
    CHECK(deflate_fixture(&good, &good_z, &good_size) == 0, "compress failed");
    CHECK(deflate_fixture(&wrong, &wrong_z, &wrong_size) == 0, "compress failed");
    CHECK(deflate_fixture(&short_run, &short_z, &short_size) == 0,
          "compress failed");
    CHECK(deflate_fixture(&ragged, &ragged_z, &ragged_size) == 0,
          "compress failed");
    payloads[0] = good_z;      payload_sizes[0] = good_size;
    payloads[1] = wrong_z;     payload_sizes[1] = wrong_size;
    payloads[2] = short_z;     payload_sizes[2] = short_size;
    payloads[3] = ragged_z;    payload_sizes[3] = ragged_size;
    CHECK(write_archive_payloads(archive_path, names, payloads, payload_sizes,
                                 4u) == 0, "bind archive write failed");
    if (pe_monker_mkr_read(archive_path, &archive) != PE_MONKER_MKR_OK) {
        CHECK(0, "bind archive was rejected");
        mpf_tree_free(tree);
        free(good_z);
        free(wrong_z);
        free(short_z);
        free(ragged_z);
        return;
    }

    if (pe_monker_mkr_read_strategy(&archive, "storedstrategy0",
                                    &strategy) != PE_MONKER_MKR_OK) {
        CHECK(0, "bind strategy was not decoded");
    } else {
        CHECK(pe_monker_mkr_bind_strategy(tree, &strategy, map,
                                          sizeof(map) / sizeof(map[0])) ==
                  PE_MONKER_MKR_OK,
              "the last-to-first pattern was not bound");
        CHECK(map[0] == 0 && map[1] == 2 && map[2] == 4 && map[3] == 3 &&
                  map[4] == 1,
              "slots bound to %d %d %d %d %d, expected 0 2 4 3 1",
              map[0], map[1], map[2], map[3], map[4]);
        {
            /* Two values per slot over two-action nodes: one class. */
            uint32_t classes = 0u;
            CHECK(pe_monker_mkr_strategy_class_count(tree, &strategy,
                                                     &classes) ==
                      PE_MONKER_MKR_OK && classes == 1u,
                  "class count came out %u, expected 1", classes);
        }
    }
    pe_monker_mkr_strategy_free(&strategy);

    /* The same tree, the same slot count, arrays one node off. Accepting this
       is what a mapping that trusts instead of checking would do. */
    if (pe_monker_mkr_read_strategy(&archive, "storedstrategy1",
                                    &strategy) != PE_MONKER_MKR_OK) {
        CHECK(0, "second bind strategy was not decoded");
    } else {
        CHECK(pe_monker_mkr_bind_strategy(tree, &strategy, map,
                                          sizeof(map) / sizeof(map[0])) ==
                  PE_MONKER_MKR_ERR_BAD_ARCHIVE,
              "a plain-preorder layout was bound anyway");
    }
    pe_monker_mkr_strategy_free(&strategy);

    if (pe_monker_mkr_read_strategy(&archive, "storedstrategy2",
                                    &strategy) != PE_MONKER_MKR_OK) {
        CHECK(0, "short bind strategy was not decoded");
    } else {
        CHECK(pe_monker_mkr_bind_strategy(tree, &strategy, map,
                                          sizeof(map) / sizeof(map[0])) ==
                  PE_MONKER_MKR_ERR_BAD_ARCHIVE,
              "a strategy with fewer slots than the tree has nodes was bound");
    }
    pe_monker_mkr_strategy_free(&strategy);

    if (pe_monker_mkr_read_strategy(&archive, "storedstrategy3",
                                    &strategy) != PE_MONKER_MKR_OK) {
        CHECK(0, "ragged bind strategy was not decoded");
    } else {
        uint32_t classes = 0u;
        CHECK(pe_monker_mkr_bind_strategy(tree, &strategy, map,
                                          sizeof(map) / sizeof(map[0])) ==
                  PE_MONKER_MKR_OK,
              "the ragged strategy should still bind: its shape is right");
        CHECK(pe_monker_mkr_strategy_class_count(tree, &strategy, &classes) ==
                  PE_MONKER_MKR_ERR_BAD_ARCHIVE,
              "slots disagreeing on the class count were accepted (%u)",
              classes);
    }
    pe_monker_mkr_strategy_free(&strategy);
    pe_monker_mkr_free(&archive);
    mpf_tree_free(tree);
    free(good_z);
    free(wrong_z);
    free(short_z);
    free(ragged_z);
}

/*
 * A real saved run, when one is at hand.
 *
 * The archive is 800 KB, too big to embed the way the .tree fixture is, so
 * this reads from POKER_MONKER_MKR and does nothing when that is unset. It is
 * therefore not CI coverage — the fixtures above are — but it is the only
 * check that confronts bytes nobody here wrote, and the reader it exercises
 * spent its whole life passing tests while unable to open a single one.
 */
static void test_real_saved_run(void)
{
    const char *path = getenv("POKER_MONKER_MKR");
    pe_monker_mkr_t archive;
    pe_monker_mkr_metadata_t metadata;
    pe_monker_mkr_strategy_t strategy;
    uint32_t index;
    uint32_t arrays = 0u;
    int64_t iscount = 0;

    if (path == NULL || path[0] == '\0') {
        printf("  (POKER_MONKER_MKR unset: real saved run not checked)\n");
        return;
    }
    if (pe_monker_mkr_read(path, &archive) != PE_MONKER_MKR_OK) {
        CHECK(0, "a real .mkr was rejected");
        return;
    }
    CHECK(pe_monker_mkr_read_metadata(&archive, &metadata) == PE_MONKER_MKR_OK,
          "a real .mkr's metadata was not decoded");
    CHECK(metadata.flop_buckets > 0u, "flopBuckets came back as zero");
    iscount = metadata.iscount;
    pe_monker_mkr_metadata_free(&metadata);

    CHECK(pe_monker_mkr_read_strategy(&archive, "storedstrategy0",
                                      &strategy) == PE_MONKER_MKR_OK,
          "a real storedstrategy0 was not decoded");
    for (index = 0u; index < strategy.slot_count; ++index)
        if (strategy.slots[index].kind != PE_MONKER_SLOT_ABSENT)
            arrays++;
    CHECK(strategy.slot_count > 0u && arrays > 0u,
          "a real storedstrategy0 decoded to nothing");
    printf("  real run: %u slots, %u arrays, bucket count %d\n",
           strategy.slot_count, arrays, strategy.bucket_count);

    /* And, when the matching tree is named too, that the slots land on it. */
    {
        const char *tree_path = getenv("POKER_MONKER_TREE");
        mpf_tree_def_t *tree = NULL;
        if (tree_path != NULL && tree_path[0] != '\0' &&
            pe_monker_tree_load(tree_path, &tree) == PE_MONKER_OK &&
            tree != NULL) {
            int32_t *map = (int32_t *)malloc((size_t)strategy.slot_count *
                                             sizeof(*map));
            CHECK(map != NULL, "bind map allocation failed");
            if (map != NULL) {
                uint32_t classes = 0u;
                int decisions = 0;
                int node;
                CHECK(pe_monker_mkr_bind_strategy(tree, &strategy, map,
                                                  strategy.slot_count) ==
                          PE_MONKER_MKR_OK,
                      "a real strategy did not bind to its own tree");
                CHECK(pe_monker_mkr_strategy_class_count(tree, &strategy,
                                                         &classes) ==
                          PE_MONKER_MKR_OK,
                      "a real strategy's slots disagree on the class count");
                for (node = 0; node < tree->node_count; ++node)
                    if (tree->nodes[node].action_count > 0)
                        decisions++;
                /* The archive states its own infoset count. It must be the
                   decision nodes times the classes a slot is indexed by —
                   two numbers this reader derives from entirely different
                   parts of the file. */
                CHECK((int64_t)classes * decisions == iscount,
                      "%u classes over %d decision nodes is %lld, but the "
                      "archive says %lld infosets",
                      classes, decisions,
                      (long long)((int64_t)classes * decisions),
                      (long long)iscount);
                printf("  bound to a %d-node tree: %u classes x %d decisions "
                       "= %lld infosets\n", tree->node_count, classes,
                       decisions, (long long)((int64_t)classes * decisions));
                free(map);
            }
            mpf_tree_free(tree);
        }
    }
    pe_monker_mkr_strategy_free(&strategy);
    pe_monker_mkr_free(&archive);
}

int main(void)
{
    test_entry_listing();
    test_encoding_and_corruption();
    test_java_scalars_and_strategy();
    test_slot_to_node_binding();
    test_real_saved_run();
    if (failures != 0)
        return 1;
    puts("test_monker_mkr: UTF-16BE ZIP entry listing passed");
    return 0;
}
