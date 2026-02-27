#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers ---- */

static ASTNode *alloc_node(NodeType type, int line, int col) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    n->type = type;
    n->line = line;
    n->col = col;
    return n;
}

static Token *peek(Parser *p, int offset) {
    int idx = p->current + offset;
    if (idx < p->token_count) return &p->tokens[idx];
    return NULL;
}

static Token *cur(Parser *p) { return peek(p, 0); }

static int match(Parser *p, TokenType t) {
    Token *c = cur(p);
    return c && c->type == t;
}

static Token *advance_tok(Parser *p) {
    if (p->current >= p->token_count) return NULL;
    return &p->tokens[p->current++];
}

static void parser_error(Parser *p, const char *msg) {
    Token *t = cur(p);
    if (t) {
        fprintf(stderr, "Parse error at line %d:%d - %s (got %s)\n",
                t->line, t->col, msg, token_type_name(t->type));
    } else {
        fprintf(stderr, "Parse error: unexpected end of file - %s\n", msg);
    }
    exit(1);
}

static Token *consume(Parser *p, TokenType t, const char *msg) {
    if (!match(p, t)) parser_error(p, msg);
    return advance_tok(p);
}

static void optional_semicolon(Parser *p) {
    if (match(p, TOKEN_SEMICOLON)) advance_tok(p);
}

/* forward declarations */
static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_statement(Parser *p);

/* ---- parse block ---- */

static void parse_block(Parser *p, ASTNode ***body, int *count) {
    consume(p, TOKEN_LBRACE, "Expected '{'");
    int cap = 8;
    *body = malloc(sizeof(ASTNode *) * (size_t)cap);
    *count = 0;

    while (!match(p, TOKEN_RBRACE) && !match(p, TOKEN_EOF)) {
        ASTNode *s = parse_statement(p);
        if (s) {
            if (*count >= cap) { cap *= 2; *body = realloc(*body, sizeof(ASTNode *) * (size_t)cap); }
            (*body)[(*count)++] = s;
        }
    }
    consume(p, TOKEN_RBRACE, "Expected '}'");
}

/* ---- expression parsing ---- */

