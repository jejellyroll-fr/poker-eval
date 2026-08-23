/*
 * monker_mkr_reader.c - read the entry table of a MonkerSolver .mkr archive
 */

#include <poker_eval/solver/pe_monker.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define PE_MONKER_MKR_EOCD_SIGNATURE 0x06054b50u
#define PE_MONKER_MKR_CENTRAL_SIGNATURE 0x02014b50u
#define PE_MONKER_MKR_LOCAL_SIGNATURE 0x04034b50u
#define PE_MONKER_MKR_DESCRIPTOR_SIGNATURE 0x08074b50u
#define PE_MONKER_MKR_EOCD_SIZE 22u
#define PE_MONKER_MKR_CENTRAL_SIZE 46u
#define PE_MONKER_MKR_LOCAL_SIZE 30u
#define PE_MONKER_MKR_MAX_SCAN (22u + 65535u)
#define PE_MONKER_MKR_DATA_DESCRIPTOR_SIZE 12u
#define PE_MONKER_MKR_DATA_DESCRIPTOR_WITH_SIGNATURE 16u
#define PE_MONKER_MKR_DATA_DESCRIPTOR_FLAG 0x0008u
#define PE_MONKER_MKR_ENCRYPTED_FLAG 0x0001u

typedef struct
{
    unsigned char *data;
    size_t size;
} mkr_file_t;

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int range_is_inside(size_t offset, size_t length, size_t total)
{
    return offset <= total && length <= total - offset;
}

