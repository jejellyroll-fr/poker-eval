/* pe-runout-report - emit an exact conditional Hold'em runout report. */

#include <poker_eval/solver/pe_runout_report.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    FILE *out;
    int first;
} report_writer_t;

static int card_value(char rank, char suit)
{
    const char *ranks = "23456789TJQKA";
    const char *suits = "cdhs";
    const char *r = strchr(ranks, rank);
    const char *s = strchr(suits, suit);
    return r && s ? (int)(r - ranks) + 13 * (int)(s - suits) : -1;
}

static int parse_cards(const char *text, mask_t *out)
{
    size_t length;
    mask_t cards = MASK_EMPTY;
    if (!text || !out) return -1;
    length = strnlen(text, 1024u);
    for (size_t i = 0u; i < length; )
    {
        int card;
        while (i < length && (text[i] == ',' || text[i] == ' ' ||
                              text[i] == '\t')) ++i;
        if (i + 1u >= length) return -1;
        card = card_value(text[i], text[i + 1u]);
        if (card < 0 || mask_is_set(cards, card)) return -1;
        cards = mask_set(cards, card);
        i += 2u;
    }
    *out = cards;
    return 0;
}

static void board_text(mask_t board, char out[16])
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "cdhs";
    size_t at = 0u;
    for (int card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(board, card))
        {
            out[at++] = ranks[card % 13];
            out[at++] = suits[card / 13];
        }
    out[at] = '\0';
}

static int write_row(const pe_runout_report_row_t *row, void *user)
{
    report_writer_t *writer = user;
    char board[16];
    board_text(row->board, board);
    if (!writer->first) fputc(',', writer->out);
    writer->first = 0;
    fprintf(writer->out, "{\"board\":\"%s\",\"probability\":%.17g}",
            board, row->probability);
    return ferror(writer->out) ? -1 : 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --board CARDS [--dead CARDS] [--json FILE]\n"
                    "Cards use pairs such as AsKd7c; dead cards are optional.\n",
            program);
}

int main(int argc, char **argv)
{
    const char *board_text_arg = NULL;
    const char *dead_text_arg = NULL;
    const char *json_path = NULL;
    mask_t board = MASK_EMPTY;
    mask_t dead = MASK_EMPTY;
    FILE *out = stdout;
    report_writer_t writer;
    size_t count = 0u;
    double probability_sum = 0.0;
    int rc;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--board") == 0 && i + 1 < argc)
            board_text_arg = argv[++i];
        else if (strcmp(argv[i], "--dead") == 0 && i + 1 < argc)
            dead_text_arg = argv[++i];
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc)
            json_path = argv[++i];
        else
        {
            usage(argv[0]);
            return 2;
        }
    }
    if (!board_text_arg || parse_cards(board_text_arg, &board) != 0 ||
        (dead_text_arg && parse_cards(dead_text_arg, &dead) != 0) ||
        (board & dead) != 0)
    {
        usage(argv[0]);
        return 2;
    }
    if (json_path)
    {
        out = fopen(json_path, "w");
        if (!out) return 1;
    }
    fprintf(out, "{\"schema\":\"pe-runout-report/v1\",\"runouts\":[");
    writer.out = out;
    writer.first = 1;
    rc = pe_holdem_runout_report_generate(
        board, dead, write_row, &writer, &count, &probability_sum);
    if (rc != 0)
    {
        if (json_path) fclose(out);
        return 1;
    }
    fprintf(out, "],\"count\":%zu,\"probability_sum\":%.17g}\n",
            count, probability_sum);
    rc = ferror(out) ? 1 : 0;
    if (json_path) fclose(out);
    return rc;
}