static ASTNode *parse_primary(Parser *p) {
    int line = cur(p) ? cur(p)->line : 0;
    int col = cur(p) ? cur(p)->col : 0;

    /* unary not */
    if (match(p, TOKEN_NOT)) {
        advance_tok(p);
        ASTNode *n = alloc_node(NODE_UNARY, line, col);
        n->as.unary.op = TOKEN_NOT;
        n->as.unary.operand = parse_primary(p);
        return n;
    }

    /* unary minus */
    if (match(p, TOKEN_MINUS)) {
        advance_tok(p);
        ASTNode *n = alloc_node(NODE_UNARY, line, col);
        n->as.unary.op = TOKEN_MINUS;
        n->as.unary.operand = parse_primary(p);
        return n;
    }

    /* number */
    if (match(p, TOKEN_NUMBER)) {
        Token *t = advance_tok(p);
        ASTNode *n = alloc_node(NODE_NUMBER, line, col);
        n->as.number = t->num_value;
        return n;
    }

    /* string */
    if (match(p, TOKEN_STRING)) {
        Token *t = advance_tok(p);
        ASTNode *n = alloc_node(NODE_STRING, line, col);
        n->as.string.str = strdup(t->value ? t->value : "");
        n->as.string.len = (int)strlen(n->as.string.str);
        return n;
    }

    /* string interpolation */
    if (match(p, TOKEN_INTERP_BEGIN)) {
        advance_tok(p); /* consume INTERP_BEGIN */
        int cap = 8;
        ASTNode **parts = malloc(sizeof(ASTNode *) * (size_t)cap);
        int cnt = 0;

        while (!match(p, TOKEN_INTERP_END) && !match(p, TOKEN_EOF)) {
            ASTNode *part = parse_expression(p);
            if (cnt >= cap) { cap *= 2; parts = realloc(parts, sizeof(ASTNode *) * (size_t)cap); }
            parts[cnt++] = part;
        }
        consume(p, TOKEN_INTERP_END, "Expected end of string interpolation");

        ASTNode *n = alloc_node(NODE_STRING_INTERP, line, col);
        n->as.interp.parts = parts;
        n->as.interp.count = cnt;
        return n;
    }

    /* null */
    if (match(p, TOKEN_NULL)) {
        advance_tok(p);
        return alloc_node(NODE_NULL, line, col);
    }

    /* true */
    if (match(p, TOKEN_TRUE)) {
        advance_tok(p);
        ASTNode *n = alloc_node(NODE_BOOL, line, col);
        n->as.boolean = 1;
        return n;
    }

    /* false */
    if (match(p, TOKEN_FALSE)) {
        advance_tok(p);
        ASTNode *n = alloc_node(NODE_BOOL, line, col);
        n->as.boolean = 0;
        return n;
    }

    /* this */
    if (match(p, TOKEN_THIS)) {
        advance_tok(p);
        return alloc_node(NODE_THIS, line, col);
    }

    /* new */
    if (match(p, TOKEN_NEW)) {
        advance_tok(p);
        Token *name = consume(p, TOKEN_IDENTIFIER, "Expected class name after 'new'");
        consume(p, TOKEN_LPAREN, "Expected '(' after class name");

        int cap = 4, cnt = 0;
        ASTNode **args = malloc(sizeof(ASTNode *) * (size_t)cap);

        if (!match(p, TOKEN_RPAREN)) {
            if (cnt >= cap) { cap *= 2; args = realloc(args, sizeof(ASTNode *) * (size_t)cap); }
            args[cnt++] = parse_expression(p);
            while (match(p, TOKEN_COMMA)) {
                advance_tok(p);
                if (cnt >= cap) { cap *= 2; args = realloc(args, sizeof(ASTNode *) * (size_t)cap); }
                args[cnt++] = parse_expression(p);
            }
        }
        consume(p, TOKEN_RPAREN, "Expected ')' after arguments");

        ASTNode *n = alloc_node(NODE_NEW, line, col);
        n->as.new_inst.class_name = strdup(name->value);
        n->as.new_inst.args = args;
        n->as.new_inst.arg_count = cnt;
        return n;
    }

    /* identifier or function call */
    if (match(p, TOKEN_IDENTIFIER)) {
        Token *t = advance_tok(p);
        char *name = t->value;

        if (match(p, TOKEN_LPAREN)) {
            advance_tok(p);
            int cap = 4, cnt = 0;
            ASTNode **args = malloc(sizeof(ASTNode *) * (size_t)cap);
            if (!match(p, TOKEN_RPAREN)) {
                if (cnt >= cap) { cap *= 2; args = realloc(args, sizeof(ASTNode *) * (size_t)cap); }
                args[cnt++] = parse_expression(p);
                while (match(p, TOKEN_COMMA)) {
                    advance_tok(p);
                    if (cnt >= cap) { cap *= 2; args = realloc(args, sizeof(ASTNode *) * (size_t)cap); }
                    args[cnt++] = parse_expression(p);
                }
            }
            consume(p, TOKEN_RPAREN, "Expected ')' after arguments");
            ASTNode *n = alloc_node(NODE_FUNC_CALL, line, col);
            n->as.func_call.name = strdup(name);
            n->as.func_call.args = args;
            n->as.func_call.arg_count = cnt;
            return n;
        }

        ASTNode *n = alloc_node(NODE_VARIABLE, line, col);
        n->as.var_name = strdup(name);
        return n;
    }

    /* array literal */
    if (match(p, TOKEN_LBRACKET)) {
        advance_tok(p);
        int cap = 8, cnt = 0;
        ASTNode **elems = malloc(sizeof(ASTNode *) * (size_t)cap);
        if (!match(p, TOKEN_RBRACKET)) {
            if (cnt >= cap) { cap *= 2; elems = realloc(elems, sizeof(ASTNode *) * (size_t)cap); }
            elems[cnt++] = parse_expression(p);
            while (match(p, TOKEN_COMMA)) {
                advance_tok(p);
                if (cnt >= cap) { cap *= 2; elems = realloc(elems, sizeof(ASTNode *) * (size_t)cap); }
                elems[cnt++] = parse_expression(p);
            }
        }
        consume(p, TOKEN_RBRACKET, "Expected ']'");
        ASTNode *n = alloc_node(NODE_ARRAY, line, col);
        n->as.array.elements = elems;
        n->as.array.count = cnt;
        return n;
    }

    /* object literal */
    if (match(p, TOKEN_LBRACE)) {
        advance_tok(p);
        int cap = 8, cnt = 0;
        char **keys = malloc(sizeof(char *) * (size_t)cap);
        ASTNode **vals = malloc(sizeof(ASTNode *) * (size_t)cap);

        if (!match(p, TOKEN_RBRACE)) {
            Token *k = consume(p, TOKEN_IDENTIFIER, "Expected property name");
            consume(p, TOKEN_COLON, "Expected ':' after property name");
            ASTNode *v = parse_expression(p);
            if (cnt >= cap) { cap *= 2; keys = realloc(keys, sizeof(char *) * (size_t)cap); vals = realloc(vals, sizeof(ASTNode *) * (size_t)cap); }
            keys[cnt] = strdup(k->value);
            vals[cnt] = v;
            cnt++;

            while (match(p, TOKEN_COMMA)) {
                advance_tok(p);
                if (match(p, TOKEN_RBRACE)) break; /* trailing comma */
                k = consume(p, TOKEN_IDENTIFIER, "Expected property name");
                consume(p, TOKEN_COLON, "Expected ':' after property name");
                v = parse_expression(p);
                if (cnt >= cap) { cap *= 2; keys = realloc(keys, sizeof(char *) * (size_t)cap); vals = realloc(vals, sizeof(ASTNode *) * (size_t)cap); }
                keys[cnt] = strdup(k->value);
                vals[cnt] = v;
                cnt++;
            }
        }
        consume(p, TOKEN_RBRACE, "Expected '}'");
        ASTNode *n = alloc_node(NODE_OBJECT, line, col);
        n->as.object.keys = keys;
        n->as.object.values = vals;
        n->as.object.count = cnt;
        return n;
    }

    /* parenthesized expression */
    if (match(p, TOKEN_LPAREN)) {
        advance_tok(p);
        ASTNode *expr = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')' after expression");
        return expr;
    }

    parser_error(p, "Expected expression");
    return NULL; /* unreachable */
}

