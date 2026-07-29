#include "jsmn.h"

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,
                                    jsmntok_t *tokens,
                                    unsigned int num_tokens)
{
    if (parser->toknext >= num_tokens)
        return NULL;
    jsmntok_t *tok = &tokens[parser->toknext++];
    tok->start = -1;
    tok->end = -1;
    tok->size = 0;
#ifdef JSMN_PARENT_LINKS
    tok->parent = -1;
#endif
    return tok;
}

static void jsmn_fill_token(jsmntok_t *token,
                            jsmntype_t type,
                            int start,
                            int end)
{
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser,
                                const char *js,
                                size_t len,
                                jsmntok_t *tokens,
                                unsigned int num_tokens)
{
    int start = (int)parser->pos;
    for (; parser->pos < len; parser->pos++)
    {
        char c = js[parser->pos];
        switch (c)
        {
        case ':':
        case '\t':
        case '\r':
        case '\n':
        case '\f':
        case ' ':
        case ',':
        case ']':
        case '}':
            goto found;
        default:
            if ((unsigned char)c < 32)
            {
                parser->pos = (unsigned int)start;
                return JSMN_ERROR_INVAL;
            }
        }
    }
found:
    if (tokens == NULL)
    {
        parser->pos--;
        return 0;
    }
    jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
    if (tok == NULL)
    {
        parser->pos = (unsigned int)start;
        return JSMN_ERROR_NOMEM;
    }
    jsmn_fill_token(tok, JSMN_PRIMITIVE, start, (int)parser->pos);
#ifdef JSMN_PARENT_LINKS
    tok->parent = parser->toksuper;
#endif
    parser->pos--;
    return 0;
}

static int jsmn_parse_string(jsmn_parser *parser,
                             const char *js,
                             size_t len,
                             jsmntok_t *tokens,
                             unsigned int num_tokens)
{
    int start = (int)parser->pos;

    parser->pos++;
    for (; parser->pos < len; parser->pos++)
    {
        char c = js[parser->pos];

        if (c == '"')
        {
            if (tokens == NULL)
                return 0;
            jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
            if (tok == NULL)
            {
                parser->pos = (unsigned int)start;
                return JSMN_ERROR_NOMEM;
            }
            jsmn_fill_token(tok, JSMN_STRING, start + 1, (int)parser->pos);
#ifdef JSMN_PARENT_LINKS
            tok->parent = parser->toksuper;
#endif
            return 0;
        }

        if (c == '\\' && parser->pos + 1 < len)
        {
            parser->pos++;
            switch (js[parser->pos])
            {
            case '\"':
            case '/':
            case '\\':
            case 'b':
            case 'f':
            case 'r':
            case 'n':
            case 't':
            case 'u':
                break;
            default:
                parser->pos = (unsigned int)start;
                return JSMN_ERROR_INVAL;
            }
        }
    }
    parser->pos = (unsigned int)start;
    return JSMN_ERROR_PART;
}

void jsmn_init(jsmn_parser *parser)
{
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser,
               const char *js,
               size_t len,
               jsmntok_t *tokens,
               unsigned int num_tokens)
{
    int r;
    for (; parser->pos < len; parser->pos++)
    {
        char c = js[parser->pos];
        jsmntok_t *tok;
        int i;

        switch (c)
        {
        case '{':
        case '[':
            parser->toknext++;
            if (parser->toknext > num_tokens)
                return JSMN_ERROR_NOMEM;
            if (tokens == NULL)
                break;
            tok = &tokens[parser->toknext - 1];
            tok->type = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
            tok->start = (int)parser->pos;
            tok->end = -1;
            tok->size = 0;
            if (parser->toksuper != -1)
            {
                tokens[parser->toksuper].size++;
#ifdef JSMN_PARENT_LINKS
                tok->parent = parser->toksuper;
#endif
            }
            parser->toksuper = (int)(parser->toknext - 1);
            break;
        case '}':
        case ']':
            if (tokens == NULL)
                break;
            for (i = (int)parser->toknext - 1; i >= 0; i--)
            {
                tok = &tokens[i];
                if (tok->start != -1 && tok->end == -1)
                {
                    if (tok->type != ((c == '}') ? JSMN_OBJECT : JSMN_ARRAY))
                        return JSMN_ERROR_INVAL;
                    tok->end = (int)parser->pos + 1;
                    parser->toksuper = tok->parent;
                    break;
                }
            }
            if (i < 0)
                return JSMN_ERROR_INVAL;
            break;
        case '"':
            r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
            if (r < 0)
                return r;
            if (parser->toksuper != -1 && tokens != NULL)
                tokens[parser->toksuper].size++;
            break;
        case '\t':
        case '\r':
        case '\n':
        case '\f':
        case ' ':
        case ',':
        case ':':
            break;
        default:
            r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
            if (r < 0)
                return r;
            if (parser->toksuper != -1 && tokens != NULL)
                tokens[parser->toksuper].size++;
            break;
        }
    }

    for (int i = (int)parser->toknext - 1; i >= 0; i--)
    {
        jsmntok_t *tok = &tokens[i];
        if (tok->start != -1 && tok->end == -1)
            return JSMN_ERROR_PART;
    }

    return (int)parser->toknext;
}
