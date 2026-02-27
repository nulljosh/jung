#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void lexer_error(Lexer *lex, const char *msg) {
    fprintf(stderr, "Lexer error at line %d, col %d: %s\n", lex->line, lex->col, msg);
    exit(1);
}

static char peek(Lexer *lex, int offset) {
    int p = lex->pos + offset;
    if (p < 0 || lex->source[p] == '\0') return '\0';
    return lex->source[p];
}

static char advance(Lexer *lex) {
    char c = lex->source[lex->pos];
    if (c == '\0') return '\0';
    lex->pos++;
    if (c == '\n') { lex->line++; lex->col = 1; }
    else { lex->col++; }
    return c;
}

static void add_token(Lexer *lex, TokenType type, const char *value, int line, int col) {
    if (lex->count >= lex->capacity) {
        lex->capacity *= 2;
        lex->tokens = realloc(lex->tokens, lex->capacity * sizeof(Token));
    }
    Token *t = &lex->tokens[lex->count++];
    t->type = type;
    t->value = value ? strdup(value) : NULL;
    t->line = line;
    t->col = col;
}

static void skip_whitespace(Lexer *lex) {
    while (peek(lex, 0) && strchr(" \t\n\r", peek(lex, 0))) {
        advance(lex);
    }
}

static void skip_comment(Lexer *lex) {
    while (peek(lex, 0) && peek(lex, 0) != '\n') {
        advance(lex);
    }
}

static TokenType check_keyword(const char *word) {
    static const struct { const char *kw; TokenType type; } keywords[] = {
        {"let",      TOKEN_LET},
        {"if",       TOKEN_IF},
        {"else",     TOKEN_ELSE},
        {"while",    TOKEN_WHILE},
        {"for",      TOKEN_FOR},
        {"in",       TOKEN_IN},
        {"print",    TOKEN_PRINT},
        {"true",     TOKEN_TRUE},
        {"false",    TOKEN_FALSE},
        {"null",     TOKEN_NULL},
        {"and",      TOKEN_AND},
        {"or",       TOKEN_OR},
        {"not",      TOKEN_NOT},
        {"fn",       TOKEN_FN},
        {"return",   TOKEN_RETURN},
        {"break",    TOKEN_BREAK},
        {"continue", TOKEN_CONTINUE},
        {"import",   TOKEN_IMPORT},
        {"try",      TOKEN_TRY},
        {"catch",    TOKEN_CATCH},
        {"throw",    TOKEN_THROW},
        {"class",    TOKEN_CLASS},
        {"new",      TOKEN_NEW},
        {"this",     TOKEN_THIS},
        /* Jung keyword aliases */
        {"dream",         TOKEN_FN},
        {"project",       TOKEN_PRINT},
        {"manifest",      TOKEN_RETURN},
        {"unconscious",   TOKEN_NULL},
        {"integrate",     TOKEN_IMPORT},
        {"archetype",     TOKEN_CLASS},
        {"Self",          TOKEN_THIS},
        {"repress",       TOKEN_IDENTIFIER},  /* phase 2: delete */
        {"individuation", TOKEN_IDENTIFIER},  /* phase 2: main entry */
        {"shadow",        TOKEN_IDENTIFIER},  /* phase 2: private */
        {"persona",       TOKEN_IDENTIFIER},  /* phase 2: public */
        {"anima",         TOKEN_IDENTIFIER},  /* phase 2: async */
        {"animus",        TOKEN_IDENTIFIER},  /* phase 2: await */
        {"collective",    TOKEN_IDENTIFIER},  /* phase 2: static */
        {"complex",       TOKEN_CLASS},       /* alias for struct/class */
        {NULL, TOKEN_EOF}
    };
    for (int i = 0; keywords[i].kw; i++) {
        if (strcmp(word, keywords[i].kw) == 0) return keywords[i].type;
    }
    return TOKEN_IDENTIFIER;
}

static void read_number(Lexer *lex) {
    int start_col = lex->col;
    int start_line = lex->line;
    int len = 0, cap = 32;
    char *buf = malloc(cap);

    while (isdigit((unsigned char)peek(lex, 0))) {
        if (len >= cap - 1) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
    }
    if (peek(lex, 0) == '.' && isdigit((unsigned char)peek(lex, 1))) {
        buf[len++] = advance(lex); /* dot */
        while (isdigit((unsigned char)peek(lex, 0))) {
            if (len >= cap - 1) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = advance(lex);
        }
    }
    buf[len] = '\0';
    add_token(lex, TOKEN_NUMBER, buf, start_line, start_col);
    free(buf);
}