static pe_monker_mkr_status_t read_file(const char *path,
                                        unsigned char **out_data,
                                        size_t *out_size)
{
    FILE *file;
    long file_size;
    unsigned char *data;
    size_t size;

    file = fopen(path, "rb");
    if (file == NULL)
        return PE_MONKER_MKR_ERR_OPEN;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return PE_MONKER_MKR_ERR_IO;
    }
    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return PE_MONKER_MKR_ERR_IO;
    }
    if ((uintmax_t)file_size > (uintmax_t)SIZE_MAX) {
        fclose(file);
        return PE_MONKER_MKR_ERR_TOO_LARGE;
    }
    size = (size_t)file_size;

    data = (unsigned char *)malloc(size == 0 ? 1u : size);
    if (data == NULL) {
        fclose(file);
        return PE_MONKER_MKR_ERR_NO_MEMORY;
    }
    if (fseek(file, 0, SEEK_SET) != 0 ||
        (size != 0 && fread(data, 1, size, file) != size)) {
        free(data);
        fclose(file);
        return PE_MONKER_MKR_ERR_IO;
    }
    if (fclose(file) != 0) {
        free(data);
        return PE_MONKER_MKR_ERR_IO;
    }

    *out_data = data;
    *out_size = size;
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t encode_utf8(uint32_t codepoint,
                                           char *out,
                                           size_t *position,
                                           size_t capacity)
{
    size_t needed;

    if (codepoint <= 0x7fu)
        needed = 1u;
    else if (codepoint <= 0x7ffu)
        needed = 2u;
    else if (codepoint <= 0xffffu)
        needed = 3u;
    else
        needed = 4u;

    if (*position > capacity || needed > capacity - *position)
        return PE_MONKER_MKR_ERR_TOO_LARGE;

    if (needed == 1u) {
        out[(*position)++] = (char)codepoint;
    } else if (needed == 2u) {
        out[(*position)++] = (char)(0xc0u | (codepoint >> 6));
        out[(*position)++] = (char)(0x80u | (codepoint & 0x3fu));
    } else if (needed == 3u) {
        out[(*position)++] = (char)(0xe0u | (codepoint >> 12));
        out[(*position)++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        out[(*position)++] = (char)(0x80u | (codepoint & 0x3fu));
    } else {
        out[(*position)++] = (char)(0xf0u | (codepoint >> 18));
        out[(*position)++] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
        out[(*position)++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        out[(*position)++] = (char)(0x80u | (codepoint & 0x3fu));
    }
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t decode_name(const unsigned char *name,
                                          size_t name_length,
                                          char **out_name)
{
    size_t position = 0;
    size_t input = 2u;
    char *decoded;

    if (name_length < 2u || (name_length & 1u) != 0u)
        return PE_MONKER_MKR_ERR_BAD_ENCODING;
    if (name[0] == 0xffu && name[1] == 0xfeu)
        return PE_MONKER_MKR_ERR_UTF16LE_BOM;
    if (name[0] != 0xfeu || name[1] != 0xffu)
        return PE_MONKER_MKR_ERR_BAD_ENCODING;

    decoded = (char *)malloc((name_length - 2u) * 4u + 1u);
    if (decoded == NULL)
        return PE_MONKER_MKR_ERR_NO_MEMORY;

    while (input < name_length) {
        uint16_t unit = (uint16_t)(((uint16_t)name[input] << 8) |
                                   (uint16_t)name[input + 1u]);
        uint32_t codepoint = unit;
        pe_monker_mkr_status_t status;

        input += 2u;
        if (unit == 0u) {
            free(decoded);
            return PE_MONKER_MKR_ERR_BAD_ENCODING;
        }
        if (unit >= 0xd800u && unit <= 0xdbffu) {
            uint16_t low;
            if (input >= name_length) {
                free(decoded);
                return PE_MONKER_MKR_ERR_BAD_ENCODING;
            }
            low = (uint16_t)(((uint16_t)name[input] << 8) |
                             (uint16_t)name[input + 1u]);
            if (low < 0xdc00u || low > 0xdfffu) {
                free(decoded);
                return PE_MONKER_MKR_ERR_BAD_ENCODING;
            }
            input += 2u;
            codepoint = 0x10000u +
                        (((uint32_t)unit - 0xd800u) << 10) +
                        ((uint32_t)low - 0xdc00u);
        } else if (unit >= 0xdc00u && unit <= 0xdfffu) {
            free(decoded);
            return PE_MONKER_MKR_ERR_BAD_ENCODING;
        }

        status = encode_utf8(codepoint, decoded, &position,
                             (name_length - 2u) * 4u);
        if (status != PE_MONKER_MKR_OK) {
            free(decoded);
            return status;
        }
    }
    decoded[position] = '\0';
    *out_name = decoded;
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t validate_local_entry(
    const unsigned char *data,
    size_t file_size,
    const unsigned char *central_name,
    size_t central_name_length,
    uint16_t central_flags,
    uint32_t central_compressed_size,
    uint32_t central_uncompressed_size,
    uint32_t local_offset)
{
    size_t local_name_length;
    size_t local_extra_length;
    size_t data_offset;
    size_t data_end;
    uint16_t local_flags;
    uint32_t local_compressed_size;
    uint32_t local_uncompressed_size;

    if (!range_is_inside((size_t)local_offset, PE_MONKER_MKR_LOCAL_SIZE,
                         file_size))
        return PE_MONKER_MKR_ERR_TRUNCATED;
    if (read_u32(data + local_offset) != PE_MONKER_MKR_LOCAL_SIGNATURE)
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;

    local_flags = read_u16(data + local_offset + 6u);
    local_compressed_size = read_u32(data + local_offset + 18u);
    local_uncompressed_size = read_u32(data + local_offset + 22u);
    local_name_length = read_u16(data + local_offset + 26u);
    local_extra_length = read_u16(data + local_offset + 28u);
    if (!range_is_inside((size_t)local_offset + PE_MONKER_MKR_LOCAL_SIZE,
                         local_name_length + local_extra_length, file_size))
        return PE_MONKER_MKR_ERR_TRUNCATED;
    if (local_name_length != central_name_length ||
        memcmp(data + local_offset + PE_MONKER_MKR_LOCAL_SIZE,
               central_name, central_name_length) != 0)
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;

    data_offset = (size_t)local_offset + PE_MONKER_MKR_LOCAL_SIZE +
                  local_name_length + local_extra_length;
    if (!range_is_inside(data_offset, central_compressed_size, file_size))
        return PE_MONKER_MKR_ERR_TRUNCATED;
    data_end = data_offset + central_compressed_size;

    if ((central_flags & PE_MONKER_MKR_ENCRYPTED_FLAG) != 0u)
        return PE_MONKER_MKR_ERR_UNSUPPORTED;
    if ((local_flags & PE_MONKER_MKR_DATA_DESCRIPTOR_FLAG) != 0u) {
        size_t descriptor_size = PE_MONKER_MKR_DATA_DESCRIPTOR_SIZE;
        if (!range_is_inside(data_end, descriptor_size, file_size))
            return PE_MONKER_MKR_ERR_TRUNCATED;
        if (read_u32(data + data_end) == PE_MONKER_MKR_DESCRIPTOR_SIGNATURE)
            descriptor_size = PE_MONKER_MKR_DATA_DESCRIPTOR_WITH_SIGNATURE;
        if (!range_is_inside(data_end, descriptor_size, file_size))
            return PE_MONKER_MKR_ERR_TRUNCATED;
        return PE_MONKER_MKR_OK;
    }

    if (local_compressed_size != central_compressed_size ||
        local_uncompressed_size != central_uncompressed_size)
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t locate_entry_payload(
    const pe_monker_mkr_t *archive, size_t index,
    mkr_file_t **out_file, const pe_monker_mkr_entry_t **out_entry,
    size_t *out_offset)
{
    mkr_file_t *file;
    const pe_monker_mkr_entry_t *entry;
    size_t local_offset;
    size_t name_length;
    size_t extra_length;
    size_t data_offset;

    if (archive == NULL || out_file == NULL || out_entry == NULL ||
        out_offset == NULL)
        return PE_MONKER_MKR_ERR_NULL_ARGUMENT;
    if (index >= archive->count || archive->private_data == NULL)
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    file = (mkr_file_t *)archive->private_data;
    entry = &archive->entries[index];
    local_offset = (size_t)entry->local_header_offset;
    if (!range_is_inside(local_offset, PE_MONKER_MKR_LOCAL_SIZE, file->size) ||
        read_u32(file->data + local_offset) != PE_MONKER_MKR_LOCAL_SIGNATURE)
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    name_length = read_u16(file->data + local_offset + 26u);
    extra_length = read_u16(file->data + local_offset + 28u);
    data_offset = local_offset + PE_MONKER_MKR_LOCAL_SIZE +
                  name_length + extra_length;
    if (!range_is_inside(data_offset, entry->compressed_size, file->size))
        return PE_MONKER_MKR_ERR_TRUNCATED;
    *out_file = file;
    *out_entry = entry;
    *out_offset = data_offset;
    return PE_MONKER_MKR_OK;
}

pe_monker_mkr_status_t pe_monker_mkr_entry_read(
    const pe_monker_mkr_t *archive, size_t index,
    unsigned char **out_data, size_t *out_size)
{
    mkr_file_t *file;
    const pe_monker_mkr_entry_t *entry;
    size_t data_offset;
    unsigned char *result;
    pe_monker_mkr_status_t status;

    if (out_data == NULL || out_size == NULL)
        return PE_MONKER_MKR_ERR_NULL_ARGUMENT;
    *out_data = NULL;
    *out_size = 0u;
    status = locate_entry_payload(archive, index, &file, &entry,
                                  &data_offset);
    if (status != PE_MONKER_MKR_OK)
        return status;
    if ((entry->flags & PE_MONKER_MKR_ENCRYPTED_FLAG) != 0u)
        return PE_MONKER_MKR_ERR_UNSUPPORTED;
    if (entry->method == 0u) {
        if (entry->compressed_size != entry->uncompressed_size)
            return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
        result = (unsigned char *)malloc(entry->uncompressed_size == 0u
                                             ? 1u : entry->uncompressed_size);
        if (result == NULL)
            return PE_MONKER_MKR_ERR_NO_MEMORY;
        if (entry->uncompressed_size != 0u)
            memcpy(result, file->data + data_offset, entry->uncompressed_size);
        *out_data = result;
        *out_size = entry->uncompressed_size;
        return PE_MONKER_MKR_OK;
    }
    if (entry->method != 8u)
        return PE_MONKER_MKR_ERR_UNSUPPORTED;

    result = (unsigned char *)malloc(entry->uncompressed_size == 0u
                                         ? 1u : entry->uncompressed_size);
    if (result == NULL)
        return PE_MONKER_MKR_ERR_NO_MEMORY;
    {
        z_stream stream;
        int inflate_status;
        memset(&stream, 0, sizeof(stream));
        stream.next_in = file->data + data_offset;
        stream.avail_in = (uInt)entry->compressed_size;
        stream.next_out = result;
        stream.avail_out = (uInt)(entry->uncompressed_size == 0u
                                      ? 1u : entry->uncompressed_size);
        inflate_status = inflateInit2(&stream, -MAX_WBITS);
        if (inflate_status == Z_OK)
            inflate_status = inflate(&stream, Z_FINISH);
        if (inflate_status != Z_STREAM_END ||
            stream.total_out != entry->uncompressed_size ||
            stream.total_in != entry->compressed_size) {
            inflateEnd(&stream);
            free(result);
            return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
        }
        inflateEnd(&stream);
    }
    *out_data = result;
    *out_size = entry->uncompressed_size;
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t find_end_record(const unsigned char *data,
                                              size_t file_size,
                                              size_t *out_offset)
{
    size_t first = file_size > PE_MONKER_MKR_MAX_SCAN
                       ? file_size - PE_MONKER_MKR_MAX_SCAN
                       : 0u;
    size_t position = file_size;
    int saw_signature = 0;

    while (position > first) {
        --position;
        if (!range_is_inside(position, 4u, file_size) ||
            read_u32(data + position) != PE_MONKER_MKR_EOCD_SIGNATURE)
            continue;
        saw_signature = 1;
        if (!range_is_inside(position, PE_MONKER_MKR_EOCD_SIZE, file_size))
            continue;
        if ((size_t)read_u16(data + position + 20u) +
                PE_MONKER_MKR_EOCD_SIZE > file_size - position)
            return PE_MONKER_MKR_ERR_TRUNCATED;
        *out_offset = position;
        return PE_MONKER_MKR_OK;
    }
    return saw_signature ? PE_MONKER_MKR_ERR_TRUNCATED
                         : PE_MONKER_MKR_ERR_BAD_ARCHIVE;
}

const char *pe_monker_mkr_status_string(pe_monker_mkr_status_t status)
{
    switch (status) {
    case PE_MONKER_MKR_OK: return "ok";
    case PE_MONKER_MKR_ERR_NULL_ARGUMENT: return "null argument";
    case PE_MONKER_MKR_ERR_OPEN: return "cannot open archive";
    case PE_MONKER_MKR_ERR_IO: return "archive I/O error";
    case PE_MONKER_MKR_ERR_BAD_ARCHIVE: return "corrupt ZIP archive";
    case PE_MONKER_MKR_ERR_TRUNCATED: return "truncated ZIP archive";
    case PE_MONKER_MKR_ERR_BAD_ENCODING: return "entry name is not UTF-16BE with BOM";
    case PE_MONKER_MKR_ERR_UTF16LE_BOM: return "entry name has unsupported UTF-16LE BOM";
    case PE_MONKER_MKR_ERR_UNSUPPORTED: return "unsupported ZIP feature";
    case PE_MONKER_MKR_ERR_NO_MEMORY: return "out of memory";
    case PE_MONKER_MKR_ERR_TOO_LARGE: return "archive is too large";
    default: return "unknown Monker archive status";
    }
}

void pe_monker_mkr_free(pe_monker_mkr_t *archive)
{
    size_t index;
    mkr_file_t *file;

    if (archive == NULL)
        return;
    for (index = 0; index < archive->count; ++index)
        free(archive->entries[index].name);
    free(archive->entries);
    archive->entries = NULL;
    archive->count = 0u;
    file = (mkr_file_t *)archive->private_data;
    if (file != NULL) {
        free(file->data);
        free(file);
    }
    archive->private_data = NULL;
}

pe_monker_mkr_status_t pe_monker_mkr_read(const char *path,
                                          pe_monker_mkr_t *out)
{
    unsigned char *data = NULL;
    size_t file_size = 0u;
    size_t eocd_offset;
    size_t central_offset;
    size_t central_size;
    size_t position;
    uint16_t disk;
    uint16_t central_disk;
    uint16_t entries_on_disk;
    uint16_t entries_total;
    uint32_t central_offset32;
    uint32_t central_size32;
    pe_monker_mkr_status_t status;
    size_t index;

    if (path == NULL || out == NULL)
        return PE_MONKER_MKR_ERR_NULL_ARGUMENT;
    out->entries = NULL;
    out->count = 0u;
    out->private_data = NULL;

    status = read_file(path, &data, &file_size);
    if (status != PE_MONKER_MKR_OK)
        return status;
    status = find_end_record(data, file_size, &eocd_offset);
    if (status != PE_MONKER_MKR_OK)
        goto fail;

    disk = read_u16(data + eocd_offset + 4u);
    central_disk = read_u16(data + eocd_offset + 6u);
    entries_on_disk = read_u16(data + eocd_offset + 8u);
    entries_total = read_u16(data + eocd_offset + 10u);
    central_size32 = read_u32(data + eocd_offset + 12u);
    central_offset32 = read_u32(data + eocd_offset + 16u);
    if (disk != 0u || central_disk != 0u || entries_on_disk != entries_total)
        status = PE_MONKER_MKR_ERR_UNSUPPORTED;
    else if (central_size32 == UINT32_MAX || central_offset32 == UINT32_MAX ||
             entries_total == UINT16_MAX)
        status = PE_MONKER_MKR_ERR_UNSUPPORTED;
    else {
        central_offset = (size_t)central_offset32;
        central_size = (size_t)central_size32;
        if (!range_is_inside(central_offset, central_size, file_size))
            status = PE_MONKER_MKR_ERR_TRUNCATED;
        else
            status = PE_MONKER_MKR_OK;
    }
    if (status != PE_MONKER_MKR_OK)
        goto fail;

    out->entries = entries_total == 0u
                       ? NULL
                       : (pe_monker_mkr_entry_t *)calloc(
                             entries_total, sizeof(*out->entries));
    if (entries_total != 0u && out->entries == NULL) {
        status = PE_MONKER_MKR_ERR_NO_MEMORY;
        goto fail;
    }
    out->count = entries_total;
    position = central_offset;
    for (index = 0; index < entries_total; ++index) {
        size_t name_length;
        size_t extra_length;
        size_t comment_length;
        size_t record_length;
        const unsigned char *name;
        pe_monker_mkr_entry_t *entry = &out->entries[index];

        if (!range_is_inside(position, PE_MONKER_MKR_CENTRAL_SIZE,
                             central_offset + central_size)) {
            status = PE_MONKER_MKR_ERR_TRUNCATED;
            goto fail;
        }
        if (read_u32(data + position) != PE_MONKER_MKR_CENTRAL_SIGNATURE) {
            status = PE_MONKER_MKR_ERR_BAD_ARCHIVE;
            goto fail;
        }
        entry->flags = read_u16(data + position + 8u);
        entry->method = read_u16(data + position + 10u);
        entry->compressed_size = read_u32(data + position + 20u);
        entry->uncompressed_size = read_u32(data + position + 24u);
        name_length = read_u16(data + position + 28u);
        extra_length = read_u16(data + position + 30u);
        comment_length = read_u16(data + position + 32u);
        entry->local_header_offset = read_u32(data + position + 42u);
        if (entry->compressed_size == UINT32_MAX ||
            entry->uncompressed_size == UINT32_MAX ||
            entry->local_header_offset == UINT32_MAX)
        {
            status = PE_MONKER_MKR_ERR_UNSUPPORTED;
            goto fail;
        }
        record_length = PE_MONKER_MKR_CENTRAL_SIZE + name_length +
                        extra_length + comment_length;
        if (!range_is_inside(position, record_length,
                             central_offset + central_size)) {
            status = PE_MONKER_MKR_ERR_TRUNCATED;
            goto fail;
        }
        name = data + position + PE_MONKER_MKR_CENTRAL_SIZE;
        status = decode_name(name, name_length, &entry->name);
        if (status != PE_MONKER_MKR_OK)
            goto fail;
        status = validate_local_entry(data, file_size, name, name_length,
                                      entry->flags, entry->compressed_size,
                                      entry->uncompressed_size,
                                      entry->local_header_offset);
        if (status != PE_MONKER_MKR_OK)
            goto fail;
        position += record_length;
    }
    if (position != central_offset + central_size) {
        status = PE_MONKER_MKR_ERR_BAD_ARCHIVE;
        goto fail;
    }

    out->private_data = malloc(sizeof(mkr_file_t));
    if (out->private_data == NULL) {
        status = PE_MONKER_MKR_ERR_NO_MEMORY;
        goto fail;
    }
    ((mkr_file_t *)out->private_data)->data = data;
    ((mkr_file_t *)out->private_data)->size = file_size;
    return PE_MONKER_MKR_OK;

fail:
    free(data);
    pe_monker_mkr_free(out);
    return status;
}

typedef enum
{
    JAVA_VALUE_NULL,
    JAVA_VALUE_STRING,
    JAVA_VALUE_INTEGER,
    JAVA_VALUE_LONG,
    JAVA_VALUE_DOUBLE,
    JAVA_VALUE_BYTES,
    JAVA_VALUE_SHORTS,
    JAVA_VALUE_OBJECTS
} java_value_kind_t;

typedef struct java_value_t java_value_t;

struct java_value_t
{
    java_value_kind_t kind;
    union
    {
        char *string;
        int32_t integer;
        int64_t long_value;
        double real;
        struct { unsigned char *data; uint32_t count; } bytes;
        struct { uint16_t *data; uint32_t count; } shorts;
        struct { java_value_t *items; uint32_t count; } objects;
    } value;
};

typedef struct
{
    unsigned char type;
    char *name;
} java_field_t;

typedef struct java_classdesc_t java_classdesc_t;

struct java_classdesc_t
{
    char *name;
    java_field_t *fields;
    uint16_t field_count;
    java_classdesc_t *super_desc;
};

typedef struct
{
    const unsigned char *data;
    size_t size;
    size_t position;
} java_reader_t;

#define JAVA_STREAM_MAGIC 0xacedu
#define JAVA_STREAM_VERSION 0x0005u
#define JAVA_TC_NULL 0x70u
#define JAVA_TC_REFERENCE 0x71u
#define JAVA_TC_CLASSDESC 0x72u
#define JAVA_TC_OBJECT 0x73u
#define JAVA_TC_STRING 0x74u
#define JAVA_TC_ARRAY 0x75u
#define JAVA_TC_CLASS 0x76u
#define JAVA_TC_BLOCKDATA 0x77u
#define JAVA_TC_ENDBLOCKDATA 0x78u
#define JAVA_TC_RESET 0x79u
#define JAVA_TC_BLOCKDATALONG 0x7au
#define JAVA_TC_EXCEPTION 0x7bu
#define JAVA_TC_LONGSTRING 0x7cu
#define JAVA_TC_PROXYCLASSDESC 0x7du
#define JAVA_TC_ENUM 0x7eu

static uint16_t java_be16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t java_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t java_be64(const unsigned char *p)
{
    uint64_t result = 0u;
    unsigned index;
    for (index = 0u; index < 8u; ++index)
        result = (result << 8) | p[index];
    return result;
}

static pe_monker_mkr_status_t java_take(java_reader_t *reader,
                                        size_t length,
                                        const unsigned char **out)
{
    if (!range_is_inside(reader->position, length, reader->size))
        return PE_MONKER_MKR_ERR_TRUNCATED;
    *out = reader->data + reader->position;
    reader->position += length;
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t java_u8(java_reader_t *reader,
                                      unsigned char *out)
{
    const unsigned char *data;
    pe_monker_mkr_status_t status = java_take(reader, 1u, &data);
    if (status == PE_MONKER_MKR_OK)
        *out = data[0];
    return status;
}

static pe_monker_mkr_status_t java_utf(java_reader_t *reader, char **out)
{
    const unsigned char *data;
    uint16_t length;
    char *result;
    pe_monker_mkr_status_t status;

    status = java_take(reader, 2u, &data);
    if (status != PE_MONKER_MKR_OK)
        return status;
    length = java_be16(data);
    status = java_take(reader, length, &data);
    if (status != PE_MONKER_MKR_OK)
        return status;
    result = (char *)malloc((size_t)length + 1u);
    if (result == NULL)
        return PE_MONKER_MKR_ERR_NO_MEMORY;
    memcpy(result, data, length);
    result[length] = '\0';
    *out = result;
    return PE_MONKER_MKR_OK;
}

static void java_value_free(java_value_t *value)
{
    uint32_t index;

    if (value == NULL)
        return;
    switch (value->kind) {
    case JAVA_VALUE_STRING:
        free(value->value.string);
        break;
    case JAVA_VALUE_BYTES:
        free(value->value.bytes.data);
        break;
    case JAVA_VALUE_SHORTS:
        free(value->value.shorts.data);
        break;
    case JAVA_VALUE_OBJECTS:
        for (index = 0u; index < value->value.objects.count; ++index)
            java_value_free(&value->value.objects.items[index]);
        free(value->value.objects.items);
        break;
    case JAVA_VALUE_NULL:
    case JAVA_VALUE_INTEGER:
    case JAVA_VALUE_LONG:
    case JAVA_VALUE_DOUBLE:
        break;
    default:
        break;
    }
    memset(value, 0, sizeof(*value));
}

static void java_classdesc_free(java_classdesc_t *desc)
{
    uint16_t index;

    if (desc == NULL)
        return;
    free(desc->name);
    for (index = 0u; index < desc->field_count; ++index)
        free(desc->fields[index].name);
    free(desc->fields);
    java_classdesc_free(desc->super_desc);
    free(desc);
}

static pe_monker_mkr_status_t java_read_value(java_reader_t *reader,
                                               java_value_t *out);

static pe_monker_mkr_status_t java_skip_annotations(java_reader_t *reader)
{
    unsigned char token;
    pe_monker_mkr_status_t status;

    for (;;) {
        status = java_u8(reader, &token);
        if (status != PE_MONKER_MKR_OK)
            return status;
        if (token == JAVA_TC_ENDBLOCKDATA)
            return PE_MONKER_MKR_OK;
        if (token == JAVA_TC_BLOCKDATA) {
            unsigned char length;
            const unsigned char *ignored;
            status = java_u8(reader, &length);
            if (status != PE_MONKER_MKR_OK)
                return status;
            status = java_take(reader, length, &ignored);
            if (status != PE_MONKER_MKR_OK)
                return status;
        } else if (token == JAVA_TC_BLOCKDATALONG) {
            const unsigned char *length;
            const unsigned char *ignored;
            status = java_take(reader, 4u, &length);
            if (status != PE_MONKER_MKR_OK)
                return status;
            status = java_take(reader, java_be32(length), &ignored);
            if (status != PE_MONKER_MKR_OK)
                return status;
        } else {
            return PE_MONKER_MKR_ERR_UNSUPPORTED;
        }
    }
}

static pe_monker_mkr_status_t java_read_classdesc(java_reader_t *reader,
                                                   java_classdesc_t **out)
{
    unsigned char token;
    const unsigned char *data;
    java_classdesc_t *desc;
    uint16_t index;
    pe_monker_mkr_status_t status;

    status = java_u8(reader, &token);
    if (status != PE_MONKER_MKR_OK)
        return status;
    if (token == JAVA_TC_NULL) {
        *out = NULL;
        return PE_MONKER_MKR_OK;
    }
    if (token != JAVA_TC_CLASSDESC)
        return token == JAVA_TC_REFERENCE ? PE_MONKER_MKR_ERR_UNSUPPORTED
                                          : PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    desc = (java_classdesc_t *)calloc(1u, sizeof(*desc));
    if (desc == NULL)
        return PE_MONKER_MKR_ERR_NO_MEMORY;
    status = java_utf(reader, &desc->name);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    status = java_take(reader, 8u, &data);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    status = java_u8(reader, &token);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    if ((token & 0x02u) == 0u)
        goto unsupported;
    status = java_take(reader, 2u, &data);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    desc->field_count = java_be16(data);
    desc->fields = desc->field_count == 0u
                       ? NULL
                       : (java_field_t *)calloc(desc->field_count,
                                                sizeof(*desc->fields));
    if (desc->field_count != 0u && desc->fields == NULL) {
        status = PE_MONKER_MKR_ERR_NO_MEMORY;
        goto fail;
    }
    for (index = 0u; index < desc->field_count; ++index) {
        status = java_u8(reader, &desc->fields[index].type);
        if (status != PE_MONKER_MKR_OK)
            goto fail;
        status = java_utf(reader, &desc->fields[index].name);
        if (status != PE_MONKER_MKR_OK)
            goto fail;
        if (desc->fields[index].type == 'L' ||
            desc->fields[index].type == '[') {
            status = java_u8(reader, &token);
            if (status != PE_MONKER_MKR_OK)
                goto fail;
            if (token == JAVA_TC_STRING) {
                char *signature;
                status = java_utf(reader, &signature);
                free(signature);
                if (status != PE_MONKER_MKR_OK)
                    goto fail;
            } else if (token != JAVA_TC_REFERENCE) {
                status = PE_MONKER_MKR_ERR_BAD_ARCHIVE;
                goto fail;
            } else {
                status = java_take(reader, 4u, &data);
                if (status != PE_MONKER_MKR_OK)
                    goto fail;
            }
        }
    }
    status = java_skip_annotations(reader);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    status = java_read_classdesc(reader, &desc->super_desc);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    *out = desc;
    return PE_MONKER_MKR_OK;

unsupported:
    status = PE_MONKER_MKR_ERR_UNSUPPORTED;
fail:
    java_classdesc_free(desc);
    return status;
}

static pe_monker_mkr_status_t java_read_primitive(java_reader_t *reader,
                                                  unsigned char type,
                                                  java_value_t *out)
{
    const unsigned char *data;
    pe_monker_mkr_status_t status;

    memset(out, 0, sizeof(*out));
    switch (type) {
    case 'B':
        status = java_take(reader, 1u, &data);
        if (status == PE_MONKER_MKR_OK) {
            out->kind = JAVA_VALUE_INTEGER;
            out->value.integer = (int8_t)data[0];
        }
        return status;
    case 'I':
        status = java_take(reader, 4u, &data);
        if (status == PE_MONKER_MKR_OK) {
            out->kind = JAVA_VALUE_INTEGER;
            out->value.integer = (int32_t)java_be32(data);
        }
        return status;
    case 'J':
        status = java_take(reader, 8u, &data);
        if (status == PE_MONKER_MKR_OK) {
            out->kind = JAVA_VALUE_LONG;
            out->value.long_value = (int64_t)java_be64(data);
        }
        return status;
    case 'D': {
        uint64_t bits;
        status = java_take(reader, 8u, &data);
        if (status != PE_MONKER_MKR_OK)
            return status;
        bits = java_be64(data);
        memcpy(&out->value.real, &bits, sizeof(bits));
        out->kind = JAVA_VALUE_DOUBLE;
        return PE_MONKER_MKR_OK;
    }
    case 'S':
        status = java_take(reader, 2u, &data);
        if (status == PE_MONKER_MKR_OK) {
            out->kind = JAVA_VALUE_INTEGER;
            out->value.integer = (int16_t)java_be16(data);
        }
        return status;
    case 'C':
        status = java_take(reader, 2u, &data);
        if (status == PE_MONKER_MKR_OK) {
            out->kind = JAVA_VALUE_INTEGER;
            out->value.integer = java_be16(data);
        }
        return status;
    case 'Z':
    case 'F':
        return PE_MONKER_MKR_ERR_UNSUPPORTED;
    default:
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    }
}

static pe_monker_mkr_status_t java_read_class_data(java_reader_t *reader,
                                                   const java_classdesc_t *desc,
                                                   java_value_t *out)
{
    uint16_t index;
    pe_monker_mkr_status_t status;

    if (desc->super_desc != NULL) {
        status = java_read_class_data(reader, desc->super_desc, out);
        if (status != PE_MONKER_MKR_OK)
            return status;
    }
    for (index = 0u; index < desc->field_count; ++index) {
        java_value_t value;
        memset(&value, 0, sizeof(value));
        if (desc->fields[index].type == 'L' ||
            desc->fields[index].type == '[') {
            status = java_read_value(reader, &value);
        } else {
            status = java_read_primitive(reader, desc->fields[index].type,
                                         &value);
        }
        if (status != PE_MONKER_MKR_OK) {
            java_value_free(&value);
            return status;
        }
        if (strcmp(desc->fields[index].name, "value") == 0) {
            java_value_free(out);
            *out = value;
        } else {
            java_value_free(&value);
        }
    }
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t java_read_array(java_reader_t *reader,
                                              java_value_t *out)
{
    java_classdesc_t *desc = NULL;
    const unsigned char *data;
    uint32_t count;
    uint32_t index;
    pe_monker_mkr_status_t status;

    status = java_read_classdesc(reader, &desc);
    if (status != PE_MONKER_MKR_OK)
        return status;
    if (desc == NULL || desc->name == NULL || desc->name[0] != '[') {
        java_classdesc_free(desc);
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    }
    status = java_take(reader, 4u, &data);
    if (status != PE_MONKER_MKR_OK) {
        java_classdesc_free(desc);
        return status;
    }
    count = java_be32(data);
    if (count > 100000000u) {
        java_classdesc_free(desc);
        return PE_MONKER_MKR_ERR_TOO_LARGE;
    }
    if (strcmp(desc->name, "[B") == 0) {
        out->kind = JAVA_VALUE_BYTES;
        out->value.bytes.count = count;
        out->value.bytes.data = count == 0u
                                    ? NULL
                                    : (unsigned char *)malloc(count);
        if (count != 0u && out->value.bytes.data == NULL) {
            java_classdesc_free(desc);
            return PE_MONKER_MKR_ERR_NO_MEMORY;
        }
        status = java_take(reader, count, &data);
        if (status == PE_MONKER_MKR_OK && count != 0u)
            memcpy(out->value.bytes.data, data, count);
    } else if (strcmp(desc->name, "[S") == 0) {
        out->kind = JAVA_VALUE_SHORTS;
        out->value.shorts.count = count;
        out->value.shorts.data = count == 0u
                                     ? NULL
                                     : (uint16_t *)malloc(
                                           (size_t)count * sizeof(uint16_t));
        if (count != 0u && out->value.shorts.data == NULL) {
            java_classdesc_free(desc);
            return PE_MONKER_MKR_ERR_NO_MEMORY;
        }
        for (index = 0u; index < count && status == PE_MONKER_MKR_OK; ++index) {
            status = java_take(reader, 2u, &data);
            if (status == PE_MONKER_MKR_OK)
                out->value.shorts.data[index] = java_be16(data);
        }
    } else if (strlen(desc->name) >= 2u && desc->name[1] == '[') {
        out->kind = JAVA_VALUE_OBJECTS;
        out->value.objects.count = count;
        out->value.objects.items = count == 0u
                                       ? NULL
                                       : (java_value_t *)calloc(
                                             count, sizeof(java_value_t));
        if (count != 0u && out->value.objects.items == NULL) {
            java_classdesc_free(desc);
            return PE_MONKER_MKR_ERR_NO_MEMORY;
        }
        for (index = 0u; index < count && status == PE_MONKER_MKR_OK; ++index)
            status = java_read_value(reader, &out->value.objects.items[index]);
    } else {
        status = PE_MONKER_MKR_ERR_UNSUPPORTED;
    }
    java_classdesc_free(desc);
    if (status != PE_MONKER_MKR_OK)
        java_value_free(out);
    return status;
}

static pe_monker_mkr_status_t java_read_value(java_reader_t *reader,
                                               java_value_t *out)
{
    unsigned char token;
    pe_monker_mkr_status_t status;

    memset(out, 0, sizeof(*out));
    status = java_u8(reader, &token);
    if (status != PE_MONKER_MKR_OK)
        return status;
    switch (token) {
    case JAVA_TC_NULL:
        out->kind = JAVA_VALUE_NULL;
        return PE_MONKER_MKR_OK;
    case JAVA_TC_STRING:
        out->kind = JAVA_VALUE_STRING;
        return java_utf(reader, &out->value.string);
    case JAVA_TC_ARRAY:
        return java_read_array(reader, out);
    case JAVA_TC_OBJECT: {
        java_classdesc_t *desc = NULL;
        status = java_read_classdesc(reader, &desc);
        if (status == PE_MONKER_MKR_OK && desc != NULL)
            status = java_read_class_data(reader, desc, out);
        java_classdesc_free(desc);
        return status;
    }
    case JAVA_TC_REFERENCE:
        return PE_MONKER_MKR_ERR_UNSUPPORTED;
    default:
        return PE_MONKER_MKR_ERR_UNSUPPORTED;
    }
}

static pe_monker_mkr_status_t parse_java_value(const unsigned char *data,
                                               size_t size,
                                               java_value_t *out)
{
    java_reader_t reader;
    const unsigned char *header;
    pe_monker_mkr_status_t status;

    if (size < 4u)
        return PE_MONKER_MKR_ERR_TRUNCATED;
    header = data;
    if (java_be16(header) != JAVA_STREAM_MAGIC ||
        java_be16(header + 2u) != JAVA_STREAM_VERSION)
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    reader.data = data;
    reader.size = size;
    reader.position = 4u;
    status = java_read_value(&reader, out);
    if (status != PE_MONKER_MKR_OK)
        return status;
    if (reader.position != reader.size) {
        java_value_free(out);
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    }
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t inflate_nested(unsigned char *data,
                                             size_t size,
                                             unsigned char **out_data,
                                             size_t *out_size)
{
    z_stream stream;
    unsigned char *result = NULL;
    size_t capacity = 4096u;
    int inflate_status;

    if (size == 0u)
        return PE_MONKER_MKR_ERR_TRUNCATED;
    if (size > UINT_MAX)
        return PE_MONKER_MKR_ERR_TOO_LARGE;
    result = (unsigned char *)malloc(capacity);
    if (result == NULL)
        return PE_MONKER_MKR_ERR_NO_MEMORY;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = data;
    stream.avail_in = (uInt)(size > UINT_MAX ? UINT_MAX : size);
    inflate_status = inflateInit(&stream);
    if (inflate_status != Z_OK) {
        free(result);
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    }
    while (inflate_status != Z_STREAM_END) {
        size_t used = (size_t)stream.total_out;
        if (used == capacity) {
            unsigned char *grown;
            size_t next_capacity = capacity > 0x40000000u
                                       ? capacity + 0x40000000u
                                       : capacity * 2u;
            if (next_capacity <= capacity || next_capacity > 1073741824u) {
                inflateEnd(&stream);
                free(result);
                return PE_MONKER_MKR_ERR_TOO_LARGE;
            }
            grown = (unsigned char *)realloc(result, next_capacity);
            if (grown == NULL) {
                inflateEnd(&stream);
                free(result);
                return PE_MONKER_MKR_ERR_NO_MEMORY;
            }
            result = grown;
            capacity = next_capacity;
        }
        stream.next_out = result + stream.total_out;
        stream.avail_out = (uInt)(capacity - (size_t)stream.total_out);
        inflate_status = inflate(&stream, Z_NO_FLUSH);
        if (inflate_status != Z_OK && inflate_status != Z_STREAM_END) {
            inflateEnd(&stream);
            free(result);
            return inflate_status == Z_BUF_ERROR && stream.avail_in == 0u
                       ? PE_MONKER_MKR_ERR_TRUNCATED
                       : PE_MONKER_MKR_ERR_BAD_ARCHIVE;
        }
        if (stream.avail_in == 0u && inflate_status != Z_STREAM_END) {
            inflateEnd(&stream);
            free(result);
            return PE_MONKER_MKR_ERR_TRUNCATED;
        }
    }
    if (stream.total_out == 0u) {
        unsigned char *shrunk = (unsigned char *)realloc(result, 1u);
        if (shrunk != NULL)
            result = shrunk;
    } else {
        unsigned char *shrunk = (unsigned char *)realloc(
            result, (size_t)stream.total_out);
        if (shrunk != NULL)
            result = shrunk;
    }
    *out_data = result;
    *out_size = (size_t)stream.total_out;
    inflateEnd(&stream);
    return PE_MONKER_MKR_OK;
}

static pe_monker_mkr_status_t find_entry(const pe_monker_mkr_t *archive,
                                         const char *name,
                                         size_t *out_index)
{
    size_t index;

    if (archive == NULL || name == NULL || out_index == NULL)
        return PE_MONKER_MKR_ERR_NULL_ARGUMENT;
    for (index = 0u; index < archive->count; ++index) {
        if (strcmp(archive->entries[index].name, name) == 0) {
            *out_index = index;
            return PE_MONKER_MKR_OK;
        }
    }
    return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
}

static pe_monker_mkr_status_t read_java_entry(const pe_monker_mkr_t *archive,
                                              const char *name,
                                              java_value_t *out)
{
    unsigned char *data = NULL;
    size_t size = 0u;
    size_t index;
    pe_monker_mkr_status_t status;

    status = find_entry(archive, name, &index);
    if (status != PE_MONKER_MKR_OK)
        return status;
    status = pe_monker_mkr_entry_read(archive, index, &data, &size);
    if (status == PE_MONKER_MKR_OK)
        status = parse_java_value(data, size, out);
    free(data);
    return status;
}

static int java_value_to_integer(const java_value_t *value, int64_t *out)
{
    if (value->kind == JAVA_VALUE_INTEGER) {
        *out = value->value.integer;
        return 1;
    }
    if (value->kind == JAVA_VALUE_LONG) {
        *out = value->value.long_value;
        return 1;
    }
    return 0;
}

static int java_value_to_double(const java_value_t *value, double *out)
{
    int64_t integer;
    if (value->kind == JAVA_VALUE_DOUBLE) {
        *out = value->value.real;
        return 1;
    }
    if (java_value_to_integer(value, &integer)) {
        *out = (double)integer;
        return 1;
    }
    return 0;
}

static pe_monker_mkr_status_t read_scalar_integer(
    const pe_monker_mkr_t *archive, const char *name, int64_t *out)
{
    java_value_t value;
    pe_monker_mkr_status_t status;
    memset(&value, 0, sizeof(value));
    status = read_java_entry(archive, name, &value);
    if (status == PE_MONKER_MKR_OK && !java_value_to_integer(&value, out))
        status = PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    java_value_free(&value);
    return status;
}

static pe_monker_mkr_status_t read_scalar_double(
    const pe_monker_mkr_t *archive, const char *name, double *out)
{
    java_value_t value;
    pe_monker_mkr_status_t status;
    memset(&value, 0, sizeof(value));
    status = read_java_entry(archive, name, &value);
    if (status == PE_MONKER_MKR_OK && !java_value_to_double(&value, out))
        status = PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    java_value_free(&value);
    return status;
}

void pe_monker_mkr_metadata_free(pe_monker_mkr_metadata_t *metadata)
{
    if (metadata == NULL)
        return;
    free(metadata->game);
    memset(metadata, 0, sizeof(*metadata));
}

pe_monker_mkr_status_t pe_monker_mkr_read_metadata(
    const pe_monker_mkr_t *archive, pe_monker_mkr_metadata_t *out)
{
    java_value_t game;
    int64_t integer;
    pe_monker_mkr_status_t status;

    if (archive == NULL || out == NULL)
        return PE_MONKER_MKR_ERR_NULL_ARGUMENT;
    memset(out, 0, sizeof(*out));
    memset(&game, 0, sizeof(game));
    status = read_java_entry(archive, "game", &game);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    if (game.kind != JAVA_VALUE_STRING) {
        status = PE_MONKER_MKR_ERR_BAD_ARCHIVE;
        goto fail;
    }
    out->game = game.value.string;
    game.value.string = NULL;
    java_value_free(&game);

    status = read_scalar_integer(archive, "iterations", &integer);
    if (status != PE_MONKER_MKR_OK || integer < 0)
        goto fail;
    out->iterations = integer;
    status = read_scalar_integer(archive, "flopBuckets", &integer);
    if (status != PE_MONKER_MKR_OK || integer < 0 || integer > UINT32_MAX)
        goto fail;
    out->flop_buckets = (uint32_t)integer;
    status = read_scalar_double(archive, "rakepercent", &out->rakepercent);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    status = read_scalar_double(archive, "rakecap", &out->rakecap);
    if (status != PE_MONKER_MKR_OK)
        goto fail;
    status = read_scalar_integer(archive, "rakeflags", &integer);
    if (status != PE_MONKER_MKR_OK || integer < INT32_MIN || integer > INT32_MAX)
        goto fail;
    out->rakeflags = (int32_t)integer;
    return PE_MONKER_MKR_OK;

fail:
    java_value_free(&game);
    pe_monker_mkr_metadata_free(out);
    return status;
}

void pe_monker_mkr_strategy_free(pe_monker_mkr_strategy_t *strategy)
{
    if (strategy == NULL)
        return;
    free(strategy->frequencies);
    memset(strategy, 0, sizeof(*strategy));
}

static pe_monker_mkr_status_t strategy_from_value(
    const java_value_t *value, uint32_t bucket_count,
    pe_monker_mkr_strategy_t *out)
{
    uint32_t class_count;
    uint32_t index;

    if (value->kind == JAVA_VALUE_NULL) {
        out->bucket_count = bucket_count;
        return PE_MONKER_MKR_OK;
    }
    if (bucket_count == 0u)
        return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
    if (value->kind == JAVA_VALUE_SHORTS) {
        if (value->value.shorts.count % bucket_count != 0u)
            return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
        class_count = value->value.shorts.count / bucket_count;
        out->frequencies = class_count == 0u
                               ? NULL
                               : (uint16_t *)malloc(
                                     (size_t)class_count * bucket_count *
                                     sizeof(*out->frequencies));
        if (class_count != 0u && out->frequencies == NULL)
            return PE_MONKER_MKR_ERR_NO_MEMORY;
        if (class_count != 0u)
            memcpy(out->frequencies, value->value.shorts.data,
                   (size_t)class_count * bucket_count *
                       sizeof(*out->frequencies));
        out->bucket_count = bucket_count;
        out->class_count = class_count;
        return PE_MONKER_MKR_OK;
    }
    if (value->kind == JAVA_VALUE_BYTES) {
        if (value->value.bytes.count % 2u != 0u ||
            value->value.bytes.count / 2u % bucket_count != 0u)
            return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
        class_count = (value->value.bytes.count / 2u) / bucket_count;
        out->frequencies = class_count == 0u
                               ? NULL
                               : (uint16_t *)malloc(
                                     (size_t)class_count * bucket_count *
                                     sizeof(*out->frequencies));
        if (class_count != 0u && out->frequencies == NULL)
            return PE_MONKER_MKR_ERR_NO_MEMORY;
        for (index = 0u; index < class_count * bucket_count; ++index)
            out->frequencies[index] = (uint16_t)(
                ((uint16_t)value->value.bytes.data[index * 2u] << 8) |
                (uint16_t)value->value.bytes.data[index * 2u + 1u]);
        out->bucket_count = bucket_count;
        out->class_count = class_count;
        return PE_MONKER_MKR_OK;
    }
    if (value->kind == JAVA_VALUE_OBJECTS) {
        class_count = value->value.objects.count;
        out->frequencies = class_count == 0u
                               ? NULL
                               : (uint16_t *)calloc(
                                     (size_t)class_count * bucket_count,
                                     sizeof(*out->frequencies));
        if (class_count != 0u && out->frequencies == NULL)
            return PE_MONKER_MKR_ERR_NO_MEMORY;
        for (index = 0u; index < class_count; ++index) {
            const java_value_t *row = &value->value.objects.items[index];
            uint32_t bucket;
            if (row->kind == JAVA_VALUE_NULL)
                continue;
            if (row->kind == JAVA_VALUE_SHORTS &&
                row->value.shorts.count == bucket_count) {
                for (bucket = 0u; bucket < bucket_count; ++bucket)
                    out->frequencies[index * bucket_count + bucket] =
                        row->value.shorts.data[bucket];
            } else if (row->kind == JAVA_VALUE_BYTES &&
                       row->value.bytes.count == bucket_count * 2u) {
                for (bucket = 0u; bucket < bucket_count; ++bucket)
                    out->frequencies[index * bucket_count + bucket] =
                        (uint16_t)(((uint16_t)row->value.bytes.data[bucket * 2u]
                                    << 8) |
                                   (uint16_t)row->value.bytes.data[bucket * 2u +
                                                                    1u]);
            } else {
                return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
            }
        }
        out->bucket_count = bucket_count;
        out->class_count = class_count;
        return PE_MONKER_MKR_OK;
    }
    return PE_MONKER_MKR_ERR_BAD_ARCHIVE;
}

pe_monker_mkr_status_t pe_monker_mkr_read_strategy(
    const pe_monker_mkr_t *archive, const char *entry_name,
    uint32_t bucket_count, pe_monker_mkr_strategy_t *out)
{
    unsigned char *compressed = NULL;
    unsigned char *java_data = NULL;
    size_t compressed_size = 0u;
    size_t java_size = 0u;
    java_value_t value;
    size_t index;
    pe_monker_mkr_status_t status;

    if (archive == NULL || entry_name == NULL || out == NULL)
        return PE_MONKER_MKR_ERR_NULL_ARGUMENT;
    memset(out, 0, sizeof(*out));
    memset(&value, 0, sizeof(value));
    status = find_entry(archive, entry_name, &index);
    if (status != PE_MONKER_MKR_OK)
        return status;
    status = pe_monker_mkr_entry_read(archive, index, &compressed,
                                      &compressed_size);
    if (status != PE_MONKER_MKR_OK)
        return status;
    status = inflate_nested(compressed, compressed_size, &java_data,
                            &java_size);
    free(compressed);
    if (status != PE_MONKER_MKR_OK)
        return status;
    status = parse_java_value(java_data, java_size, &value);
    free(java_data);
    if (status == PE_MONKER_MKR_OK)
        status = strategy_from_value(&value, bucket_count, out);
    java_value_free(&value);
    if (status != PE_MONKER_MKR_OK)
        pe_monker_mkr_strategy_free(out);
    return status;
}
