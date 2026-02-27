#ifndef JUNG_PARSER_H
#define JUNG_PARSER_H

#include "lexer.h"
#include "value.h"

typedef enum {
    NODE_NUMBER, NODE_STRING, NODE_BOOL, NODE_NULL,
    NODE_VARIABLE, NODE_BINARY, NODE_UNARY,
    NODE_ASSIGN, NODE_COMPOUND_ASSIGN,
    NODE_PRINT, NODE_IF, NODE_WHILE, NODE_FOR,
    NODE_FUNC_DEF, NODE_FUNC_CALL, NODE_RETURN,
    NODE_BREAK, NODE_CONTINUE,
    NODE_IMPORT, NODE_TRY_CATCH, NODE_THROW,
    NODE_CLASS, NODE_NEW, NODE_THIS,
    NODE_ARRAY, NODE_ARRAY_INDEX,
    NODE_OBJECT, NODE_OBJ_ACCESS, NODE_OBJ_ASSIGN,
    NODE_OBJ_COMPOUND_ASSIGN,
    NODE_TERNARY, NODE_STRING_INTERP,
    NODE_PROGRAM
} NodeType;

struct ASTNode {
    NodeType type;
    int line;
    int col;

    union {
        /* NODE_NUMBER */
        double number;

        /* NODE_STRING */
        struct { char *str; int len; } string;

        /* NODE_BOOL */
        int boolean;

        /* NODE_VARIABLE */
        char *var_name;

        /* NODE_BINARY */
        struct {
            ASTNode *left;
            ASTNode *right;
            TokenType op;
        } binary;

        /* NODE_UNARY */
        struct {
            ASTNode *operand;
            TokenType op;
        } unary;

        /* NODE_ASSIGN */
        struct {
            char *name;
            ASTNode *value;
        } assign;

        /* NODE_COMPOUND_ASSIGN */
        struct {
            char *name;
            TokenType op;
            ASTNode *value;
        } comp_assign;

        /* NODE_PRINT */
        ASTNode *print_expr;

        /* NODE_IF */
        struct {
            ASTNode *condition;
            ASTNode **then_body;
            int then_count;
            ASTNode **else_body;
            int else_count;
        } if_stmt;

        /* NODE_WHILE */
        struct {
            ASTNode *condition;
            ASTNode **body;
            int body_count;
        } while_loop;

        /* NODE_FOR */
        struct {
            char *var;
            ASTNode *iterable;
            ASTNode **body;
            int body_count;
        } for_loop;

        /* NODE_FUNC_DEF */
        struct {
            char *name;
            Param *params;
            int param_count;
            ASTNode **body;
            int body_count;
        } func_def;

        /* NODE_FUNC_CALL */
        struct {
            char *name;
            ASTNode **args;
            int arg_count;
        } func_call;

        /* NODE_RETURN */
        ASTNode *return_val; /* may be NULL */

        /* NODE_IMPORT */
        char *import_path;

        /* NODE_TRY_CATCH */
        struct {
            ASTNode **try_body;
            int try_count;
            char *catch_var;   /* may be NULL */
            ASTNode **catch_body;
            int catch_count;
        } try_catch;

        /* NODE_THROW */
        ASTNode *throw_val;

        /* NODE_CLASS */
        struct {
            char *name;
            ASTNode **methods;   /* each is NODE_FUNC_DEF */
            int method_count;
        } class_def;

        /* NODE_NEW */
        struct {
            char *class_name;
            ASTNode **args;
            int arg_count;
        } new_inst;

        /* NODE_ARRAY */
        struct {
            ASTNode **elements;
            int count;
        } array;

        /* NODE_ARRAY_INDEX */
        struct {
            ASTNode *array_expr;
            ASTNode *index;
        } array_index;

        /* NODE_OBJECT */
        struct {
            char **keys;
            ASTNode **values;
            int count;
        } object;

        /* NODE_OBJ_ACCESS */
        struct {
            ASTNode *obj;
            char *key;       /* for dot notation */
            ASTNode *key_expr; /* for bracket notation */
            int is_bracket;
        } obj_access;

        /* NODE_OBJ_ASSIGN */
        struct {
            ASTNode *obj;
            char *key;
            ASTNode *key_expr;
            ASTNode *value;
            int is_bracket;
        } obj_assign;

        /* NODE_OBJ_COMPOUND_ASSIGN */
        struct {
            ASTNode *obj;
            char *key;
            ASTNode *key_expr;
            ASTNode *value;
            int is_bracket;
            TokenType op;
        } obj_comp_assign;

        /* NODE_TERNARY */
        struct {
            ASTNode *condition;
            ASTNode *then_expr;
            ASTNode *else_expr;
        } ternary;

        /* NODE_STRING_INTERP */
        struct {
            ASTNode **parts;   /* alternating STRING nodes and expression nodes */
            int count;
        } interp;

        /* NODE_PROGRAM */
        struct {
            ASTNode **stmts;
            int count;
        } program;
    } as;
};

typedef struct {
    Token *tokens;
    int token_count;
    int current;
} Parser;

void     parser_init(Parser *p, Token *tokens, int count);
ASTNode *parser_parse(Parser *p);          /* returns NODE_PROGRAM */
ASTNode *parser_parse_expression(Parser *p); /* for REPL / string interpolation */
void     ast_free(ASTNode *node);

#endif