static ASTNode *parse_postfix(Parser *p) {
    ASTNode *left = parse_primary(p);

    while (1) {
        int line = cur(p) ? cur(p)->line : 0;
        int col = cur(p) ? cur(p)->col : 0;

        /* array indexing / bracket access */
        if (match(p, TOKEN_LBRACKET)) {
            advance_tok(p);
            ASTNode *idx = parse_expression(p);
            consume(p, TOKEN_RBRACKET, "Expected ']'");
            ASTNode *n = alloc_node(NODE_ARRAY_INDEX, line, col);
            n->as.array_index.array_expr = left;
            n->as.array_index.index = idx;
            left = n;
        }
        /* dot access / method call */
        else if (match(p, TOKEN_DOT)) {
            advance_tok(p);
            if (!match(p, TOKEN_IDENTIFIER)) parser_error(p, "Expected property name after '.'");
            Token *name = advance_tok(p);

            if (match(p, TOKEN_LPAREN)) {
                /* method call -> __method_NAME */
                advance_tok(p);
                int cap = 4, cnt = 0;
                ASTNode **args = malloc(sizeof(ASTNode *) * (size_t)cap);
                /* first arg is the object */
                args[cnt++] = left;
                if (!match(p, TOKEN_RPAREN)) {
                    if (cnt >= cap) { cap *= 2; args = realloc(args, sizeof(ASTNode *) * (size_t)cap); }
                    args[cnt++] = parse_expression(p);
                    while (match(p, TOKEN_COMMA)) {
                        advance_tok(p);
                        if (cnt >= cap) { cap *= 2; args = realloc(args, sizeof(ASTNode *) * (size_t)cap); }
                        args[cnt++] = parse_expression(p);
                    }
                }
                consume(p, TOKEN_RPAREN, "Expected ')' after arguments");

                char method_name[300];
                snprintf(method_name, sizeof(method_name), "__method_%s", name->value);

                ASTNode *n = alloc_node(NODE_FUNC_CALL, line, col);
                n->as.func_call.name = strdup(method_name);
                n->as.func_call.args = args;
                n->as.func_call.arg_count = cnt;
                left = n;
            } else {
                /* property access */
                ASTNode *n = alloc_node(NODE_OBJ_ACCESS, line, col);
                n->as.obj_access.obj = left;
                n->as.obj_access.key = strdup(name->value);
                n->as.obj_access.key_expr = NULL;
                n->as.obj_access.is_bracket = 0;
                left = n;
            }
        } else {
            break;
        }
    }
    return left;
}

static ASTNode *parse_multiplication(Parser *p) {
    ASTNode *left = parse_postfix(p);
    while (match(p, TOKEN_MULTIPLY) || match(p, TOKEN_DIVIDE) || match(p, TOKEN_MODULO)) {
        int line = cur(p)->line, col = cur(p)->col;
        Token *op = advance_tok(p);
        ASTNode *right = parse_postfix(p);
        ASTNode *n = alloc_node(NODE_BINARY, line, col);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = op->type;
        left = n;
    }
    return left;
}

