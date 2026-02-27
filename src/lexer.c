#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void add_token(Lexer *lex, TokenType type, const char *val, double num, int line, int col) {
    if (lex->token_count >= lex->token_cap) {
        lex->token_cap *= 2;
        lex->tokens = realloc(lex->tokens, sizeof(Token) * (size_t)lex->token_cap);
    }
    Token *t = &lex->tokens[lex->token_count++];
    t->type = type;
    t->value = val ? strdup(val) : NULL;
    t->num_value = num;
    t->line = line;
    t->col = col;
}

static char peek(Lexer *lex, int offset) {
    size_t p = lex->pos + (size_t)offset;
    if (p < lex->length) return lex->source[p];
    return '\0';
}

static char advance(Lexer *lex) {
    if (lex->pos >= lex->length) return '\0';
    char ch = lex->source[lex->pos++];
    if (ch == '\n') { lex->line++; lex->col = 1; }
    else { lex->col++; }
    return ch;
}

static void skip_whitespace(Lexer *lex) {
    while (lex->pos < lex->length) {
        char c = lex->source[lex->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') advance(lex);
        else break;
    }
}

static void skip_comment(Lexer *lex) {
    /* # comment -- skip to end of line */
    while (lex->pos < lex->length && lex->source[lex->pos] != '\n')
        advance(lex);
}

static void read_number(Lexer *lex) {
    int scol = lex->col;
    int sline = lex->line;
    char buf[64];
    int len = 0;

    while (lex->pos < lex->length && isdigit((unsigned char)lex->source[lex->pos]) && len < 62) {
        buf[len++] = advance(lex);
    }
    if (lex->pos < lex->length && lex->source[lex->pos] == '.' &&
        lex->pos + 1 < lex->length && isdigit((unsigned char)lex->source[lex->pos + 1])) {
        buf[len++] = advance(lex); /* '.' */
        while (lex->pos < lex->length && isdigit((unsigned char)lex->source[lex->pos]) && len < 62) {
            buf[len++] = advance(lex);
        }
    }
    buf[len] = '\0';
    double val = strtod(buf, NULL);
    add_token(lex, TOKEN_NUMBER, buf, val, sline, scol);
}

/* Read string content and handle interpolation.
 * On entry, the opening " has already been consumed. */
static void read_string_inner(Lexer *lex, int sline, int scol) {
    char *buf = malloc(4096);
    int blen = 0;
    int bcap = 4096;
    int has_interp = 0;

#define SBUF_PUSH(c) do { \
    if (blen >= bcap - 1) { bcap *= 2; buf = realloc(buf, (size_t)bcap); } \
    buf[blen++] = (c); \
} while(0)

    while (lex->pos < lex->length && lex->source[lex->pos] != '"') {
        if (lex->source[lex->pos] == '\\') {
            advance(lex);
            if (lex->pos >= lex->length) break;
            char esc = advance(lex);
            switch (esc) {
                case 'n': SBUF_PUSH('\n'); break;
                case 't': SBUF_PUSH('\t'); break;
                case '"': SBUF_PUSH('"'); break;
                case '\\': SBUF_PUSH('\\'); break;
                case '$': SBUF_PUSH('$'); break;
                default: SBUF_PUSH(esc); break;
            }
        } else if (lex->source[lex->pos] == '$' && lex->pos + 1 < lex->length && lex->source[lex->pos + 1] == '{') {
            /* String interpolation */
            if (!has_interp) {
                has_interp = 1;
                add_token(lex, TOKEN_INTERP_BEGIN, NULL, 0, sline, scol);
            }
            /* Emit accumulated string */
            if (blen > 0) {
                buf[blen] = '\0';
                add_token(lex, TOKEN_STRING, buf, 0, lex->line, lex->col);
                blen = 0;
            }
            advance(lex); /* $ */
            advance(lex); /* { */
            /* Collect expression tokens until matching } */
            int depth = 1;
            char *expr = malloc(4096);
            int elen = 0;
            int ecap = 4096;
            while (lex->pos < lex->length && depth > 0) {
                char c = lex->source[lex->pos];
                if (c == '{') depth++;
                else if (c == '}') { depth--; if (depth == 0) { advance(lex); break; } }
                if (depth > 0) {
                    if (elen >= ecap - 1) { ecap *= 2; expr = realloc(expr, (size_t)ecap); }
                    expr[elen++] = advance(lex);
                }
            }
            expr[elen] = '\0';
            /* Tokenize the expression and insert tokens inline */
            Lexer sub;
            lexer_init(&sub, expr);
            lexer_tokenize(&sub);
            for (int i = 0; i < sub.token_count; i++) {
                if (sub.tokens[i].type == TOKEN_EOF) break;
                add_token(lex, sub.tokens[i].type, sub.tokens[i].value, sub.tokens[i].num_value, sline, scol);
            }
            lexer_free(&sub);
            free(expr);
        } else {
            SBUF_PUSH(advance(lex));
        }
    }

    if (lex->pos < lex->length) advance(lex); /* closing " */

    if (has_interp) {
        if (blen > 0) {
            buf[blen] = '\0';
            add_token(lex, TOKEN_STRING, buf, 0, lex->line, lex->col);
        }
        add_token(lex, TOKEN_INTERP_END, NULL, 0, lex->line, lex->col);
    } else {
        buf[blen] = '\0';
        add_token(lex, TOKEN_STRING, buf, 0, sline, scol);
    }
    free(buf);
#undef SBUF_PUSH
}