static void read_identifier(Lexer *lex) {
    int start_col = lex->col;
    int start_line = lex->line;
    int len = 0, cap = 64;
    char *buf = malloc(cap);

    while (isalnum((unsigned char)peek(lex, 0)) || peek(lex, 0) == '_') {
        if (len >= cap - 1) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
    }
    buf[len] = '\0';
    TokenType type = check_keyword(buf);
    add_token(lex, type, buf, start_line, start_col);
    free(buf);
}

/*
 * String reading with interpolation support.
 * "text ${expr} more" produces:
 *   INTERP_BEGIN, STRING("text "), <tokens for expr>, STRING(" more"), INTERP_END
 * Simple strings (no ${}) produce a single STRING token.
 */
static void read_string(Lexer *lex) {
    int start_col = lex->col;
    int start_line = lex->line;
    advance(lex); /* skip opening quote */

    int len = 0, cap = 128;
    char *buf = malloc(cap);
    int has_interp = 0;

    /* First pass: detect if there's interpolation */
    int save_pos = lex->pos, save_line = lex->line, save_col = lex->col;
    while (peek(lex, 0) && peek(lex, 0) != '"') {
        if (peek(lex, 0) == '\\') {
            advance(lex);
            if (peek(lex, 0)) advance(lex);
        } else if (peek(lex, 0) == '$' && peek(lex, 1) == '{') {
            has_interp = 1;
            break;
        } else {
            advance(lex);
        }
    }
    /* Restore position */
    lex->pos = save_pos;
    lex->line = save_line;
    lex->col = save_col;

    if (!has_interp) {
        /* Simple string */
        while (peek(lex, 0) && peek(lex, 0) != '"') {
            if (peek(lex, 0) == '\\') {
                advance(lex);
                char esc = peek(lex, 0);
                if (esc == 'n') { buf[len++] = '\n'; advance(lex); }
                else if (esc == 't') { buf[len++] = '\t'; advance(lex); }
                else if (esc == '"') { buf[len++] = '"'; advance(lex); }
                else if (esc == '\\') { buf[len++] = '\\'; advance(lex); }
                else if (esc == '$') { buf[len++] = '$'; advance(lex); }
                else { buf[len++] = advance(lex); }
            } else {
                if (len >= cap - 1) { cap *= 2; buf = realloc(buf, cap); }
                buf[len++] = advance(lex);
            }
            if (len >= cap - 1) { cap *= 2; buf = realloc(buf, cap); }
        }
        if (!peek(lex, 0)) lexer_error(lex, "Unterminated string");
        advance(lex); /* skip closing quote */
        buf[len] = '\0';
        add_token(lex, TOKEN_STRING, buf, start_line, start_col);
        free(buf);
        return;
    }

    /* Interpolated string */
    add_token(lex, TOKEN_INTERP_BEGIN, NULL, start_line, start_col);

    while (peek(lex, 0) && peek(lex, 0) != '"') {
        if (peek(lex, 0) == '\\') {
            advance(lex);
            char esc = peek(lex, 0);
            if (len >= cap - 1) { cap *= 2; buf = realloc(buf, cap); }
            if (esc == 'n') { buf[len++] = '\n'; advance(lex); }
            else if (esc == 't') { buf[len++] = '\t'; advance(lex); }
            else if (esc == '"') { buf[len++] = '"'; advance(lex); }
            else if (esc == '\\') { buf[len++] = '\\'; advance(lex); }
            else if (esc == '$') { buf[len++] = '$'; advance(lex); }
            else { buf[len++] = advance(lex); }
        } else if (peek(lex, 0) == '$' && peek(lex, 1) == '{') {
            /* Flush accumulated text as a STRING token */
            buf[len] = '\0';
            if (len > 0) {
                add_token(lex, TOKEN_STRING, buf, lex->line, lex->col);
            }
            len = 0;

            advance(lex); /* skip $ */
            advance(lex); /* skip { */

            /* Now lex the expression inside ${...} normally until matching } */
            int depth = 1;
            while (peek(lex, 0) && depth > 0) {
                skip_whitespace(lex);
                if (!peek(lex, 0) || depth == 0) break;

                char c = peek(lex, 0);

                if (c == '}') {
                    depth--;
                    if (depth == 0) {
                        advance(lex); /* skip closing } */
                        break;
                    }
                    add_token(lex, TOKEN_RBRACE, "}", lex->line, lex->col);
                    advance(lex);
                    continue;
                }
                if (c == '{') {
                    depth++;
                    add_token(lex, TOKEN_LBRACE, "{", lex->line, lex->col);
                    advance(lex);
                    continue;
                }

                /* Lex a single token inside the interpolation */
                if (isdigit((unsigned char)c)) {
                    read_number(lex);
                } else if (isalpha((unsigned char)c) || c == '_') {
                    read_identifier(lex);
                } else if (c == '"') {
                    read_string(lex); /* nested string */
                } else if (c == '+') {
                    int col = lex->col; advance(lex);
                    if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_PLUS_ASSIGN, "+=", lex->line, col); }
                    else add_token(lex, TOKEN_PLUS, "+", lex->line, col);
                } else if (c == '-') {
                    int col = lex->col; advance(lex);
                    if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_MINUS_ASSIGN, "-=", lex->line, col); }
                    else add_token(lex, TOKEN_MINUS, "-", lex->line, col);
                } else if (c == '*') {
                    int col = lex->col; advance(lex);
                    if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_MULTIPLY_ASSIGN, "*=", lex->line, col); }
                    else add_token(lex, TOKEN_MULTIPLY, "*", lex->line, col);
                } else if (c == '/') {
                    int col = lex->col; advance(lex);
                    if (peek(lex, 0) == '/') { skip_comment(lex); }
                    else if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_DIVIDE_ASSIGN, "/=", lex->line, col); }
                    else add_token(lex, TOKEN_DIVIDE, "/", lex->line, col);
                } else if (c == '%') { add_token(lex, TOKEN_MODULO, "%", lex->line, lex->col); advance(lex); }
                else if (c == '(') { add_token(lex, TOKEN_LPAREN, "(", lex->line, lex->col); advance(lex); }
                else if (c == ')') { add_token(lex, TOKEN_RPAREN, ")", lex->line, lex->col); advance(lex); }
                else if (c == '[') { add_token(lex, TOKEN_LBRACKET, "[", lex->line, lex->col); advance(lex); }
                else if (c == ']') { add_token(lex, TOKEN_RBRACKET, "]", lex->line, lex->col); advance(lex); }
                else if (c == ',') { add_token(lex, TOKEN_COMMA, ",", lex->line, lex->col); advance(lex); }
                else if (c == ':') { add_token(lex, TOKEN_COLON, ":", lex->line, lex->col); advance(lex); }
                else if (c == '.') { add_token(lex, TOKEN_DOT, ".", lex->line, lex->col); advance(lex); }
                else if (c == ';') { add_token(lex, TOKEN_SEMICOLON, ";", lex->line, lex->col); advance(lex); }
                else if (c == '?') { add_token(lex, TOKEN_QUESTION, "?", lex->line, lex->col); advance(lex); }
                else if (c == '=') {
                    int col = lex->col; advance(lex);
                    if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_EQ, "==", lex->line, col); }
                    else add_token(lex, TOKEN_ASSIGN, "=", lex->line, col);
                } else if (c == '!') {
                    int col = lex->col; advance(lex);
                    if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_NEQ, "!=", lex->line, col); }
                    else lexer_error(lex, "Unexpected '!'");
                } else if (c == '>') {
                    int col = lex->col; advance(lex);
                    if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_GTE, ">=", lex->line, col); }
                    else add_token(lex, TOKEN_GT, ">", lex->line, col);
                } else if (c == '<') {
                    int col = lex->col; advance(lex);
                    if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_LTE, "<=", lex->line, col); }
                    else add_token(lex, TOKEN_LT, "<", lex->line, col);
                } else {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Unexpected character in interpolation: '%c'", c);
                    lexer_error(lex, msg);
                }
            }
        } else {
            if (len >= cap - 1) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = advance(lex);
        }
    }

    /* Flush any remaining text */
    buf[len] = '\0';
    if (len > 0) {
        add_token(lex, TOKEN_STRING, buf, lex->line, lex->col);
    }

    if (!peek(lex, 0)) lexer_error(lex, "Unterminated interpolated string");
    advance(lex); /* skip closing quote */

    add_token(lex, TOKEN_INTERP_END, NULL, lex->line, lex->col);
    free(buf);
}

