#ifndef JUNG_PARSER_H
#define JUNG_PARSER_H

#include "lexer.h"

typedef enum {
    NODE_NUMBER,
    NODE_STRING,
    NODE_BOOL,
    NODE_NULL,
    NODE_ARRAY,
    NODE_OBJECT,
    NODE_VARIABLE,
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_ASSIGNMENT,
    NODE_COMPOUND_ASSIGN,
    NODE_TERNARY,
    NODE_STRING_INTERP,
    NODE_INDEX,
    NODE_DOT_ACCESS,
    NODE_DOT_ASSIGN,
    NODE_METHOD_CALL,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_FUNCTION_DEF,
    NODE_FUNCTION_CALL,
    NODE_RETURN,
    NODE_CLASS_DEF,
    NODE_NEW_INSTANCE,
    NODE_THIS,
    NODE_TRY_CATCH,
    NODE_THROW,
    NODE_PRINT,
    NODE_IMPORT,
    NODE_PROGRAM
} NodeType;

typedef struct ASTNode {
    NodeType type;
    int line;
    union {
        /* NODE_NUMBER */
        double number;

        /* NODE_STRING */
        char *string;

        /* NODE_BOOL */
        int boolean;

        /* NODE_VARIABLE */
        char *var_name;

        /* NODE_ARRAY */
        struct { struct ASTNode **elements; int count; } array;

        /* NODE_OBJECT */
        struct { char **keys; struct ASTNode **values; int count; } object;

        /* NODE_BINARY_OP */
        struct { struct ASTNode *left; struct ASTNode *right; TokenType op; } binary;

        /* NODE_UNARY_OP */
        struct { struct ASTNode *operand; TokenType op; } unary;

        /* NODE_ASSIGNMENT */
        struct { char *name; struct ASTNode *value; } assign;

        /* NODE_COMPOUND_ASSIGN */
        struct { char *name; TokenType op; struct ASTNode *value; } compound;

        /* NODE_TERNARY */
        struct { struct ASTNode *cond; struct ASTNode *then_expr; struct ASTNode *else_expr; } ternary;

        /* NODE_STRING_INTERP */
        struct { struct ASTNode **parts; int count; } interp;

        /* NODE_INDEX */
        struct { struct ASTNode *object; struct ASTNode *index; } index;

        /* NODE_DOT_ACCESS */
        struct { struct ASTNode *object; char *field; } dot;

        /* NODE_DOT_ASSIGN */
        struct { struct ASTNode *object; char *field; struct ASTNode *value; int is_bracket; } dot_assign;

        /* NODE_METHOD_CALL */
        struct { struct ASTNode *object; char *method; struct ASTNode **args; int arg_count; } method;

        /* NODE_IF */
        struct { struct ASTNode *cond; struct ASTNode **then_stmts; int then_count;
                 struct ASTNode **else_stmts; int else_count; } if_stmt;

        /* NODE_WHILE */
        struct { struct ASTNode *cond; struct ASTNode **body; int body_count; } while_loop;

        /* NODE_FOR */
        struct { char *var; struct ASTNode *iterable;
                 struct ASTNode **body; int body_count; } for_loop;

        /* NODE_FUNCTION_DEF */
        struct { char *name; char **params; int param_count;
                 struct ASTNode **body; int body_count; } func_def;

        /* NODE_FUNCTION_CALL */
        struct { char *name; struct ASTNode **args; int arg_count; } func_call;

        /* NODE_RETURN */
        struct { struct ASTNode *value; } ret;

        /* NODE_CLASS_DEF */
        struct { char *name; struct ASTNode **methods; int method_count; } class_def;

        /* NODE_NEW_INSTANCE */
        struct { char *class_name; struct ASTNode **args; int arg_count; } new_inst;

        /* NODE_TRY_CATCH */
        struct { struct ASTNode **try_stmts; int try_count;
                 char *catch_var;
                 struct ASTNode **catch_stmts; int catch_count; } try_catch;

        /* NODE_THROW */
        struct { struct ASTNode *value; } throw_stmt;

        /* NODE_PRINT */
        struct { struct ASTNode *expr; } print_stmt;

        /* NODE_IMPORT */
        struct { char *path; } import_stmt;

        /* NODE_PROGRAM */
        struct { struct ASTNode **stmts; int count; } program;
    } as;
} ASTNode;

typedef struct {
    Token *tokens;
    int count;
    int pos;
} Parser;

void parser_init(Parser *p, Token *tokens, int count);
ASTNode *parser_parse(Parser *p);
void ast_free(ASTNode *node);

#endif
