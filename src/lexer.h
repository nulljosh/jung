#ifndef JUNG_LEXER_H
#define JUNG_LEXER_H

#include <stddef.h>

typedef enum {
    TOKEN_NUMBER, TOKEN_STRING, TOKEN_IDENTIFIER,
    TOKEN_TRUE, TOKEN_FALSE, TOKEN_NULL,
    TOKEN_LET, TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_FOR, TOKEN_IN,
    TOKEN_PRINT, TOKEN_FN, TOKEN_RETURN, TOKEN_BREAK, TOKEN_CONTINUE,
    TOKEN_IMPORT, TOKEN_TRY, TOKEN_CATCH, TOKEN_THROW,
    TOKEN_CLASS, TOKEN_NEW, TOKEN_THIS,
    TOKEN_AND, TOKEN_OR, TOKEN_NOT,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_MULTIPLY, TOKEN_DIVIDE, TOKEN_MODULO,
    TOKEN_ASSIGN, TOKEN_PLUS_ASSIGN, TOKEN_MINUS_ASSIGN,
    TOKEN_MULTIPLY_ASSIGN, TOKEN_DIVIDE_ASSIGN,
    TOKEN_EQ, TOKEN_NEQ, TOKEN_GT, TOKEN_LT, TOKEN_GTE, TOKEN_LTE,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
    TOKEN_LBRACKET, TOKEN_RBRACKET,
    TOKEN_SEMICOLON, TOKEN_COMMA, TOKEN_COLON, TOKEN_DOT, TOKEN_QUESTION,
    TOKEN_INTERP_BEGIN, TOKEN_INTERP_END,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *value;       /* string representation of token value */
    double num_value;  /* numeric value for TOKEN_NUMBER */
    int line;
    int col;
} Token;

typedef struct {
    const char *source;
    size_t length;
    size_t pos;
    int line;
    int col;
    Token *tokens;
    int token_count;
    int token_cap;
} Lexer;

void lexer_init(Lexer *lex, const char *source);
void lexer_tokenize(Lexer *lex);
void lexer_free(Lexer *lex);

const char *token_type_name(TokenType type);

#endif