void lexer_init(Lexer *lex, const char *source) {
    lex->source = source;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->capacity = 256;
    lex->count = 0;
    lex->tokens = malloc(lex->capacity * sizeof(Token));
}

void lexer_tokenize(Lexer *lex) {
    while (1) {
        /* Skip whitespace and comments */
        while (1) {
            skip_whitespace(lex);
            if (peek(lex, 0) == '/' && peek(lex, 1) == '/') {
                skip_comment(lex);
                continue;
            }
            break;
        }

        if (!peek(lex, 0)) {
            add_token(lex, TOKEN_EOF, NULL, lex->line, lex->col);
            return;
        }

        char c = peek(lex, 0);

        if (isdigit((unsigned char)c)) { read_number(lex); continue; }
        if (c == '"') { read_string(lex); continue; }
        if (isalpha((unsigned char)c) || c == '_') { read_identifier(lex); continue; }

        if (c == '=') {
            int col = lex->col; advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_EQ, "==", lex->line, col); }
            else add_token(lex, TOKEN_ASSIGN, "=", lex->line, col);
            continue;
        }
        if (c == '!') {
            int col = lex->col; advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_NEQ, "!=", lex->line, col); }
            else lexer_error(lex, "Unexpected character: '!'");
            continue;
        }
        if (c == '>') {
            int col = lex->col; advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_GTE, ">=", lex->line, col); }
            else add_token(lex, TOKEN_GT, ">", lex->line, col);
            continue;
        }
        if (c == '<') {
            int col = lex->col; advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_LTE, "<=", lex->line, col); }
            else add_token(lex, TOKEN_LT, "<", lex->line, col);
            continue;
        }
        if (c == '+') {
            int col = lex->col; advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_PLUS_ASSIGN, "+=", lex->line, col); }
            else add_token(lex, TOKEN_PLUS, "+", lex->line, col);
            continue;
        }
        if (c == '-') {
            int col = lex->col; advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_MINUS_ASSIGN, "-=", lex->line, col); }
            else add_token(lex, TOKEN_MINUS, "-", lex->line, col);
            continue;
        }
        if (c == '*') {
            int col = lex->col; advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_MULTIPLY_ASSIGN, "*=", lex->line, col); }
            else add_token(lex, TOKEN_MULTIPLY, "*", lex->line, col);
            continue;
        }
        if (c == '/') {
            int col = lex->col; advance(lex);
            if (peek(lex, 0) == '=') { advance(lex); add_token(lex, TOKEN_DIVIDE_ASSIGN, "/=", lex->line, col); }
            else add_token(lex, TOKEN_DIVIDE, "/", lex->line, col);
            continue;
        }
        if (c == '%') { add_token(lex, TOKEN_MODULO, "%", lex->line, lex->col); advance(lex); continue; }
        if (c == '(') { add_token(lex, TOKEN_LPAREN, "(", lex->line, lex->col); advance(lex); continue; }
        if (c == ')') { add_token(lex, TOKEN_RPAREN, ")", lex->line, lex->col); advance(lex); continue; }
        if (c == '{') { add_token(lex, TOKEN_LBRACE, "{", lex->line, lex->col); advance(lex); continue; }
        if (c == '}') { add_token(lex, TOKEN_RBRACE, "}", lex->line, lex->col); advance(lex); continue; }
        if (c == '[') { add_token(lex, TOKEN_LBRACKET, "[", lex->line, lex->col); advance(lex); continue; }
        if (c == ']') { add_token(lex, TOKEN_RBRACKET, "]", lex->line, lex->col); advance(lex); continue; }
        if (c == ';') { add_token(lex, TOKEN_SEMICOLON, ";", lex->line, lex->col); advance(lex); continue; }
        if (c == ',') { add_token(lex, TOKEN_COMMA, ",", lex->line, lex->col); advance(lex); continue; }
        if (c == ':') { add_token(lex, TOKEN_COLON, ":", lex->line, lex->col); advance(lex); continue; }
        if (c == '.') { add_token(lex, TOKEN_DOT, ".", lex->line, lex->col); advance(lex); continue; }
        if (c == '?') { add_token(lex, TOKEN_QUESTION, "?", lex->line, lex->col); advance(lex); continue; }

        {
            char msg[64];
            snprintf(msg, sizeof(msg), "Unexpected character: '%c'", c);
            lexer_error(lex, msg);
        }
    }
}