static ASTNode *parse_addition(Parser *p) {
    ASTNode *left = parse_multiplication(p);
    while (match(p, TOKEN_PLUS) || match(p, TOKEN_MINUS)) {
        int line = cur(p)->line, col = cur(p)->col;
        Token *op = advance_tok(p);
        ASTNode *right = parse_multiplication(p);
        ASTNode *n = alloc_node(NODE_BINARY, line, col);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = op->type;
        left = n;
    }
    return left;
}

static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_addition(p);
    while (match(p, TOKEN_GT) || match(p, TOKEN_LT) || match(p, TOKEN_EQ) ||
           match(p, TOKEN_NEQ) || match(p, TOKEN_GTE) || match(p, TOKEN_LTE)) {
        int line = cur(p)->line, col = cur(p)->col;
        Token *op = advance_tok(p);
        ASTNode *right = parse_addition(p);
        ASTNode *n = alloc_node(NODE_BINARY, line, col);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = op->type;
        left = n;
    }
    return left;
}

static ASTNode *parse_and(Parser *p) {
    ASTNode *left = parse_comparison(p);
    while (match(p, TOKEN_AND)) {
        int line = cur(p)->line, col = cur(p)->col;
        advance_tok(p);
        ASTNode *right = parse_comparison(p);
        ASTNode *n = alloc_node(NODE_BINARY, line, col);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = TOKEN_AND;
        left = n;
    }
    return left;
}

static ASTNode *parse_or(Parser *p) {
    ASTNode *left = parse_and(p);
    while (match(p, TOKEN_OR)) {
        int line = cur(p)->line, col = cur(p)->col;
        advance_tok(p);
        ASTNode *right = parse_and(p);
        ASTNode *n = alloc_node(NODE_BINARY, line, col);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = TOKEN_OR;
        left = n;
    }
    return left;
}

static ASTNode *parse_ternary(Parser *p) {
    ASTNode *expr = parse_or(p);
    if (match(p, TOKEN_QUESTION)) {
        int line = cur(p)->line, col = cur(p)->col;
        advance_tok(p);
        ASTNode *then_e = parse_ternary(p);
        consume(p, TOKEN_COLON, "Expected ':' in ternary expression");
        ASTNode *else_e = parse_ternary(p);
        ASTNode *n = alloc_node(NODE_TERNARY, line, col);
        n->as.ternary.condition = expr;
        n->as.ternary.then_expr = then_e;
        n->as.ternary.else_expr = else_e;
        return n;
    }
    return expr;
}

static ASTNode *parse_expression(Parser *p) {
    return parse_ternary(p);
}

/* ---- parse params ---- */

static void parse_params(Parser *p, Param **out, int *count) {
    consume(p, TOKEN_LPAREN, "Expected '(' after function name");
    int cap = 4;
    *out = malloc(sizeof(Param) * (size_t)cap);
    *count = 0;

    if (!match(p, TOKEN_RPAREN)) {
        Token *pname = consume(p, TOKEN_IDENTIFIER, "Expected parameter name");
        Param pm;
        pm.name = strdup(pname->value);
        pm.default_val = NULL;
        if (match(p, TOKEN_ASSIGN)) {
            advance_tok(p);
            pm.default_val = parse_expression(p);
        }
        if (*count >= cap) { cap *= 2; *out = realloc(*out, sizeof(Param) * (size_t)cap); }
        (*out)[(*count)++] = pm;

        while (match(p, TOKEN_COMMA)) {
            advance_tok(p);
            pname = consume(p, TOKEN_IDENTIFIER, "Expected parameter name");
            pm.name = strdup(pname->value);
            pm.default_val = NULL;
            if (match(p, TOKEN_ASSIGN)) {
                advance_tok(p);
                pm.default_val = parse_expression(p);
            }
            if (*count >= cap) { cap *= 2; *out = realloc(*out, sizeof(Param) * (size_t)cap); }
            (*out)[(*count)++] = pm;
        }
    }
    consume(p, TOKEN_RPAREN, "Expected ')' after parameters");
}

/* ---- statement parsing ---- */

