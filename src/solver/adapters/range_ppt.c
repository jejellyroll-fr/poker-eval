/*
 * range_ppt.c - small, exact PPT matcher for four-card Omaha ranges (MKR-07)
 */

#include <poker_eval/solver/pe_monker.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PE_MONKER_PPT_MAX_EXPRESSION (1024u * 1024u)

typedef struct
{
    int rank;
    int suit;
} ppt_token_t;

typedef struct
{
    ppt_token_t tokens[4];
    unsigned token_count;
    int wildcard;
} ppt_term_t;

typedef enum
{
    PPT_NODE_TERM,
    PPT_NODE_OR,
    PPT_NODE_AND,
    PPT_NODE_DIFF
} ppt_node_kind_t;

typedef struct ppt_node_t ppt_node_t;

struct ppt_node_t
{
    ppt_node_kind_t kind;
    ppt_term_t term;
    ppt_node_t *left;
    ppt_node_t *right;
};

typedef struct
{
    const char *text;
    size_t length;
    size_t position;
} ppt_parser_t;

static int is_rank_character(char character)
{
    return character == '2' || character == '3' || character == '4' ||
           character == '5' || character == '6' || character == '7' ||
           character == '8' || character == '9' || character == 'T' ||
           character == 'J' || character == 'Q' || character == 'K' ||
           character == 'A' || character == 'r';
}

static int rank_value(char character)
{
    if (character >= '2' && character <= '9')
        return character - '2';
    if (character == 'T') return 8;
    if (character == 'J') return 9;
    if (character == 'Q') return 10;
    if (character == 'K') return 11;
    if (character == 'A') return 12;
    return -2; /* the repeated rank variable r */
}

static int is_suit_character(char character)
{
    return character == 's' || character == 'h' || character == 'd' ||
           character == 'c' || character == 'x' || character == 'y' ||
           character == 'z';
}

static int suit_value(char character)
{
    if (character == 's') return 0;
    if (character == 'h') return 1;
    if (character == 'd') return 2;
    if (character == 'c') return 3;
    return -(character - 'x' + 2); /* x/y/z -> -2/-3/-4 */
}

static void ppt_node_free(ppt_node_t *node)
{
    if (node == NULL)
        return;
    ppt_node_free(node->left);
    ppt_node_free(node->right);
    free(node);
}

static ppt_node_t *ppt_node_new(ppt_node_kind_t kind,
                                ppt_node_t *left, ppt_node_t *right)
{
    ppt_node_t *node = (ppt_node_t *)calloc(1u, sizeof(*node));
    if (node != NULL) {
        node->kind = kind;
        node->left = left;
        node->right = right;
    }
    return node;
}

static pe_monker_ppt_status_t parse_term(ppt_parser_t *parser,
                                        ppt_term_t *out)
{
    size_t start = parser->position;
    size_t end;
    size_t index;
    int suited = 0;

    memset(out, 0, sizeof(*out));
    if (parser->position < parser->length &&
        parser->text[parser->position] == '*') {
        out->wildcard = 1;
        ++parser->position;
        return PE_MONKER_PPT_OK;
    }
    while (parser->position < parser->length &&
           parser->text[parser->position] != ',' &&
           parser->text[parser->position] != ':' &&
           parser->text[parser->position] != '!' &&
           parser->text[parser->position] != '(' &&
           parser->text[parser->position] != ')' &&
           parser->text[parser->position] != '+')
        ++parser->position;
    end = parser->position;
    if (end == start)
        return PE_MONKER_PPT_ERR_SYNTAX;
    if (parser->position < parser->length && parser->text[parser->position] == '+') {
        ++parser->position;
        if (parser->position < parser->length &&
            parser->text[parser->position] != ',' &&
            parser->text[parser->position] != ':' &&
            parser->text[parser->position] != ')' &&
            parser->text[parser->position] != '!')
            return PE_MONKER_PPT_ERR_SYNTAX;
    }
    if (end - start >= 3u && parser->text[end - 1u] == 's') {
        suited = 1;
        --end;
    }
    if (end - start == 0u)
        return PE_MONKER_PPT_ERR_SYNTAX;
    if (end - start > 4u)
        return PE_MONKER_PPT_ERR_TOO_MANY_CARDS;
    if (suited) {
        for (index = start; index < end; ++index) {
            if (!is_rank_character(parser->text[index]) ||
                parser->text[index] == 'r')
                return PE_MONKER_PPT_ERR_SYNTAX;
            out->tokens[out->token_count].rank = rank_value(parser->text[index]);
            out->tokens[out->token_count].suit = -2;
            ++out->token_count;
        }
        return PE_MONKER_PPT_OK;
    }
    for (index = start; index < end; ++index) {
        char character = parser->text[index];
        if (is_rank_character(character)) {
            out->tokens[out->token_count].rank = rank_value(character);
            out->tokens[out->token_count].suit = -1;
        } else if (is_suit_character(character)) {
            out->tokens[out->token_count].rank = -1;
            out->tokens[out->token_count].suit = suit_value(character);
        } else {
            return PE_MONKER_PPT_ERR_SYNTAX;
        }
        ++out->token_count;
    }
    return PE_MONKER_PPT_OK;
}

