/*
 * monker_filters.c - MonkerSolver node-filter grammar (MKR-06)
 */

#include <poker_eval/solver/pe_monker.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define PE_MONKER_FILTER_MAX_DEPTH 256u

static const char *const filter_keywords[] = {
    "street", "player", "pot", "spr", "invested", "committed",
    "callers", "action", "numtoact", "players", "bets", "prebets",
    "flopbets", "turnbets", "riverbets", "open", "ip", "oop", "agg",
    "ipagg", "oopagg", "pre", "flop", "turn", "river", "call", "raise",
    "fold", "check", "allin", "limit", "rake", "dead", "stack", "board",
    "prevstreet"
};

typedef struct
{
    const char *expression;
    size_t length;
    size_t position;
    size_t depth;
    size_t max_depth;
    pe_monker_filter_t *filter;
} filter_parser_t;

static int is_keyword_char(char character)
{
    return isalnum((unsigned char)character) || character == '_';
}

static int keyword_is_known(const char *keyword)
{
    size_t index;
    for (index = 0u;
         index < sizeof(filter_keywords) / sizeof(filter_keywords[0]); ++index)
        if (strcmp(keyword, filter_keywords[index]) == 0)
            return 1;
    return 0;
}

static void atom_free(pe_monker_filter_atom_t *atom)
{
    if (atom == NULL)
        return;
    free(atom->keyword);
    free(atom->value);
    memset(atom, 0, sizeof(*atom));
}

void pe_monker_filter_free(pe_monker_filter_t *filter)
{
    size_t index;
    if (filter == NULL)
        return;
    for (index = 0u; index < filter->atom_count; ++index)
        atom_free(&filter->atoms[index]);
    free(filter->atoms);
    memset(filter, 0, sizeof(*filter));
}

static pe_monker_filter_status_t append_atom(
    filter_parser_t *parser, const char *keyword, size_t keyword_length,
    unsigned negated, unsigned previous,
    pe_monker_filter_operator_t operator, const char *value,
    size_t value_length)
{
    pe_monker_filter_atom_t *grown;
    pe_monker_filter_atom_t *atom;
    char *keyword_copy;
    char *value_copy = NULL;

    keyword_copy = (char *)malloc(keyword_length + 1u);
    if (keyword_copy == NULL)
        return PE_MONKER_FILTER_ERR_NO_MEMORY;
    memcpy(keyword_copy, keyword, keyword_length);
    keyword_copy[keyword_length] = '\0';
    if (value_length != 0u) {
        value_copy = (char *)malloc(value_length + 1u);
        if (value_copy == NULL) {
            free(keyword_copy);
            return PE_MONKER_FILTER_ERR_NO_MEMORY;
        }
        memcpy(value_copy, value, value_length);
        value_copy[value_length] = '\0';
    }
    grown = (pe_monker_filter_atom_t *)realloc(
        parser->filter->atoms,
        (parser->filter->atom_count + 1u) * sizeof(*parser->filter->atoms));
    if (grown == NULL) {
        free(keyword_copy);
        free(value_copy);
        return PE_MONKER_FILTER_ERR_NO_MEMORY;
    }
    parser->filter->atoms = grown;
    atom = &grown[parser->filter->atom_count++];
    atom->keyword = keyword_copy;
    atom->value = value_copy;
    atom->operator = operator;
    atom->negated = negated;
    atom->previous = previous;
    return PE_MONKER_FILTER_OK;
}

static pe_monker_filter_status_t parse_or(filter_parser_t *parser);