static ASTNode *parse_statement(Parser *p) {
    if (!cur(p) || match(p, TOKEN_EOF)) return NULL;

    int line = cur(p)->line, col = cur(p)->col;

    /* class */
    if (match(p, TOKEN_CLASS)) {
        advance_tok(p);
        Token *name = consume(p, TOKEN_IDENTIFIER, "Expected class name");
        consume(p, TOKEN_LBRACE, "Expected '{' after class name");

        int cap = 8, cnt = 0;
        ASTNode **methods = malloc(sizeof(ASTNode *) * (size_t)cap);

        while (!match(p, TOKEN_RBRACE) && !match(p, TOKEN_EOF)) {
            int mline = cur(p)->line, mcol = cur(p)->col;
            if (!match(p, TOKEN_FN)) parser_error(p, "Expected method definition in class");
            advance_tok(p);
            Token *mname = consume(p, TOKEN_IDENTIFIER, "Expected method name");

            Param *params; int pcount;
            parse_params(p, &params, &pcount);

            ASTNode **body; int bcount;
            parse_block(p, &body, &bcount);

            ASTNode *m = alloc_node(NODE_FUNC_DEF, mline, mcol);
            m->as.func_def.name = strdup(mname->value);
            m->as.func_def.params = params;
            m->as.func_def.param_count = pcount;
            m->as.func_def.body = body;
            m->as.func_def.body_count = bcount;

            if (cnt >= cap) { cap *= 2; methods = realloc(methods, sizeof(ASTNode *) * (size_t)cap); }
            methods[cnt++] = m;
        }
        consume(p, TOKEN_RBRACE, "Expected '}' after class body");

        ASTNode *n = alloc_node(NODE_CLASS, line, col);
        n->as.class_def.name = strdup(name->value);
        n->as.class_def.methods = methods;
        n->as.class_def.method_count = cnt;
        return n;
    }

    /* function def */
    if (match(p, TOKEN_FN)) {
        advance_tok(p);
        Token *name = consume(p, TOKEN_IDENTIFIER, "Expected function name");

        Param *params; int pcount;
        parse_params(p, &params, &pcount);

        ASTNode **body; int bcount;
        parse_block(p, &body, &bcount);

        ASTNode *n = alloc_node(NODE_FUNC_DEF, line, col);
        n->as.func_def.name = strdup(name->value);
        n->as.func_def.params = params;
        n->as.func_def.param_count = pcount;
        n->as.func_def.body = body;
        n->as.func_def.body_count = bcount;
        return n;
    }

    /* return */
    if (match(p, TOKEN_RETURN)) {
        advance_tok(p);
        ASTNode *val = NULL;
        if (!match(p, TOKEN_SEMICOLON) && !match(p, TOKEN_RBRACE) && !match(p, TOKEN_EOF)) {
            val = parse_expression(p);
        }
        optional_semicolon(p);
        ASTNode *n = alloc_node(NODE_RETURN, line, col);
        n->as.return_val = val;
        return n;
    }

    /* break */
    if (match(p, TOKEN_BREAK)) {
        advance_tok(p);
        optional_semicolon(p);
        return alloc_node(NODE_BREAK, line, col);
    }

    /* continue */
    if (match(p, TOKEN_CONTINUE)) {
        advance_tok(p);
        optional_semicolon(p);
        return alloc_node(NODE_CONTINUE, line, col);
    }

    /* import */
    if (match(p, TOKEN_IMPORT)) {
        advance_tok(p);
        Token *path = consume(p, TOKEN_STRING, "Expected string path after import");
        optional_semicolon(p);
        ASTNode *n = alloc_node(NODE_IMPORT, line, col);
        n->as.import_path = strdup(path->value);
        return n;
    }

    /* try/catch */
    if (match(p, TOKEN_TRY)) {
        advance_tok(p);
        ASTNode **try_body; int try_cnt;
        parse_block(p, &try_body, &try_cnt);

        consume(p, TOKEN_CATCH, "Expected 'catch' after try block");

        char *catch_var = NULL;
        if (match(p, TOKEN_LPAREN)) {
            advance_tok(p);
            Token *cv = consume(p, TOKEN_IDENTIFIER, "Expected variable name in catch");
            catch_var = strdup(cv->value);
            consume(p, TOKEN_RPAREN, "Expected ')' after catch variable");
        }

        ASTNode **catch_body; int catch_cnt;
        parse_block(p, &catch_body, &catch_cnt);

        ASTNode *n = alloc_node(NODE_TRY_CATCH, line, col);
        n->as.try_catch.try_body = try_body;
        n->as.try_catch.try_count = try_cnt;
        n->as.try_catch.catch_var = catch_var;
        n->as.try_catch.catch_body = catch_body;
        n->as.try_catch.catch_count = catch_cnt;
        return n;
    }

    /* throw */
    if (match(p, TOKEN_THROW)) {
        advance_tok(p);
        ASTNode *val = parse_expression(p);
        optional_semicolon(p);
        ASTNode *n = alloc_node(NODE_THROW, line, col);
        n->as.throw_val = val;
        return n;
    }

    /* if */
    if (match(p, TOKEN_IF)) {
        advance_tok(p);
        ASTNode *cond = parse_expression(p);

        ASTNode **then_body; int then_cnt;
        parse_block(p, &then_body, &then_cnt);

        ASTNode **else_body = NULL; int else_cnt = 0;
        if (match(p, TOKEN_ELSE)) {
            advance_tok(p);
            /* else if */
            if (match(p, TOKEN_IF)) {
                else_body = malloc(sizeof(ASTNode *));
                else_body[0] = parse_statement(p); /* recursion for else if */
                else_cnt = 1;
            } else {
                parse_block(p, &else_body, &else_cnt);
            }
        }

        ASTNode *n = alloc_node(NODE_IF, line, col);
        n->as.if_stmt.condition = cond;
        n->as.if_stmt.then_body = then_body;
        n->as.if_stmt.then_count = then_cnt;
        n->as.if_stmt.else_body = else_body;
        n->as.if_stmt.else_count = else_cnt;
        return n;
    }

    /* while */
    if (match(p, TOKEN_WHILE)) {
        advance_tok(p);
        ASTNode *cond = parse_expression(p);
        ASTNode **body; int cnt;
        parse_block(p, &body, &cnt);

        ASTNode *n = alloc_node(NODE_WHILE, line, col);
        n->as.while_loop.condition = cond;
        n->as.while_loop.body = body;
        n->as.while_loop.body_count = cnt;
        return n;
    }

    /* for */
    if (match(p, TOKEN_FOR)) {
        advance_tok(p);
        Token *var = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
        consume(p, TOKEN_IN, "Expected 'in'");
        ASTNode *iter = parse_expression(p);
        ASTNode **body; int cnt;
        parse_block(p, &body, &cnt);

        ASTNode *n = alloc_node(NODE_FOR, line, col);
        n->as.for_loop.var = strdup(var->value);
        n->as.for_loop.iterable = iter;
        n->as.for_loop.body = body;
        n->as.for_loop.body_count = cnt;
        return n;
    }

    /* let */
    if (match(p, TOKEN_LET)) {
        advance_tok(p);
        Token *name = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
        consume(p, TOKEN_ASSIGN, "Expected '=' in assignment");
        ASTNode *val = parse_expression(p);
        optional_semicolon(p);

        ASTNode *n = alloc_node(NODE_ASSIGN, line, col);
        n->as.assign.name = strdup(name->value);
        n->as.assign.value = val;
        return n;
    }

    /* print */
    if (match(p, TOKEN_PRINT)) {
        advance_tok(p);
        ASTNode *expr = parse_expression(p);
        optional_semicolon(p);
        ASTNode *n = alloc_node(NODE_PRINT, line, col);
        n->as.print_expr = expr;
        return n;
    }

    /* reassignment or compound assignment: identifier = ... or identifier += ... */
    if (match(p, TOKEN_IDENTIFIER)) {
        Token *next = peek(p, 1);
        if (next && next->type == TOKEN_ASSIGN) {
            Token *name = advance_tok(p);
            advance_tok(p); /* = */
            ASTNode *val = parse_expression(p);
            optional_semicolon(p);
            ASTNode *n = alloc_node(NODE_ASSIGN, line, col);
            n->as.assign.name = strdup(name->value);
            n->as.assign.value = val;
            return n;
        }
        if (next && (next->type == TOKEN_PLUS_ASSIGN || next->type == TOKEN_MINUS_ASSIGN ||
                     next->type == TOKEN_MULTIPLY_ASSIGN || next->type == TOKEN_DIVIDE_ASSIGN)) {
            Token *name = advance_tok(p);
            Token *op = advance_tok(p);
            ASTNode *val = parse_expression(p);
            optional_semicolon(p);
            ASTNode *n = alloc_node(NODE_COMPOUND_ASSIGN, line, col);
            n->as.comp_assign.name = strdup(name->value);
            n->as.comp_assign.op = op->type;
            n->as.comp_assign.value = val;
            return n;
        }
    }

    /* expression statement (includes obj.prop = val) */
    if (!match(p, TOKEN_EOF)) {
        ASTNode *expr = parse_expression(p);

        /* check for obj.prop = val or arr[idx] = val */
        if (match(p, TOKEN_ASSIGN)) {
            advance_tok(p);
            ASTNode *val = parse_expression(p);
            optional_semicolon(p);

            ASTNode *n = alloc_node(NODE_OBJ_ASSIGN, line, col);
            if (expr->type == NODE_OBJ_ACCESS) {
                n->as.obj_assign.obj = expr->as.obj_access.obj;
                n->as.obj_assign.key = expr->as.obj_access.key;
                n->as.obj_assign.key_expr = expr->as.obj_access.key_expr;
                n->as.obj_assign.is_bracket = expr->as.obj_access.is_bracket;
                n->as.obj_assign.value = val;
                /* don't free expr, we stole its children */
                free(expr);
            } else if (expr->type == NODE_ARRAY_INDEX) {
                n->as.obj_assign.obj = expr->as.array_index.array_expr;
                n->as.obj_assign.key = NULL;
                n->as.obj_assign.key_expr = expr->as.array_index.index;
                n->as.obj_assign.is_bracket = 1;
                n->as.obj_assign.value = val;
                free(expr);
            } else {
                parser_error(p, "Invalid assignment target");
            }
            return n;
        }

        /* compound assignment on obj.prop or arr[idx]: obj.prop += val */
        if (match(p, TOKEN_PLUS_ASSIGN) || match(p, TOKEN_MINUS_ASSIGN) ||
            match(p, TOKEN_MULTIPLY_ASSIGN) || match(p, TOKEN_DIVIDE_ASSIGN)) {
            Token *op_tok = advance_tok(p);
            ASTNode *rhs = parse_expression(p);
            optional_semicolon(p);

            if (expr->type == NODE_OBJ_ACCESS || expr->type == NODE_ARRAY_INDEX) {
                ASTNode *n = alloc_node(NODE_OBJ_COMPOUND_ASSIGN, line, col);
                if (expr->type == NODE_OBJ_ACCESS) {
                    n->as.obj_comp_assign.obj = expr->as.obj_access.obj;
                    n->as.obj_comp_assign.key = expr->as.obj_access.key;
                    n->as.obj_comp_assign.key_expr = expr->as.obj_access.key_expr;
                    n->as.obj_comp_assign.is_bracket = expr->as.obj_access.is_bracket;
                } else {
                    n->as.obj_comp_assign.obj = expr->as.array_index.array_expr;
                    n->as.obj_comp_assign.key = NULL;
                    n->as.obj_comp_assign.key_expr = expr->as.array_index.index;
                    n->as.obj_comp_assign.is_bracket = 1;
                }
                n->as.obj_comp_assign.value = rhs;
                n->as.obj_comp_assign.op = op_tok->type;
                free(expr);
                return n;
            } else {
                parser_error(p, "Invalid compound assignment target");
            }
        }

        optional_semicolon(p);
        return expr;
    }

    return NULL;
}