static pe_monker_ppt_status_t parse_or(ppt_parser_t *parser, ppt_node_t **out);

static pe_monker_ppt_status_t parse_primary(ppt_parser_t *parser,
                                            ppt_node_t **out)
{
    ppt_node_t *node;
    ppt_term_t term;
    pe_monker_ppt_status_t status;

    if (parser->position < parser->length &&
        parser->text[parser->position] == '(') {
        ++parser->position;
        status = parse_or(parser, out);
        if (status != PE_MONKER_PPT_OK)
            return status;
        if (parser->position >= parser->length ||
            parser->text[parser->position] != ')') {
            ppt_node_free(*out);
            *out = NULL;
            return PE_MONKER_PPT_ERR_SYNTAX;
        }
        ++parser->position;
        return PE_MONKER_PPT_OK;
    }
    status = parse_term(parser, &term);
    if (status != PE_MONKER_PPT_OK)
        return status;
    node = ppt_node_new(PPT_NODE_TERM, NULL, NULL);
    if (node == NULL)
        return PE_MONKER_PPT_ERR_NO_MEMORY;
    node->term = term;
    *out = node;
    return PE_MONKER_PPT_OK;
}

static pe_monker_ppt_status_t parse_difference(ppt_parser_t *parser,
                                               ppt_node_t **out)
{
    ppt_node_t *left = NULL;
    ppt_node_t *right = NULL;
    ppt_node_t *node;
    int leading_difference = 0;
    pe_monker_ppt_status_t status;

    if (parser->position < parser->length && parser->text[parser->position] == '!') {
        ++parser->position;
        leading_difference = 1;
    } else {
        status = parse_primary(parser, &left);
        if (status != PE_MONKER_PPT_OK)
            return status;
    }
    if (leading_difference ||
        (parser->position < parser->length && parser->text[parser->position] == '!')) {
        if (!leading_difference)
            ++parser->position;
        status = parse_primary(parser, &right);
        if (status != PE_MONKER_PPT_OK) {
            ppt_node_free(left);
            return status;
        }
        node = ppt_node_new(PPT_NODE_DIFF, left, right);
        if (node == NULL) {
            ppt_node_free(left);
            ppt_node_free(right);
            return PE_MONKER_PPT_ERR_NO_MEMORY;
        }
        *out = node;
    } else {
        *out = left;
        return PE_MONKER_PPT_OK;
    }
    while (parser->position < parser->length && parser->text[parser->position] == '!') {
        ++parser->position;
        status = parse_primary(parser, &right);
        if (status != PE_MONKER_PPT_OK) {
            ppt_node_free(node);
            return status;
        }
        node = ppt_node_new(PPT_NODE_DIFF, node, right);
        if (node == NULL) {
            ppt_node_free(right);
            return PE_MONKER_PPT_ERR_NO_MEMORY;
        }
        *out = node;
    }
    return PE_MONKER_PPT_OK;
}

static pe_monker_ppt_status_t parse_and(ppt_parser_t *parser, ppt_node_t **out)
{
    ppt_node_t *node;
    pe_monker_ppt_status_t status = parse_difference(parser, &node);
    if (status != PE_MONKER_PPT_OK)
        return status;
    while (parser->position < parser->length && parser->text[parser->position] == ':') {
        ppt_node_t *right;
        ++parser->position;
        status = parse_difference(parser, &right);
        if (status != PE_MONKER_PPT_OK) {
            ppt_node_free(node);
            return status;
        }
        node = ppt_node_new(PPT_NODE_AND, node, right);
        if (node == NULL) {
            ppt_node_free(right);
            return PE_MONKER_PPT_ERR_NO_MEMORY;
        }
    }
    *out = node;
    return PE_MONKER_PPT_OK;
}

static pe_monker_ppt_status_t parse_or(ppt_parser_t *parser, ppt_node_t **out)
{
    ppt_node_t *node;
    pe_monker_ppt_status_t status = parse_and(parser, &node);
    if (status != PE_MONKER_PPT_OK)
        return status;
    while (parser->position < parser->length && parser->text[parser->position] == ',') {
        ppt_node_t *right;
        ++parser->position;
        status = parse_and(parser, &right);
        if (status != PE_MONKER_PPT_OK) {
            ppt_node_free(node);
            return status;
        }
        node = ppt_node_new(PPT_NODE_OR, node, right);
        if (node == NULL) {
            ppt_node_free(right);
            return PE_MONKER_PPT_ERR_NO_MEMORY;
        }
    }
    *out = node;
    return PE_MONKER_PPT_OK;
}