static pe_monker_filter_status_t parse_atom(filter_parser_t *parser)
{
    size_t keyword_start;
    size_t keyword_length;
    size_t value_start;
    size_t value_length = 0u;
    unsigned negated = 0u;
    unsigned previous = 0u;
    pe_monker_filter_operator_t operator = PE_MONKER_FILTER_OP_NONE;
    char character;

    if (parser->position >= parser->length)
        return PE_MONKER_FILTER_ERR_SYNTAX;
    if (parser->expression[parser->position] == '!') {
        negated = 1u;
        ++parser->position;
    }
    if (parser->position < parser->length &&
        parser->expression[parser->position] == '^') {
        previous = 1u;
        ++parser->position;
    }
    if (parser->position >= parser->length)
        return PE_MONKER_FILTER_ERR_SYNTAX;
    if (parser->expression[parser->position] == '(') {
        if (previous != 0u)
            return PE_MONKER_FILTER_ERR_SYNTAX;
        ++parser->position;
        if (++parser->depth > PE_MONKER_FILTER_MAX_DEPTH)
            return PE_MONKER_FILTER_ERR_TOO_DEEP;
        if (parser->depth > parser->max_depth)
            parser->max_depth = parser->depth;
        if (parser->position >= parser->length ||
            parser->expression[parser->position] == ')')
            return PE_MONKER_FILTER_ERR_SYNTAX;
        {
            pe_monker_filter_status_t status = parse_or(parser);
            if (status != PE_MONKER_FILTER_OK)
                return status;
        }
        if (parser->position >= parser->length ||
            parser->expression[parser->position] != ')')
            return PE_MONKER_FILTER_ERR_SYNTAX;
        ++parser->position;
        --parser->depth;
        return PE_MONKER_FILTER_OK;
    }

    keyword_start = parser->position;
    while (parser->position < parser->length &&
           is_keyword_char(parser->expression[parser->position]))
        ++parser->position;
    keyword_length = parser->position - keyword_start;
    if (keyword_length == 0u)
        return PE_MONKER_FILTER_ERR_SYNTAX;
    {
        char *keyword = (char *)malloc(keyword_length + 1u);
        int known;
        if (keyword == NULL)
            return PE_MONKER_FILTER_ERR_NO_MEMORY;
        memcpy(keyword, parser->expression + keyword_start, keyword_length);
        keyword[keyword_length] = '\0';
        known = keyword_is_known(keyword);
        free(keyword);
        if (!known)
            return PE_MONKER_FILTER_ERR_UNKNOWN_KEYWORD;
    }
    value_start = parser->position;
    if (keyword_length == strlen("prevstreet") &&
        strncmp(parser->expression + keyword_start, "prevstreet",
                strlen("prevstreet")) == 0) {
        previous = 1u;
        keyword_length = strlen("street");
    }
    if (parser->position < parser->length) {
        character = parser->expression[parser->position];
        if (character == '>' || character == '<' || character == '=') {
            if (character == '>') {
                operator = PE_MONKER_FILTER_OP_GT;
                ++parser->position;
                if (parser->position < parser->length &&
                    parser->expression[parser->position] == '=') {
                    operator = PE_MONKER_FILTER_OP_GE;
                    ++parser->position;
                }
            } else if (character == '<') {
                operator = PE_MONKER_FILTER_OP_LT;
                ++parser->position;
                if (parser->position < parser->length &&
                    parser->expression[parser->position] == '=') {
                    operator = PE_MONKER_FILTER_OP_LE;
                    ++parser->position;
                }
            } else {
                operator = PE_MONKER_FILTER_OP_EQ;
                ++parser->position;
            }
            value_start = parser->position;
            while (parser->position < parser->length &&
                   parser->expression[parser->position] != ',' &&
                   parser->expression[parser->position] != ':' &&
                   parser->expression[parser->position] != ')') {
                if (isspace((unsigned char)parser->expression[parser->position]) ||
                    parser->expression[parser->position] == '!' ||
                    parser->expression[parser->position] == '^' ||
                    parser->expression[parser->position] == '>' ||
                    parser->expression[parser->position] == '<' ||
                    parser->expression[parser->position] == '=')
                    return PE_MONKER_FILTER_ERR_BAD_VALUE;
                ++parser->position;
            }
            value_length = parser->position - value_start;
            if (value_length == 0u)
                return PE_MONKER_FILTER_ERR_BAD_VALUE;
        }
    }
    if (keyword_length == strlen("street") &&
        strncmp(parser->expression + keyword_start, "prevstreet",
                strlen("prevstreet")) == 0)
        return append_atom(parser, "street", strlen("street"), negated,
                           previous, operator,
                           parser->expression + value_start, value_length);
    return append_atom(parser, parser->expression + keyword_start, keyword_length,
                       negated, previous, operator,
                       value_length == 0u ? NULL :
                           parser->expression + value_start, value_length);
}

static pe_monker_filter_status_t parse_and(filter_parser_t *parser)
{
    pe_monker_filter_status_t status = parse_atom(parser);
    if (status != PE_MONKER_FILTER_OK)
        return status;
    while (parser->position < parser->length &&
           parser->expression[parser->position] == ':') {
        ++parser->position;
        if (parser->position == parser->length ||
            parser->expression[parser->position] == ')' ||
            parser->expression[parser->position] == ',')
            break;
        status = parse_atom(parser);
        if (status != PE_MONKER_FILTER_OK)
            return status;
    }
    return PE_MONKER_FILTER_OK;
}

static pe_monker_filter_status_t parse_or(filter_parser_t *parser)
{
    pe_monker_filter_status_t status = parse_and(parser);
    if (status != PE_MONKER_FILTER_OK)
        return status;
    while (parser->position < parser->length &&
           parser->expression[parser->position] == ',') {
        ++parser->position;
        if (parser->position == parser->length ||
            parser->expression[parser->position] == ')')
            break;
        status = parse_and(parser);
        if (status != PE_MONKER_FILTER_OK)
            return status;
    }
    return PE_MONKER_FILTER_OK;
}

pe_monker_filter_status_t pe_monker_filter_parse(
    const char *expression, pe_monker_filter_t *out)
{
    filter_parser_t parser;
    pe_monker_filter_status_t status;

    if (expression == NULL || out == NULL)
        return PE_MONKER_FILTER_ERR_NULL_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (*expression == '\0')
        return PE_MONKER_FILTER_ERR_EMPTY;
    parser.expression = expression;
    parser.length = strlen(expression);
    parser.position = 0u;
    parser.depth = 0u;
    parser.max_depth = 0u;
    parser.filter = out;
    status = parse_or(&parser);
    if (status == PE_MONKER_FILTER_OK && parser.position != parser.length)
        status = PE_MONKER_FILTER_ERR_SYNTAX;
    if (status == PE_MONKER_FILTER_OK)
        out->max_depth = parser.max_depth;
    else
        pe_monker_filter_free(out);
    return status;
}

const char *pe_monker_filter_status_string(pe_monker_filter_status_t status)
{
    switch (status) {
    case PE_MONKER_FILTER_OK: return "ok";
    case PE_MONKER_FILTER_ERR_NULL_ARGUMENT: return "null argument";
    case PE_MONKER_FILTER_ERR_EMPTY: return "empty filter";
    case PE_MONKER_FILTER_ERR_SYNTAX: return "filter syntax error";
    case PE_MONKER_FILTER_ERR_UNKNOWN_KEYWORD: return "unknown filter keyword";
    case PE_MONKER_FILTER_ERR_BAD_VALUE: return "invalid filter value";
    case PE_MONKER_FILTER_ERR_NO_MEMORY: return "out of memory";
    case PE_MONKER_FILTER_ERR_TOO_DEEP: return "filter nesting too deep";
    default: return "unknown filter status";
    }
}