static TokenType check_keyword(const char *word) {
    /* Standard keywords */
    if (strcmp(word, "let") == 0)      return TOKEN_LET;
    if (strcmp(word, "if") == 0)       return TOKEN_IF;
    if (strcmp(word, "else") == 0)     return TOKEN_ELSE;
    if (strcmp(word, "while") == 0)    return TOKEN_WHILE;
    if (strcmp(word, "for") == 0)      return TOKEN_FOR;
    if (strcmp(word, "in") == 0)       return TOKEN_IN;
    if (strcmp(word, "print") == 0)    return TOKEN_PRINT;
    if (strcmp(word, "fn") == 0)       return TOKEN_FN;
    if (strcmp(word, "return") == 0)   return TOKEN_RETURN;
    if (strcmp(word, "break") == 0)    return TOKEN_BREAK;
    if (strcmp(word, "continue") == 0) return TOKEN_CONTINUE;
    if (strcmp(word, "import") == 0)   return TOKEN_IMPORT;
    if (strcmp(word, "try") == 0)      return TOKEN_TRY;
    if (strcmp(word, "catch") == 0)    return TOKEN_CATCH;
    if (strcmp(word, "throw") == 0)    return TOKEN_THROW;
    if (strcmp(word, "class") == 0)    return TOKEN_CLASS;
    if (strcmp(word, "new") == 0)      return TOKEN_NEW;
    if (strcmp(word, "this") == 0)     return TOKEN_THIS;
    if (strcmp(word, "true") == 0)     return TOKEN_TRUE;
    if (strcmp(word, "false") == 0)    return TOKEN_FALSE;
    if (strcmp(word, "null") == 0)     return TOKEN_NULL;
    if (strcmp(word, "and") == 0)      return TOKEN_AND;
    if (strcmp(word, "or") == 0)       return TOKEN_OR;
    if (strcmp(word, "not") == 0)      return TOKEN_NOT;

    /* Jungian archetypes -- aliases for standard keywords */
    if (strcmp(word, "dream") == 0)          return TOKEN_FN;       /* fn       -- a message from the unconscious */
    if (strcmp(word, "archetype") == 0)      return TOKEN_CLASS;    /* class    -- define a pattern */
    if (strcmp(word, "project") == 0)        return TOKEN_PRINT;    /* print    -- express to the outside */
    if (strcmp(word, "integrate") == 0)      return TOKEN_IMPORT;   /* import   -- absorb external wisdom */
    if (strcmp(word, "manifest") == 0)       return TOKEN_RETURN;   /* return   -- make conscious */
    if (strcmp(word, "unconscious") == 0)    return TOKEN_NULL;     /* null     -- the unknown */
    if (strcmp(word, "Self") == 0)           return TOKEN_THIS;     /* this     -- the unified whole */
    if (strcmp(word, "emerge") == 0)         return TOKEN_NEW;      /* new      -- arise from the unconscious */
    if (strcmp(word, "perceive") == 0)       return TOKEN_LET;      /* let      -- become aware */
    if (strcmp(word, "confront") == 0)       return TOKEN_TRY;      /* try      -- face the shadow */
    if (strcmp(word, "embrace") == 0)        return TOKEN_CATCH;    /* catch    -- accept what you find */
    if (strcmp(word, "reject") == 0)         return TOKEN_THROW;    /* throw    -- cast into the unconscious */
    if (strcmp(word, "individuation") == 0)  return TOKEN_FN;       /* fn       -- the journey of becoming */

    return TOKEN_IDENTIFIER;
}

