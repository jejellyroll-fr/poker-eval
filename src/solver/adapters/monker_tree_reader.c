/*
 * monker_tree_reader.c - MonkerSolver .tree header reader (MKR-01)
 */

#include <poker_eval/solver/pe_monker.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int read_bytes(FILE *file, unsigned char *out, size_t count)
{
    return fread(out, 1u, count, file) == count ? 0 : -1;
}

static int64_t decode_i64(const unsigned char *bytes)
{
    uint64_t value = 0u;
    unsigned i;

    for (i = 0u; i < 8u; ++i)
        value |= (uint64_t)bytes[i] << (8u * i);
    return (int64_t)value;
}

static int32_t decode_i32(const unsigned char *bytes)
{
    uint32_t value = 0u;
    unsigned i;

    for (i = 0u; i < 4u; ++i)
        value |= (uint32_t)bytes[i] << (8u * i);
    return (int32_t)value;
}

static double decode_f64(const unsigned char *bytes)
{
    uint64_t bits = (uint64_t)decode_i64(bytes);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int read_i64(FILE *file, int64_t *out)
{
    unsigned char bytes[8];
    if (read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    *out = decode_i64(bytes);
    return 0;
}

static int read_i32(FILE *file, int32_t *out)
{
    unsigned char bytes[4];
    if (read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    *out = decode_i32(bytes);
    return 0;
}

static int read_f64(FILE *file, double *out)
{
    unsigned char bytes[8];
    if (read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    *out = decode_f64(bytes);
    return 0;
}

static int valid_f64(double value)
{
    return value == value && value > -1.0e300 && value < 1.0e300;
}

pe_monker_status_t pe_monker_tree_read_header(
    const char *path, pe_monker_tree_header_t *out)
{
    FILE *file;
    int64_t signature;
    int32_t internal_format;
    int32_t player_count;
    int32_t first_to_act;
    int32_t street;
    int i;

    if (!path || !out)
        return PE_MONKER_ERR_NULL_ARGUMENT;

    file = fopen(path, "rb");
    if (!file)
        return PE_MONKER_ERR_OPEN;

    memset(out, 0, sizeof(*out));
    if (read_i64(file, &signature) != 0 ||
        read_i32(file, &internal_format) != 0 ||
        read_i32(file, &player_count) != 0 ||
        read_i32(file, &first_to_act) != 0 ||
        read_i32(file, &street) != 0)
    {
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }

    if (signature < 33486 || signature > 33490)
    {
        fclose(file);
        return PE_MONKER_ERR_BAD_SIGNATURE;
    }
    if (player_count < 1 || player_count > (int32_t)PE_MONKER_MAX_PLAYERS ||
        street < 0 || street > 4 ||
        first_to_act < -1 || first_to_act >= player_count)
    {
        fclose(file);
        return PE_MONKER_ERR_INVALID_HEADER;
    }

    out->signature = signature;
    out->internal_format = internal_format;
    out->player_count = (uint32_t)player_count;
    out->first_to_act = first_to_act;
    out->street = street;

    if (street == 0)
    {
        for (i = 0; i < player_count; ++i)
            if (read_f64(file, &out->committed[i]) != 0 ||
                !valid_f64(out->committed[i]))
            {
                fclose(file);
                return PE_MONKER_ERR_TRUNCATED;
            }
    }
    if (read_f64(file, &out->dead_money) != 0 || !valid_f64(out->dead_money))
    {
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }
    for (i = 0; i < player_count; ++i)
        if (read_f64(file, &out->stacks[i]) != 0 || !valid_f64(out->stacks[i]))
        {
            fclose(file);
            return PE_MONKER_ERR_TRUNCATED;
        }

    fclose(file);
    return PE_MONKER_OK;
}

const char *pe_monker_status_string(pe_monker_status_t status)
{
    switch (status)
    {
    case PE_MONKER_OK: return "ok";
    case PE_MONKER_ERR_NULL_ARGUMENT: return "null argument";
    case PE_MONKER_ERR_OPEN: return "cannot open file";
    case PE_MONKER_ERR_IO: return "I/O error";
    case PE_MONKER_ERR_TRUNCATED: return "truncated header";
    case PE_MONKER_ERR_BAD_SIGNATURE: return "unsupported signature";
    case PE_MONKER_ERR_INVALID_HEADER: return "invalid header";
    default: return "unknown Monker error";
    }
}