static int token_matches(const ppt_token_t *token, int rank, int suit,
                         int *rank_variable, int *suit_variables)
{
    int variable;
    if (token->rank >= 0 && token->rank != rank)
        return 0;
    if (token->rank == -2) {
        if (*rank_variable >= 0 && *rank_variable != rank)
            return 0;
        *rank_variable = rank;
    }
    if (token->suit >= 0 && token->suit != suit)
        return 0;
    if (token->suit <= -2) {
        variable = -token->suit - 2;
        if (suit_variables[variable] >= 0 && suit_variables[variable] != suit)
            return 0;
        if (suit_variables[variable] < 0) {
            int other;
            for (other = 0; other < 3; ++other)
                if (other != variable && suit_variables[other] == suit)
                    return 0;
            suit_variables[variable] = suit;
        }
    }
    return 1;
}

static int match_tokens(const ppt_term_t *term, unsigned token_index,
                        const int *ranks, const int *suits,
                        int *used, int *rank_variable, int *suit_variables)
{
    unsigned card;
    if (token_index == term->token_count)
        return 1;
    for (card = 0u; card < 4u; ++card) {
        int next_rank_variable;
        int next_suit_variables[3];
        if (used[card] != 0)
            continue;
        next_rank_variable = *rank_variable;
        memcpy(next_suit_variables, suit_variables,
               sizeof(next_suit_variables));
        if (!token_matches(&term->tokens[token_index], ranks[card], suits[card],
                           &next_rank_variable, next_suit_variables))
            continue;
        used[card] = 1;
        if (match_tokens(term, token_index + 1u, ranks, suits, used,
                         &next_rank_variable, next_suit_variables))
            return 1;
        used[card] = 0;
    }
    return 0;
}

static int term_matches(const ppt_term_t *term, const int *ranks,
                        const int *suits)
{
    int used[4] = {0, 0, 0, 0};
    int suit_variables[3] = {-1, -1, -1};
    int rank_variable = -1;
    if (term->wildcard != 0)
        return 1;
    return match_tokens(term, 0u, ranks, suits, used, &rank_variable,
                        suit_variables);
}

static int node_matches(const ppt_node_t *node, const int *ranks,
                        const int *suits)
{
    if (node == NULL)
        return 0;
    switch (node->kind) {
    case PPT_NODE_TERM:
        return term_matches(&node->term, ranks, suits);
    case PPT_NODE_OR:
        return node_matches(node->left, ranks, suits) ||
               node_matches(node->right, ranks, suits);
    case PPT_NODE_AND:
        return node_matches(node->left, ranks, suits) &&
               node_matches(node->right, ranks, suits);
    case PPT_NODE_DIFF:
        return node_matches(node->left, ranks, suits) &&
               !node_matches(node->right, ranks, suits);
    default:
        return 0;
    }
}

pe_monker_ppt_status_t pe_monker_ppt_count(const char *expression,
                                           uint64_t *out_count)
{
    ppt_parser_t parser;
    ppt_node_t *root = NULL;
    pe_monker_ppt_status_t status;
    unsigned a;
    uint64_t count = 0u;

    if (expression == NULL || out_count == NULL)
        return PE_MONKER_PPT_ERR_NULL_ARGUMENT;
    if (*expression == '\0')
        return PE_MONKER_PPT_ERR_EMPTY;
    parser.text = expression;
    parser.length = strnlen(expression, PE_MONKER_PPT_MAX_EXPRESSION);
    parser.position = 0u;
    status = parse_or(&parser, &root);
    if (status == PE_MONKER_PPT_OK && parser.position != parser.length)
        status = PE_MONKER_PPT_ERR_SYNTAX;
    if (status != PE_MONKER_PPT_OK) {
        ppt_node_free(root);
        return status;
    }
    for (a = 0u; a < 49u; ++a) {
        unsigned b;
        for (b = a + 1u; b < 50u; ++b) {
            unsigned c;
            for (c = b + 1u; c < 51u; ++c) {
                unsigned d;
                for (d = c + 1u; d < 52u; ++d) {
                    int ranks[4] = {(int)(a / 4u), (int)(b / 4u),
                                    (int)(c / 4u), (int)(d / 4u)};
                    int suits[4] = {(int)(a % 4u), (int)(b % 4u),
                                    (int)(c % 4u), (int)(d % 4u)};
                    if (node_matches(root, ranks, suits))
                        ++count;
                }
            }
        }
    }
    ppt_node_free(root);
    *out_count = count;
    return PE_MONKER_PPT_OK;
}

const char *pe_monker_ppt_status_string(pe_monker_ppt_status_t status)
{
    switch (status) {
    case PE_MONKER_PPT_OK: return "ok";
    case PE_MONKER_PPT_ERR_NULL_ARGUMENT: return "null argument";
    case PE_MONKER_PPT_ERR_EMPTY: return "empty PPT expression";
    case PE_MONKER_PPT_ERR_SYNTAX: return "PPT syntax error";
    case PE_MONKER_PPT_ERR_TOO_MANY_CARDS: return "PPT hand has too many cards";
    case PE_MONKER_PPT_ERR_NO_MEMORY: return "out of memory";
    default: return "unknown PPT status";
    }
}