static void read_identifier(Lexer *lex) {
    int scol = lex->col;
    int sline = lex->line;
    char buf[256];
    int len = 0;
    while (lex->pos < lex->length &&
           (isalnum((unsigned char)lex->source[lex->pos]) || lex->source[lex->pos] == '_') &&
           len < 254) {
        buf[len++] = advance(lex);
    }
    buf[len] = '\0';
    TokenType tt = check_keyword(buf);
    add_token(lex, tt, buf, 0, sline, scol);
}

void lexer_init(Lexer *lex, const char *source) {
    lex->source = source;
    lex->length = strlen(source);
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->token_cap = 256;
    lex->token_count = 0;
    lex->tokens = malloc(sizeof(Token) * (size_t)lex->token_cap);
}

void lexer_tokenize(Lexer *lex) {
    while (1) {
        /* skip whitespace and comments */
        while (1) {
            skip_whitespace(lex);
            if (lex->pos < lex->length && lex->source[lex->pos] == '#') {
                skip_comment(lex);
                continue;
            }
            /* Also support // comments for .jot compatibility */
            if (lex->pos + 1 < lex->length && lex->source[lex->pos] == '/' && lex->source[lex->pos + 1] == '/') {
                skip_comment(lex);
                continue;
            }
            break;
        }

        if (lex->pos >= lex->length) {
            add_token(lex, TOKEN_EOF, NULL, 0, lex->line, lex->col);
            break;
        }

        char ch = lex->source[lex->pos];
        int sline = lex->line;
        int scol = lex->col;

        if (isdigit((unsigned char)ch)) { read_number(lex); continue; }
        if (ch == '"') { advance(lex); read_string_inner(lex, sline, scol); continue; }
        if (isalpha((unsigned char)ch) || ch == '_') { read_identifier(lex); continue; }

        /* two-char operators */
        if (ch == '=') {
            advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_EQ, "==", 0, sline, scol); }
            else add_token(lex, TOKEN_ASSIGN, "=", 0, sline, scol);
            continue;
        }
        if (ch == '!') {
            advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_NEQ, "!=", 0, sline, scol); }
            else { fprintf(stderr, "Line %d:%d - Unexpected character: '!'\n", sline, scol); exit(1); }
            continue;
        }
        if (ch == '>') {
            advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_GTE, ">=", 0, sline, scol); }
            else add_token(lex, TOKEN_GT, ">", 0, sline, scol);
            continue;
        }
        if (ch == '<') {
            advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_LTE, "<=", 0, sline, scol); }
            else add_token(lex, TOKEN_LT, "<", 0, sline, scol);
            continue;
        }
        if (ch == '+') {
            advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_PLUS_ASSIGN, "+=", 0, sline, scol); }
            else add_token(lex, TOKEN_PLUS, "+", 0, sline, scol);
            continue;
        }
        if (ch == '-') {
            advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_MINUS_ASSIGN, "-=", 0, sline, scol); }
            else add_token(lex, TOKEN_MINUS, "-", 0, sline, scol);
            continue;
        }
        if (ch == '*') {
            advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_MULTIPLY_ASSIGN, "*=", 0, sline, scol); }
            else add_token(lex, TOKEN_MULTIPLY, "*", 0, sline, scol);
            continue;
        }
        if (ch == '/') {
            advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_DIVIDE_ASSIGN, "/=", 0, sline, scol); }
            else add_token(lex, TOKEN_DIVIDE, "/", 0, sline, scol);
            continue;
        }

        /* single-char tokens */
        advance(lex);
        switch (ch) {
            case '%': add_token(lex, TOKEN_MODULO, "%", 0, sline, scol); break;
            case '(': add_token(lex, TOKEN_LPAREN, "(", 0, sline, scol); break;
            case ')': add_token(lex, TOKEN_RPAREN, ")", 0, sline, scol); break;
            case '{': add_token(lex, TOKEN_LBRACE, "{", 0, sline, scol); break;
            case '}': add_token(lex, TOKEN_RBRACE, "}", 0, sline, scol); break;
            case '[': add_token(lex, TOKEN_LBRACKET, "[", 0, sline, scol); break;
            case ']': add_token(lex, TOKEN_RBRACKET, "]", 0, sline, scol); break;
            case ';': add_token(lex, TOKEN_SEMICOLON, ";", 0, sline, scol); break;
            case ',': add_token(lex, TOKEN_COMMA, ",", 0, sline, scol); break;
            case ':': add_token(lex, TOKEN_COLON, ":", 0, sline, scol); break;
            case '.': add_token(lex, TOKEN_DOT, ".", 0, sline, scol); break;
            case '?': add_token(lex, TOKEN_QUESTION, "?", 0, sline, scol); break;
            default:
                fprintf(stderr, "Line %d:%d - Unexpected character: '%c'\n", sline, scol, ch);
                exit(1);
        }
    }
}

