/* pe_work_unit.c - distributed public-state work unit (DIST-02). */

#include <poker_eval/solver/pe_work_unit.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_hex[] = "0123456789abcdef";

void pe_work_unit_init(pe_work_unit_t *unit)
{
    if (unit != NULL)
        memset(unit, 0, sizeof(*unit));
}

void pe_work_unit_destroy(pe_work_unit_t *unit)
{
    if (unit == NULL)
        return;
    free(unit->boards);
    free(unit->ranges);
    free(unit->regret_snapshot);
    pe_work_unit_init(unit);
}

int pe_work_unit_validate(const pe_work_unit_t *unit)
{
    size_t i;

    if (unit == NULL || unit->iteration_begin > unit->iteration_end ||
        (unit->board_count != 0u && unit->board_width == 0u) ||
        (unit->board_count != 0u && unit->boards == NULL) ||
        (unit->ranges_size != 0u && unit->ranges == NULL) ||
        (unit->regret_count != 0u && unit->regret_snapshot == NULL))
        return -1;
    if (unit->board_count > SIZE_MAX / (size_t)unit->board_width)
        return -1;
    for (i = 0u; i < unit->regret_count; ++i)
        if (!isfinite(unit->regret_snapshot[i]))
            return -1;
    return 0;
}

static void append_char(char *out, size_t capacity, size_t *position, char value)
{
    if (out != NULL && *position + 1u < capacity)
        out[*position] = value;
    (*position)++;
}

static void append_text(char *out, size_t capacity, size_t *position,
                        const char *text)
{
    while (*text != '\0')
        append_char(out, capacity, position, *text++);
}

static void append_bytes(char *out, size_t capacity, size_t *position,
                         const uint8_t *bytes, size_t count)
{
    size_t i;
    if (count == 0u)
    {
        append_char(out, capacity, position, '-');
        return;
    }
    for (i = 0u; i < count; ++i)
    {
        append_char(out, capacity, position, k_hex[bytes[i] >> 4]);
        append_char(out, capacity, position, k_hex[bytes[i] & 0x0Fu]);
    }
}

static void append_u64_hex(char *out, size_t capacity, size_t *position,
                           uint64_t value)
{
    int shift;
    for (shift = 60; shift >= 0; shift -= 4)
        append_char(out, capacity, position,
                    k_hex[(value >> shift) & UINT64_C(0xF)]);
}

size_t pe_work_unit_to_string(const pe_work_unit_t *unit,
                              char *out, size_t capacity)
{
    size_t position = 0u;
    size_t i;
    size_t board_bytes;

    if ((out == NULL && capacity != 0u) || pe_work_unit_validate(unit) != 0)
        return 0u;
    board_bytes = unit->board_count * (size_t)unit->board_width;
    append_text(out, capacity, &position, "PE_WORK_V1;state=0x");
    append_u64_hex(out, capacity, &position, unit->public_state);
    {
        char number[256];
        int written = snprintf(number, sizeof(number),
                               ";player=%u;iteration_begin=%llu;iteration_end=%llu;board_width=%u;boards=",
                               (unsigned)unit->player,
                               (unsigned long long)unit->iteration_begin,
                               (unsigned long long)unit->iteration_end,
                               (unsigned)unit->board_width);
        if (written < 0 || (size_t)written >= sizeof(number))
            return 0u;
        append_text(out, capacity, &position, number);
    }
    append_bytes(out, capacity, &position, unit->boards, board_bytes);
    append_text(out, capacity, &position, ";ranges=");
    append_bytes(out, capacity, &position, unit->ranges, unit->ranges_size);
    append_text(out, capacity, &position, ";regrets=");
    if (unit->regret_count == 0u)
        append_char(out, capacity, &position, '-');
    else
        for (i = 0u; i < unit->regret_count; ++i)
        {
            uint64_t bits;
            memcpy(&bits, &unit->regret_snapshot[i], sizeof(bits));
            if (i != 0u)
                append_char(out, capacity, &position, ',');
            append_u64_hex(out, capacity, &position, bits);
        }
    if (out != NULL && capacity != 0u)
        out[position < capacity ? position : capacity - 1u] = '\0';
    return position;
}

static int token_is(const char *token, size_t length, const char *expected)
{
    return strlen(expected) == length && memcmp(token, expected, length) == 0;
}