void lexer_free(Lexer *lex) {
    for (int i = 0; i < lex->count; i++) {
        free(lex->tokens[i].value);
    }
    free(lex->tokens);
    lex->tokens = NULL;
    lex->count = 0;
}

const char *token_type_name(TokenType t) {
    static const char *names[] = {
        "NUMBER", "STRING", "IDENTIFIER",
        "TRUE", "FALSE", "NULL",
        "LET", "IF", "ELSE", "WHILE", "FOR", "IN",
        "PRINT", "FN", "RETURN", "BREAK", "CONTINUE",
        "IMPORT", "TRY", "CATCH", "THROW",
        "CLASS", "NEW", "THIS",
        "AND", "OR", "NOT",
        "PLUS", "MINUS", "MULTIPLY", "DIVIDE", "MODULO",
        "ASSIGN", "PLUS_ASSIGN", "MINUS_ASSIGN", "MULTIPLY_ASSIGN", "DIVIDE_ASSIGN",
        "EQ", "NEQ", "GT", "LT", "GTE", "LTE",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE",
        "LBRACKET", "RBRACKET",
        "SEMICOLON", "COMMA", "COLON", "DOT", "QUESTION",
        "INTERP_BEGIN", "INTERP_END",
        "EOF"
    };
    if (t >= 0 && t <= TOKEN_EOF) return names[t];
    return "UNKNOWN";
}