/* ---- public API ---- */

void parser_init(Parser *p, Token *tokens, int count) {
    p->tokens = tokens;
    p->token_count = count;
    p->current = 0;
}

ASTNode *parser_parse(Parser *p) {
    int cap = 32;
    ASTNode **stmts = malloc(sizeof(ASTNode *) * (size_t)cap);
    int cnt = 0;

    while (!match(p, TOKEN_EOF)) {
        ASTNode *s = parse_statement(p);
        if (s) {
            if (cnt >= cap) { cap *= 2; stmts = realloc(stmts, sizeof(ASTNode *) * (size_t)cap); }
            stmts[cnt++] = s;
        }
    }

    ASTNode *prog = alloc_node(NODE_PROGRAM, 0, 0);
    prog->as.program.stmts = stmts;
    prog->as.program.count = cnt;
    return prog;
}

ASTNode *parser_parse_expression(Parser *p) {
    return parse_expression(p);
}

void ast_free(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_NUMBER:
        case NODE_BOOL:
        case NODE_NULL:
        case NODE_THIS:
        case NODE_BREAK:
        case NODE_CONTINUE:
            break;

        case NODE_STRING:
            free(node->as.string.str);
            break;

        case NODE_VARIABLE:
            free(node->as.var_name);
            break;

        case NODE_BINARY:
            ast_free(node->as.binary.left);
            ast_free(node->as.binary.right);
            break;

        case NODE_UNARY:
            ast_free(node->as.unary.operand);
            break;

        case NODE_ASSIGN:
            free(node->as.assign.name);
            ast_free(node->as.assign.value);
            break;

        case NODE_COMPOUND_ASSIGN:
            free(node->as.comp_assign.name);
            ast_free(node->as.comp_assign.value);
            break;

        case NODE_PRINT:
            ast_free(node->as.print_expr);
            break;

        case NODE_IF:
            ast_free(node->as.if_stmt.condition);
            for (int i = 0; i < node->as.if_stmt.then_count; i++)
                ast_free(node->as.if_stmt.then_body[i]);
            free(node->as.if_stmt.then_body);
            for (int i = 0; i < node->as.if_stmt.else_count; i++)
                ast_free(node->as.if_stmt.else_body[i]);
            free(node->as.if_stmt.else_body);
            break;

        case NODE_WHILE:
            ast_free(node->as.while_loop.condition);
            for (int i = 0; i < node->as.while_loop.body_count; i++)
                ast_free(node->as.while_loop.body[i]);
            free(node->as.while_loop.body);
            break;

        case NODE_FOR:
            free(node->as.for_loop.var);
            ast_free(node->as.for_loop.iterable);
            for (int i = 0; i < node->as.for_loop.body_count; i++)
                ast_free(node->as.for_loop.body[i]);
            free(node->as.for_loop.body);
            break;

        case NODE_FUNC_DEF:
            free(node->as.func_def.name);
            for (int i = 0; i < node->as.func_def.param_count; i++) {
                free(node->as.func_def.params[i].name);
                ast_free(node->as.func_def.params[i].default_val);
            }
            free(node->as.func_def.params);
            for (int i = 0; i < node->as.func_def.body_count; i++)
                ast_free(node->as.func_def.body[i]);
            free(node->as.func_def.body);
            break;

        case NODE_FUNC_CALL:
            free(node->as.func_call.name);
            for (int i = 0; i < node->as.func_call.arg_count; i++)
                ast_free(node->as.func_call.args[i]);
            free(node->as.func_call.args);
            break;

        case NODE_RETURN:
            ast_free(node->as.return_val);
            break;

        case NODE_IMPORT:
            free(node->as.import_path);
            break;

        case NODE_TRY_CATCH:
            for (int i = 0; i < node->as.try_catch.try_count; i++)
                ast_free(node->as.try_catch.try_body[i]);
            free(node->as.try_catch.try_body);
            free(node->as.try_catch.catch_var);
            for (int i = 0; i < node->as.try_catch.catch_count; i++)
                ast_free(node->as.try_catch.catch_body[i]);
            free(node->as.try_catch.catch_body);
            break;

        case NODE_THROW:
            ast_free(node->as.throw_val);
            break;

        case NODE_CLASS:
            free(node->as.class_def.name);
            for (int i = 0; i < node->as.class_def.method_count; i++)
                ast_free(node->as.class_def.methods[i]);
            free(node->as.class_def.methods);
            break;

        case NODE_NEW:
            free(node->as.new_inst.class_name);
            for (int i = 0; i < node->as.new_inst.arg_count; i++)
                ast_free(node->as.new_inst.args[i]);
            free(node->as.new_inst.args);
            break;

        case NODE_ARRAY:
            for (int i = 0; i < node->as.array.count; i++)
                ast_free(node->as.array.elements[i]);
            free(node->as.array.elements);
            break;

        case NODE_ARRAY_INDEX:
            ast_free(node->as.array_index.array_expr);
            ast_free(node->as.array_index.index);
            break;

        case NODE_OBJECT:
            for (int i = 0; i < node->as.object.count; i++) {
                free(node->as.object.keys[i]);
                ast_free(node->as.object.values[i]);
            }
            free(node->as.object.keys);
            free(node->as.object.values);
            break;

        case NODE_OBJ_ACCESS:
            ast_free(node->as.obj_access.obj);
            free(node->as.obj_access.key);
            ast_free(node->as.obj_access.key_expr);
            break;

        case NODE_OBJ_ASSIGN:
            ast_free(node->as.obj_assign.obj);
            free(node->as.obj_assign.key);
            ast_free(node->as.obj_assign.key_expr);
            ast_free(node->as.obj_assign.value);
            break;

        case NODE_OBJ_COMPOUND_ASSIGN:
            ast_free(node->as.obj_comp_assign.obj);
            free(node->as.obj_comp_assign.key);
            ast_free(node->as.obj_comp_assign.key_expr);
            ast_free(node->as.obj_comp_assign.value);
            break;

        case NODE_TERNARY:
            ast_free(node->as.ternary.condition);
            ast_free(node->as.ternary.then_expr);
            ast_free(node->as.ternary.else_expr);
            break;

        case NODE_STRING_INTERP:
            for (int i = 0; i < node->as.interp.count; i++)
                ast_free(node->as.interp.parts[i]);
            free(node->as.interp.parts);
            break;

        case NODE_PROGRAM:
            for (int i = 0; i < node->as.program.count; i++)
                ast_free(node->as.program.stmts[i]);
            free(node->as.program.stmts);
            break;
    }
    free(node);
}
