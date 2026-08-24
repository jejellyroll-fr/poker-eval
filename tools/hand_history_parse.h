#ifndef PE_HAND_HISTORY_PARSE_H
#define PE_HAND_HISTORY_PARSE_H

/*
 * Text-parsing helpers shared by the hand-history importer and its tests.
 * Header-only so the unit tests can exercise the exact functions the CLI
 * uses without launching a subprocess.
 */

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Copy the hand id following '#' (PokerStars "Hand #123:") up to the first
 * whitespace or ':'; the trailing separator is not part of the id.
 */
static void hh_extract_hand_id(const char *line, char *out, size_t out_size)
{
    const char *hash;
    const char *p;
    size_t n = 0u;
    if (out == NULL || out_size == 0u) return;
    out[0] = '\0';
    if (line == NULL) return;
    hash = strchr(line, '#');
    if (hash == NULL) return;
    p = hash + 1;
    while (*p != '\0' && !isspace((unsigned char)*p) && *p != ':' &&
           n + 1u < out_size) {
        out[n++] = *p++;
    }
    out[n] = '\0';
}

/*
 * Parse the last monetary amount in the text so that "raises $2 to $3"
 * yields 3. A numeric run starts at a digit whose predecessor is neither a
 * digit nor '.', so multi-digit and decimal amounts parse in full:
 * "bets $10" yields 10, "$0.50" yields 0.5.
 */
static int hh_parse_amount(const char *text, double *amount)
{
    const char *p = text;
    const char *start = NULL;
    char *end;
    if (text == NULL || amount == NULL) return 0;
    while (*p != '\0') {
        if (isdigit((unsigned char)*p)) {
            const char prev = (p == text) ? '\0' : *(p - 1);
            if (!isdigit((unsigned char)prev) && prev != '.') {
                start = p;
            }
        }
        ++p;
    }
    if (start == NULL) {
        return 0;
    }
    errno = 0;
    *amount = strtod(start, &end);
    return end != start && errno == 0;
}

/* Strip trailing ':' separators, e.g. the "PlayerB:" prefix of action lines. */
static void hh_strip_trailing_colon(char *s)
{
    size_t len;
    if (s == NULL) return;
    len = strlen(s);
    while (len > 0u && s[len - 1u] == ':') {
        s[--len] = '\0';
    }
}

#endif /* PE_HAND_HISTORY_PARSE_H */