void lexer_free(Lexer *lex) {
    for (int i = 0; i < lex->token_count; i++) {
        free(lex->tokens[i].value);
    }
    free(lex->tokens);
    lex->tokens = NULL;
    lex->token_count = 0;
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_TRUE: return "TRUE";
        case TOKEN_FALSE: return "FALSE";
        case TOKEN_NULL: return "NULL";
        case TOKEN_LET: return "LET";
        case TOKEN_IF: return "IF";
        case TOKEN_ELSE: return "ELSE";
        case TOKEN_WHILE: return "WHILE";
        case TOKEN_FOR: return "FOR";
        case TOKEN_IN: return "IN";
        case TOKEN_PRINT: return "PRINT";
        case TOKEN_FN: return "FN";
        case TOKEN_RETURN: return "RETURN";
        case TOKEN_BREAK: return "BREAK";
        case TOKEN_CONTINUE: return "CONTINUE";
        case TOKEN_IMPORT: return "IMPORT";
        case TOKEN_TRY: return "TRY";
        case TOKEN_CATCH: return "CATCH";
        case TOKEN_THROW: return "THROW";
        case TOKEN_CLASS: return "CLASS";
        case TOKEN_NEW: return "NEW";
        case TOKEN_THIS: return "THIS";
        case TOKEN_AND: return "AND";
        case TOKEN_OR: return "OR";
        case TOKEN_NOT: return "NOT";
        case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";
        case TOKEN_MULTIPLY: return "MULTIPLY";
        case TOKEN_DIVIDE: return "DIVIDE";
        case TOKEN_MODULO: return "MODULO";
        case TOKEN_ASSIGN: return "ASSIGN";
        case TOKEN_PLUS_ASSIGN: return "PLUS_ASSIGN";
        case TOKEN_MINUS_ASSIGN: return "MINUS_ASSIGN";
        case TOKEN_MULTIPLY_ASSIGN: return "MULTIPLY_ASSIGN";
        case TOKEN_DIVIDE_ASSIGN: return "DIVIDE_ASSIGN";
        case TOKEN_EQ: return "EQ";
        case TOKEN_NEQ: return "NEQ";
        case TOKEN_GT: return "GT";
        case TOKEN_LT: return "LT";
        case TOKEN_GTE: return "GTE";
        case TOKEN_LTE: return "LTE";
        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_LBRACE: return "LBRACE";
        case TOKEN_RBRACE: return "RBRACE";
        case TOKEN_LBRACKET: return "LBRACKET";
        case TOKEN_RBRACKET: return "RBRACKET";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_COLON: return "COLON";
        case TOKEN_DOT: return "DOT";
        case TOKEN_QUESTION: return "QUESTION";
        case TOKEN_INTERP_BEGIN: return "INTERP_BEGIN";
        case TOKEN_INTERP_END: return "INTERP_END";
        case TOKEN_EOF: return "EOF";
    }
    return "UNKNOWN";
}