static int parse_number(const char *value, size_t length,
                        unsigned long long *out, int base)
{
    char text[96];
    char *end;

    if (length == 0u || length >= sizeof(text) || out == NULL ||
        value[0] == '-')
        return -1;
    memcpy(text, value, length);
    text[length] = '\0';
    errno = 0;
    *out = strtoull(text, &end, base);
    return errno == 0 && end != text && *end == '\0' ? 0 : -1;
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int decode_bytes(const char *value, size_t length,
                        uint8_t **out, size_t *out_size)
{
    size_t i;
    uint8_t *bytes;

    if (out == NULL || out_size == NULL)
        return -1;
    *out = NULL;
    *out_size = 0u;
    if (length == 1u && value[0] == '-')
        return 0;
    if (length == 0u || (length & 1u) != 0u ||
        length / 2u > SIZE_MAX / sizeof(uint8_t))
        return -1;
    bytes = (uint8_t *)malloc(length / 2u);
    if (bytes == NULL)
        return -1;
    for (i = 0u; i < length / 2u; ++i)
    {
        int high = hex_digit(value[i * 2u]);
        int low = hex_digit(value[i * 2u + 1u]);
        if (high < 0 || low < 0)
        {
            free(bytes);
            return -1;
        }
        bytes[i] = (uint8_t)((high << 4) | low);
    }
    *out = bytes;
    *out_size = length / 2u;
    return 0;
}

static int decode_regrets(const char *value, size_t length,
                          double **out, size_t *out_count)
{
    size_t count = 1u;
    size_t i;
    double *regrets;

    if (out == NULL || out_count == NULL)
        return -1;
    *out = NULL;
    *out_count = 0u;
    if (length == 1u && value[0] == '-')
        return 0;
    if (length == 0u)
        return -1;
    for (i = 0u; i < length; ++i)
        if (value[i] == ',')
            count++;
    if (count > SIZE_MAX / sizeof(*regrets))
        return -1;
    regrets = (double *)malloc(count * sizeof(*regrets));
    if (regrets == NULL)
        return -1;
    {
        size_t start = 0u;
        for (i = 0u; i < count; ++i)
        {
            size_t end = start;
            uint64_t bits = 0u;
            size_t digit;
            while (end < length && value[end] != ',')
                end++;
            if (end - start != 16u)
            {
                free(regrets);
                return -1;
            }
            for (digit = start; digit < end; ++digit)
            {
                int nibble = hex_digit(value[digit]);
                if (nibble < 0)
                {
                    free(regrets);
                    return -1;
                }
                bits = (bits << 4) | (uint64_t)nibble;
            }
            memcpy(&regrets[i], &bits, sizeof(bits));
            start = end + 1u;
        }
    }
    *out = regrets;
    *out_count = count;
    return 0;
}

int pe_work_unit_from_string(const char *text, pe_work_unit_t *out)
{
    pe_work_unit_t parsed;
    const char *cursor;
    unsigned seen = 0u;
    unsigned long long number;

    if (text == NULL || out == NULL ||
        strncmp(text, "PE_WORK_V1;", 11u) != 0)
        return -1;
    pe_work_unit_init(&parsed);
    cursor = text + 11u;
    while (*cursor != '\0')
    {
        const char *end = strchr(cursor, ';');
        const char *equals = memchr(cursor, '=',
                                    end == NULL ? strlen(cursor) :
                                                   (size_t)(end - cursor));
        size_t token_length = end == NULL ? strlen(cursor) :
                                           (size_t)(end - cursor);
        size_t key_length;
        const char *value;
        size_t value_length;

        if (equals == NULL || equals == cursor)
            goto fail;
        key_length = (size_t)(equals - cursor);
        value = equals + 1;
        value_length = token_length - key_length - 1u;
        if (token_is(cursor, key_length, "state"))
        {
            if ((seen & (1u << 0)) != 0u ||
                parse_number(value, value_length, &number, 0) != 0)
                goto fail;
            parsed.public_state = (uint64_t)number;
            seen |= 1u << 0;
        }
        else if (token_is(cursor, key_length, "player") ||
                 token_is(cursor, key_length, "board_width"))
        {
            unsigned bit = token_is(cursor, key_length, "player") ? 1u : 4u;
            if ((seen & (1u << bit)) != 0u ||
                parse_number(value, value_length, &number, 10) != 0 ||
                (bit == 1u && number > UINT8_MAX) ||
                (bit == 4u && number > UINT8_MAX))
                goto fail;
            if (bit == 1u)
                parsed.player = (uint8_t)number;
            else
                parsed.board_width = (uint8_t)number;
            seen |= 1u << bit;
        }
        else if (token_is(cursor, key_length, "iteration_begin") ||
                 token_is(cursor, key_length, "iteration_end"))
        {
            unsigned bit = token_is(cursor, key_length, "iteration_begin") ? 2u : 3u;
            if ((seen & (1u << bit)) != 0u ||
                parse_number(value, value_length, &number, 10) != 0)
                goto fail;
            if (bit == 2u)
                parsed.iteration_begin = (uint64_t)number;
            else
                parsed.iteration_end = (uint64_t)number;
            seen |= 1u << bit;
        }
        else if (token_is(cursor, key_length, "boards"))
        {
            size_t bytes;
            if ((seen & (1u << 5)) != 0u ||
                decode_bytes(value, value_length, &parsed.boards, &bytes) != 0)
                goto fail;
            parsed.board_count = bytes;
            seen |= 1u << 5;
        }
        else if (token_is(cursor, key_length, "ranges"))
        {
            if ((seen & (1u << 6)) != 0u ||
                decode_bytes(value, value_length, &parsed.ranges,
                             &parsed.ranges_size) != 0)
                goto fail;
            seen |= 1u << 6;
        }
        else if (token_is(cursor, key_length, "regrets"))
        {
            if ((seen & (1u << 7)) != 0u ||
                decode_regrets(value, value_length,
                               &parsed.regret_snapshot,
                               &parsed.regret_count) != 0)
                goto fail;
            seen |= 1u << 7;
        }
        cursor = end == NULL ? cursor + token_length : end + 1;
    }
    if (seen != 0xFFu)
        goto fail;
    if (parsed.board_width == 0u)
    {
        if (parsed.board_count != 0u)
            goto fail;
    }
    else if (parsed.board_count % parsed.board_width != 0u)
        goto fail;
    else
        parsed.board_count /= parsed.board_width;
    if (pe_work_unit_validate(&parsed) != 0)
        goto fail;
    pe_work_unit_destroy(out);
    *out = parsed;
    return 0;

fail:
    pe_work_unit_destroy(&parsed);
    return -1;
}
