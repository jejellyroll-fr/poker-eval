/*
 * monker_mkr_reader.c - read the entry table of a MonkerSolver .mkr archive
 */

#include <poker_eval/solver/pe_monker.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    if (archive == NULL)
        return;
    for (index = 0; index < archive->count; ++index)
        free(archive->entries[index].name);
    free(archive->entries);
    archive->entries = NULL;
    archive->count = 0u;
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

    free(data);
    return PE_MONKER_MKR_OK;

fail:
    free(data);
    pe_monker_mkr_free(out);
    return status;
}
