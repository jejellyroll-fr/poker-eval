#include <poker_eval/core/safe_format.h>

#include <stdio.h>
#include <string.h>

typedef struct
{
    char *out;
    size_t capacity;
    size_t length;
} pe_safe_writer_t;

static void writer_append(pe_safe_writer_t *writer, const char *text,
                          size_t length)
{
    size_t original_length;
    size_t available;

    if (writer == NULL || text == NULL || length == 0u)
        return;
    original_length = length;
    if (writer->out == NULL || writer->capacity == 0u)
    {
        writer->length += length;
        return;
    }
    if (writer->length >= writer->capacity - 1u)
    {
        writer->length += original_length;
        writer->out[writer->capacity - 1u] = '\0';
        return;
    }
    available = writer->capacity - 1u - writer->length;
    if (length > available)
        length = available;
    memcpy(writer->out + writer->length, text, length);
    writer->length += original_length;
    if (writer->length >= writer->capacity)
        writer->length = writer->capacity - 1u;
    writer->out[writer->length] = '\0';
}

static void writer_append_text(pe_safe_writer_t *writer, const char *text)
{
    if (text != NULL)
        writer_append(writer, text, strlen(text));
}

static void writer_append_int(pe_safe_writer_t *writer, int value)
{
    char piece[128];
    int written;

    written = snprintf(piece, sizeof(piece), "%d", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_long(pe_safe_writer_t *writer, long value)
{
    char piece[128];
    int written = snprintf(piece, sizeof(piece), "%ld", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_long_long(pe_safe_writer_t *writer, long long value)
{
    char piece[128];
    int written = snprintf(piece, sizeof(piece), "%lld", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_unsigned(pe_safe_writer_t *writer,
                                   unsigned int value)
{
    char piece[128];
    int written = snprintf(piece, sizeof(piece), "%u", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_unsigned_long(pe_safe_writer_t *writer,
                                        unsigned long value)
{
    char piece[128];
    int written = snprintf(piece, sizeof(piece), "%lu", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_unsigned_long_long(pe_safe_writer_t *writer,
                                             unsigned long long value)
{
    char piece[128];
    int written = snprintf(piece, sizeof(piece), "%llu", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_size(pe_safe_writer_t *writer, size_t value)
{
    char piece[128];
    int written = snprintf(piece, sizeof(piece), "%zu", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_hex(pe_safe_writer_t *writer, unsigned int value,
                              int zero_pad)
{
    char piece[128];
    int written;
    if (zero_pad)
        written = snprintf(piece, sizeof(piece), "%04x", value);
    else
        written = snprintf(piece, sizeof(piece), "%x", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_hex_long(pe_safe_writer_t *writer,
                                   unsigned long value)
{
    char piece[128];
    int written = snprintf(piece, sizeof(piece), "%lx", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_hex_long_long(pe_safe_writer_t *writer,
                                        unsigned long long value)
{
    char piece[128];
    int written = snprintf(piece, sizeof(piece), "%llx", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static void writer_append_double(pe_safe_writer_t *writer, double value,
                                 char conversion, int precision)
{
    char piece[128];
    int written;

    if (conversion == 'a')
        written = snprintf(piece, sizeof(piece), "%a", value);
    else if (conversion == 'f' && precision == 2)
        written = snprintf(piece, sizeof(piece), "%.2f", value);
    else if (conversion == 'f' && precision == 3)
        written = snprintf(piece, sizeof(piece), "%.3f", value);
    else if (conversion == 'f' && precision == 4)
        written = snprintf(piece, sizeof(piece), "%.4f", value);
    else if (conversion == 'f' && precision == 6)
        written = snprintf(piece, sizeof(piece), "%.6f", value);
    else if (conversion == 'g' && precision == 17)
        written = snprintf(piece, sizeof(piece), "%.17g", value);
    else if (conversion == 'g')
        written = snprintf(piece, sizeof(piece), "%g", value);
    else
        written = snprintf(piece, sizeof(piece), "%f", value);
    if (written > 0)
        writer_append(writer, piece, (size_t)written);
}

static int parse_number(const char **cursor)
{
    int value = 0;

    while (**cursor >= '0' && **cursor <= '9')
    {
        value = value * 10 + (int)(**cursor - '0');
        ++*cursor;
    }
    return value;
}

size_t pe_safe_vformat(char *out, size_t capacity, const char *format,
                       va_list args)
{
    pe_safe_writer_t writer;
    const char *cursor;

    if (format == NULL)
        return 0u;
    writer.out = out;
    writer.capacity = capacity;
    writer.length = 0u;
    if (out != NULL && capacity > 0u)
        out[0] = '\0';

    cursor = format;
    while (*cursor != '\0')
    {
        const char *conversion_start;
        int precision = -1;
        int width = 0;
        int zero_pad = 0;
        int long_count = 0;
        char conversion;

        if (*cursor != '%')
        {
            const char *text_start = cursor;
            while (*cursor != '\0' && *cursor != '%')
                ++cursor;
            writer_append(&writer, text_start,
                          (size_t)(cursor - text_start));
            continue;
        }

        conversion_start = cursor++;
        if (*cursor == '%')
        {
            writer_append(&writer, "%", 1u);
            ++cursor;
            continue;
        }
        while (*cursor == '-' || *cursor == '+' || *cursor == ' ' ||
               *cursor == '#' || *cursor == '0')
        {
            if (*cursor == '0')
                zero_pad = 1;
            ++cursor;
        }
        width = parse_number(&cursor);
        if (*cursor == '.')
        {
            ++cursor;
            precision = parse_number(&cursor);
        }
        if (*cursor == 'l')
        {
            ++long_count;
            ++cursor;
            if (*cursor == 'l')
            {
                ++long_count;
                ++cursor;
            }
        }
        else if (*cursor == 'z')
        {
            long_count = 3;
            ++cursor;
        }
        conversion = *cursor;
        if (conversion != '\0')
            ++cursor;

        switch (conversion)
        {
        case 's':
            writer_append_text(&writer, va_arg(args, const char *));
            break;
        case 'c':
        {
            char piece = (char)va_arg(args, int);
            writer_append(&writer, &piece, 1u);
            break;
        }
        case 'd':
        case 'i':
            if (long_count == 1)
                writer_append_long(&writer, va_arg(args, long));
            else if (long_count == 2)
                writer_append_long_long(&writer, va_arg(args, long long));
            else
                writer_append_int(&writer, va_arg(args, int));
            break;
        case 'u':
            if (long_count == 1)
                writer_append_unsigned_long(&writer,
                                            va_arg(args, unsigned long));
            else if (long_count == 2)
                writer_append_unsigned_long_long(
                    &writer, va_arg(args, unsigned long long));
            else if (long_count == 3)
                writer_append_size(&writer, va_arg(args, size_t));
            else
                writer_append_unsigned(&writer, va_arg(args, unsigned int));
            break;
        case 'x':
            if (long_count == 2)
                writer_append_hex_long_long(&writer,
                                            va_arg(args, unsigned long long));
            else if (long_count == 1)
                writer_append_hex_long(&writer, va_arg(args, unsigned long));
            else
                writer_append_hex(&writer, va_arg(args, unsigned int),
                                  zero_pad && width == 4);
            break;
        case 'f':
            writer_append_double(&writer, va_arg(args, double), 'f', precision);
            break;
        case 'g':
            writer_append_double(&writer, va_arg(args, double), 'g', precision);
            break;
        case 'a':
            writer_append_double(&writer, va_arg(args, double), 'a', precision);
            break;
        default:
            /* Keep unsupported input visible without interpreting it. */
            writer_append(&writer, conversion_start,
                          (size_t)(cursor - conversion_start));
            break;
        }
    }
    return writer.length;
}
