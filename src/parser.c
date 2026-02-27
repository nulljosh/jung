#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parser_error(Parser *p, const char *msg) {
    if (p->pos < p->count) {
        fprintf(stderr, "Parse error at line %d, col %d: %s\n",
                p->tokens[p->pos].line, p->tokens[p->pos].col, msg);
    } else {
        fprintf(stderr, "Parse error: unexpected end of input: %s\n", msg);
    }
    exit(1);
}

static Token *peek(Parser *p) {
    if (p->pos < p->count) return &p->tokens[p->pos];
    return NULL;
}

static Token *peek_at(Parser *p, int offset) {
    int idx = p->pos + offset;
    if (idx >= 0 && idx < p->count) return &p->tokens[idx];
    return NULL;
}

static Token *advance(Parser *p) {
    if (p->pos >= p->count) {
        parser_error(p, "unexpected end of input");
    }
    return &p->tokens[p->pos++];
}

static int match(Parser *p, TokenType type) {
    Token *t = peek(p);
    return t && t->type == type;
}

static Token *consume(Parser *p, TokenType type, const char *msg) {
    if (!match(p, type)) parser_error(p, msg);
    return advance(p);
}

/* Forward declarations */
static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_statement(Parser *p);

static ASTNode *ast_alloc(NodeType type, int line) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    n->type = type;
    n->line = line;
    return n;
}

/* ---- Precedence climbing: primary -> postfix -> unary -> mul -> add -> cmp -> eq -> and -> or -> ternary ---- */

static ASTNode *parse_primary(Parser *p) {
    Token *t = peek(p);
    if (!t) parser_error(p, "expected expression");

    /* Unary not */
    if (t->type == TOKEN_NOT) {
        advance(p);
        ASTNode *operand = parse_primary(p);
        ASTNode *n = ast_alloc(NODE_UNARY_OP, t->line);
        n->as.unary.op = TOKEN_NOT;
        n->as.unary.operand = operand;
        return n;
    }

    /* Unary minus */
    if (t->type == TOKEN_MINUS) {
        advance(p);
        ASTNode *operand = parse_primary(p);
        ASTNode *n = ast_alloc(NODE_UNARY_OP, t->line);
        n->as.unary.op = TOKEN_MINUS;
        n->as.unary.operand = operand;
        return n;
    }

    /* Number */
    if (t->type == TOKEN_NUMBER) {
        advance(p);
        ASTNode *n = ast_alloc(NODE_NUMBER, t->line);
        n->as.number = atof(t->value);
        return n;
    }

    /* String */
    if (t->type == TOKEN_STRING) {
        advance(p);
        ASTNode *n = ast_alloc(NODE_STRING, t->line);
        n->as.string = strdup(t->value);
        return n;
    }

    /* Interpolated string */
    if (t->type == TOKEN_INTERP_BEGIN) {
        advance(p); /* skip INTERP_BEGIN */
        int cap = 8, cnt = 0;
        ASTNode **parts = calloc(cap, sizeof(ASTNode *));

        while (!match(p, TOKEN_INTERP_END)) {
            if (match(p, TOKEN_STRING)) {
                Token *st = advance(p);
                ASTNode *sn = ast_alloc(NODE_STRING, st->line);
                sn->as.string = strdup(st->value);
                if (cnt >= cap) { cap *= 2; parts = realloc(parts, cap * sizeof(ASTNode *)); }
                parts[cnt++] = sn;
            } else {
                ASTNode *expr = parse_expression(p);
                if (cnt >= cap) { cap *= 2; parts = realloc(parts, cap * sizeof(ASTNode *)); }
                parts[cnt++] = expr;
            }
        }
        consume(p, TOKEN_INTERP_END, "expected end of interpolated string");
        ASTNode *n = ast_alloc(NODE_STRING_INTERP, t->line);
        n->as.interp.parts = parts;
        n->as.interp.count = cnt;
        return n;
    }

    /* Boolean true */
    if (t->type == TOKEN_TRUE) {
        advance(p);
        ASTNode *n = ast_alloc(NODE_BOOL, t->line);
        n->as.boolean = 1;
        return n;
    }

    /* Boolean false */
    if (t->type == TOKEN_FALSE) {
        advance(p);
        ASTNode *n = ast_alloc(NODE_BOOL, t->line);
        n->as.boolean = 0;
        return n;
    }

    /* null */
    if (t->type == TOKEN_NULL) {
        advance(p);
        return ast_alloc(NODE_NULL, t->line);
    }

    /* this */
    if (t->type == TOKEN_THIS) {
        advance(p);
        return ast_alloc(NODE_THIS, t->line);
    }

    /* new ClassName(args) */
    if (t->type == TOKEN_NEW) {
        advance(p);
        Token *cls = consume(p, TOKEN_IDENTIFIER, "expected class name after 'new'");
        consume(p, TOKEN_LPAREN, "expected '(' after class name");
        int acap = 4, acnt = 0;
        ASTNode **args = calloc(acap, sizeof(ASTNode *));
        if (!match(p, TOKEN_RPAREN)) {
            args[acnt++] = parse_expression(p);
            while (match(p, TOKEN_COMMA)) {
                advance(p);
                if (acnt >= acap) { acap *= 2; args = realloc(args, acap * sizeof(ASTNode *)); }
                args[acnt++] = parse_expression(p);
            }
        }
        consume(p, TOKEN_RPAREN, "expected ')' after arguments");
        ASTNode *n = ast_alloc(NODE_NEW_INSTANCE, t->line);
        n->as.new_inst.class_name = strdup(cls->value);
        n->as.new_inst.args = args;
        n->as.new_inst.arg_count = acnt;
        return n;
    }

    /* Identifier or function call */
    if (t->type == TOKEN_IDENTIFIER) {
        Token *id = advance(p);
        /* Function call */
        if (match(p, TOKEN_LPAREN)) {
            advance(p);
            int acap = 4, acnt = 0;
            ASTNode **args = calloc(acap, sizeof(ASTNode *));
            if (!match(p, TOKEN_RPAREN)) {
                args[acnt++] = parse_expression(p);
                while (match(p, TOKEN_COMMA)) {
                    advance(p);
                    if (acnt >= acap) { acap *= 2; args = realloc(args, acap * sizeof(ASTNode *)); }
                    args[acnt++] = parse_expression(p);
                }
            }
            consume(p, TOKEN_RPAREN, "expected ')' after arguments");
            ASTNode *n = ast_alloc(NODE_FUNCTION_CALL, id->line);
            n->as.func_call.name = strdup(id->value);
            n->as.func_call.args = args;
            n->as.func_call.arg_count = acnt;
            return n;
        }
        ASTNode *n = ast_alloc(NODE_VARIABLE, id->line);
        n->as.var_name = strdup(id->value);
        return n;
    }

    /* Array literal */
    if (t->type == TOKEN_LBRACKET) {
        advance(p);
        int ecap = 8, ecnt = 0;
        ASTNode **elems = calloc(ecap, sizeof(ASTNode *));
        if (!match(p, TOKEN_RBRACKET)) {
            elems[ecnt++] = parse_expression(p);
            while (match(p, TOKEN_COMMA)) {
                advance(p);
                if (match(p, TOKEN_RBRACKET)) break; /* trailing comma */
                if (ecnt >= ecap) { ecap *= 2; elems = realloc(elems, ecap * sizeof(ASTNode *)); }
                elems[ecnt++] = parse_expression(p);
            }
        }
        consume(p, TOKEN_RBRACKET, "expected ']'");
        ASTNode *n = ast_alloc(NODE_ARRAY, t->line);
        n->as.array.elements = elems;
        n->as.array.count = ecnt;
        return n;
    }

    /* Object literal */
    if (t->type == TOKEN_LBRACE) {
        advance(p);
        int ocap = 8, ocnt = 0;
        char **keys = calloc(ocap, sizeof(char *));
        ASTNode **vals = calloc(ocap, sizeof(ASTNode *));
        if (!match(p, TOKEN_RBRACE)) {
            Token *k = consume(p, TOKEN_IDENTIFIER, "expected property name");
            consume(p, TOKEN_COLON, "expected ':' after property name");
            ASTNode *v = parse_expression(p);
            keys[ocnt] = strdup(k->value);
            vals[ocnt] = v;
            ocnt++;
            while (match(p, TOKEN_COMMA)) {
                advance(p);
                if (match(p, TOKEN_RBRACE)) break; /* trailing comma */
                if (ocnt >= ocap) {
                    ocap *= 2;
                    keys = realloc(keys, ocap * sizeof(char *));
                    vals = realloc(vals, ocap * sizeof(ASTNode *));
                }
                k = consume(p, TOKEN_IDENTIFIER, "expected property name");
                consume(p, TOKEN_COLON, "expected ':' after property name");
                v = parse_expression(p);
                keys[ocnt] = strdup(k->value);
                vals[ocnt] = v;
                ocnt++;
            }
        }
        consume(p, TOKEN_RBRACE, "expected '}'");
        ASTNode *n = ast_alloc(NODE_OBJECT, t->line);
        n->as.object.keys = keys;
        n->as.object.values = vals;
        n->as.object.count = ocnt;
        return n;
    }

    /* Parenthesized expression */
    if (t->type == TOKEN_LPAREN) {
        advance(p);
        ASTNode *expr = parse_expression(p);
        consume(p, TOKEN_RPAREN, "expected ')' after expression");
        return expr;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "expected expression, got %s", token_type_name(t->type));
    parser_error(p, msg);
    return NULL; /* unreachable */
}

/* Postfix: array index, dot access, method call */
static ASTNode *parse_postfix(Parser *p) {
    ASTNode *left = parse_primary(p);

    while (1) {
        /* Array indexing / bracket access */
        if (match(p, TOKEN_LBRACKET)) {
            int line = peek(p)->line;
            advance(p);
            ASTNode *idx = parse_expression(p);
            consume(p, TOKEN_RBRACKET, "expected ']'");
            ASTNode *n = ast_alloc(NODE_INDEX, line);
            n->as.index.object = left;
            n->as.index.index = idx;
            left = n;
            continue;
        }

        /* Dot access / method call */
        if (match(p, TOKEN_DOT)) {
            int line = peek(p)->line;
            advance(p);
            Token *field = consume(p, TOKEN_IDENTIFIER, "expected property name after '.'");

            if (match(p, TOKEN_LPAREN)) {
                /* Method call: obj.method(args) */
                advance(p);
                int acap = 4, acnt = 0;
                ASTNode **args = calloc(acap, sizeof(ASTNode *));
                if (!match(p, TOKEN_RPAREN)) {
                    args[acnt++] = parse_expression(p);
                    while (match(p, TOKEN_COMMA)) {
                        advance(p);
                        if (acnt >= acap) { acap *= 2; args = realloc(args, acap * sizeof(ASTNode *)); }
                        args[acnt++] = parse_expression(p);
                    }
                }
                consume(p, TOKEN_RPAREN, "expected ')' after arguments");
                ASTNode *n = ast_alloc(NODE_METHOD_CALL, line);
                n->as.method.object = left;
                n->as.method.method = strdup(field->value);
                n->as.method.args = args;
                n->as.method.arg_count = acnt;
                left = n;
            } else {
                /* Property access: obj.field */
                ASTNode *n = ast_alloc(NODE_DOT_ACCESS, line);
                n->as.dot.object = left;
                n->as.dot.field = strdup(field->value);
                left = n;
            }
            continue;
        }

        break;
    }
    return left;
}

static ASTNode *parse_multiplication(Parser *p) {
    ASTNode *left = parse_postfix(p);
    while (match(p, TOKEN_MULTIPLY) || match(p, TOKEN_DIVIDE) || match(p, TOKEN_MODULO)) {
        Token *op = advance(p);
        ASTNode *right = parse_postfix(p);
        ASTNode *n = ast_alloc(NODE_BINARY_OP, op->line);
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
        Token *op = advance(p);
        ASTNode *right = parse_multiplication(p);
        ASTNode *n = ast_alloc(NODE_BINARY_OP, op->line);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = op->type;
        left = n;
    }
    return left;
}

static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_addition(p);
    while (match(p, TOKEN_GT) || match(p, TOKEN_LT) || match(p, TOKEN_GTE) || match(p, TOKEN_LTE)) {
        Token *op = advance(p);
        ASTNode *right = parse_addition(p);
        ASTNode *n = ast_alloc(NODE_BINARY_OP, op->line);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = op->type;
        left = n;
    }
    return left;
}

static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_comparison(p);
    while (match(p, TOKEN_EQ) || match(p, TOKEN_NEQ)) {
        Token *op = advance(p);
        ASTNode *right = parse_comparison(p);
        ASTNode *n = ast_alloc(NODE_BINARY_OP, op->line);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = op->type;
        left = n;
    }
    return left;
}

static ASTNode *parse_and(Parser *p) {
    ASTNode *left = parse_equality(p);
    while (match(p, TOKEN_AND)) {
        Token *op = advance(p);
        ASTNode *right = parse_equality(p);
        ASTNode *n = ast_alloc(NODE_BINARY_OP, op->line);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = op->type;
        left = n;
    }
    return left;
}

static ASTNode *parse_or(Parser *p) {
    ASTNode *left = parse_and(p);
    while (match(p, TOKEN_OR)) {
        Token *op = advance(p);
        ASTNode *right = parse_and(p);
        ASTNode *n = ast_alloc(NODE_BINARY_OP, op->line);
        n->as.binary.left = left;
        n->as.binary.right = right;
        n->as.binary.op = op->type;
        left = n;
    }
    return left;
}

static ASTNode *parse_expression(Parser *p) {
    ASTNode *expr = parse_or(p);
    /* Ternary */
    if (match(p, TOKEN_QUESTION)) {
        int line = peek(p)->line;
        advance(p);
        ASTNode *then_e = parse_expression(p);
        consume(p, TOKEN_COLON, "expected ':' in ternary");
        ASTNode *else_e = parse_expression(p);
        ASTNode *n = ast_alloc(NODE_TERNARY, line);
        n->as.ternary.cond = expr;
        n->as.ternary.then_expr = then_e;
        n->as.ternary.else_expr = else_e;
        return n;
    }
    return expr;
}

/* Parse a { ... } block returning array of statements */
static void parse_block(Parser *p, ASTNode ***out_stmts, int *out_count) {
    consume(p, TOKEN_LBRACE, "expected '{'");
    int cap = 16, cnt = 0;
    ASTNode **stmts = calloc(cap, sizeof(ASTNode *));
    while (!match(p, TOKEN_RBRACE) && !match(p, TOKEN_EOF)) {
        ASTNode *s = parse_statement(p);
        if (s) {
            if (cnt >= cap) { cap *= 2; stmts = realloc(stmts, cap * sizeof(ASTNode *)); }
            stmts[cnt++] = s;
        }
    }
    consume(p, TOKEN_RBRACE, "expected '}'");
    *out_stmts = stmts;
    *out_count = cnt;
}

static ASTNode *parse_statement(Parser *p) {
    Token *t = peek(p);
    if (!t || t->type == TOKEN_EOF) return NULL;

    /* class */
    if (t->type == TOKEN_CLASS) {
        advance(p);
        Token *name = consume(p, TOKEN_IDENTIFIER, "expected class name");
        consume(p, TOKEN_LBRACE, "expected '{' after class name");

        int mcap = 8, mcnt = 0;
        ASTNode **methods = calloc(mcap, sizeof(ASTNode *));
        while (!match(p, TOKEN_RBRACE)) {
            consume(p, TOKEN_FN, "expected method definition in class");
            Token *mname = consume(p, TOKEN_IDENTIFIER, "expected method name");
            consume(p, TOKEN_LPAREN, "expected '(' after method name");
            int pcap = 4, pcnt = 0;
            char **params = calloc(pcap, sizeof(char *));
            if (!match(p, TOKEN_RPAREN)) {
                Token *pn = consume(p, TOKEN_IDENTIFIER, "expected parameter name");
                params[pcnt++] = strdup(pn->value);
                while (match(p, TOKEN_COMMA)) {
                    advance(p);
                    if (pcnt >= pcap) { pcap *= 2; params = realloc(params, pcap * sizeof(char *)); }
                    pn = consume(p, TOKEN_IDENTIFIER, "expected parameter name");
                    params[pcnt++] = strdup(pn->value);
                }
            }
            consume(p, TOKEN_RPAREN, "expected ')' after parameters");
            ASTNode **body; int bcnt;
            parse_block(p, &body, &bcnt);

            ASTNode *m = ast_alloc(NODE_FUNCTION_DEF, mname->line);
            m->as.func_def.name = strdup(mname->value);
            m->as.func_def.params = params;
            m->as.func_def.param_count = pcnt;
            m->as.func_def.body = body;
            m->as.func_def.body_count = bcnt;
            if (mcnt >= mcap) { mcap *= 2; methods = realloc(methods, mcap * sizeof(ASTNode *)); }
            methods[mcnt++] = m;
        }
        consume(p, TOKEN_RBRACE, "expected '}' after class body");

        ASTNode *n = ast_alloc(NODE_CLASS_DEF, name->line);
        n->as.class_def.name = strdup(name->value);
        n->as.class_def.methods = methods;
        n->as.class_def.method_count = mcnt;
        return n;
    }

    /* fn */
    if (t->type == TOKEN_FN) {
        advance(p);
        Token *name = consume(p, TOKEN_IDENTIFIER, "expected function name");
        consume(p, TOKEN_LPAREN, "expected '(' after function name");
        int pcap = 4, pcnt = 0;
        char **params = calloc(pcap, sizeof(char *));
        if (!match(p, TOKEN_RPAREN)) {
            Token *pn = consume(p, TOKEN_IDENTIFIER, "expected parameter name");
            params[pcnt++] = strdup(pn->value);
            while (match(p, TOKEN_COMMA)) {
                advance(p);
                if (pcnt >= pcap) { pcap *= 2; params = realloc(params, pcap * sizeof(char *)); }
                pn = consume(p, TOKEN_IDENTIFIER, "expected parameter name");
                params[pcnt++] = strdup(pn->value);
            }
        }
        consume(p, TOKEN_RPAREN, "expected ')' after parameters");
        ASTNode **body; int bcnt;
        parse_block(p, &body, &bcnt);

        ASTNode *n = ast_alloc(NODE_FUNCTION_DEF, name->line);
        n->as.func_def.name = strdup(name->value);
        n->as.func_def.params = params;
        n->as.func_def.param_count = pcnt;
        n->as.func_def.body = body;
        n->as.func_def.body_count = bcnt;
        return n;
    }

    /* return */
    if (t->type == TOKEN_RETURN) {
        advance(p);
        ASTNode *val = NULL;
        if (!match(p, TOKEN_SEMICOLON)) {
            val = parse_expression(p);
        }
        consume(p, TOKEN_SEMICOLON, "expected ';' after return");
        ASTNode *n = ast_alloc(NODE_RETURN, t->line);
        n->as.ret.value = val;
        return n;
    }

    /* break */
    if (t->type == TOKEN_BREAK) {
        advance(p);
        consume(p, TOKEN_SEMICOLON, "expected ';' after break");
        return ast_alloc(NODE_BREAK, t->line);
    }

    /* continue */
    if (t->type == TOKEN_CONTINUE) {
        advance(p);
        consume(p, TOKEN_SEMICOLON, "expected ';' after continue");
        return ast_alloc(NODE_CONTINUE, t->line);
    }

    /* import */
    if (t->type == TOKEN_IMPORT) {
        advance(p);
        Token *path = consume(p, TOKEN_STRING, "expected string path after import");
        consume(p, TOKEN_SEMICOLON, "expected ';' after import");
        ASTNode *n = ast_alloc(NODE_IMPORT, t->line);
        n->as.import_stmt.path = strdup(path->value);
        return n;
    }

    /* try/catch */
    if (t->type == TOKEN_TRY) {
        advance(p);
        ASTNode **try_stmts; int try_cnt;
        parse_block(p, &try_stmts, &try_cnt);
        consume(p, TOKEN_CATCH, "expected 'catch' after try block");
        char *catch_var = NULL;
        /* Catch variable: either "catch e" or "catch (e)" */
        if (match(p, TOKEN_LPAREN)) {
            advance(p);
            Token *cv = consume(p, TOKEN_IDENTIFIER, "expected variable in catch");
            catch_var = strdup(cv->value);
            consume(p, TOKEN_RPAREN, "expected ')' after catch variable");
        } else if (match(p, TOKEN_IDENTIFIER)) {
            Token *cv = advance(p);
            catch_var = strdup(cv->value);
        }
        ASTNode **catch_stmts; int catch_cnt;
        parse_block(p, &catch_stmts, &catch_cnt);

        ASTNode *n = ast_alloc(NODE_TRY_CATCH, t->line);
        n->as.try_catch.try_stmts = try_stmts;
        n->as.try_catch.try_count = try_cnt;
        n->as.try_catch.catch_var = catch_var;
        n->as.try_catch.catch_stmts = catch_stmts;
        n->as.try_catch.catch_count = catch_cnt;
        return n;
    }

    /* throw */
    if (t->type == TOKEN_THROW) {
        advance(p);
        ASTNode *val = parse_expression(p);
        consume(p, TOKEN_SEMICOLON, "expected ';' after throw");
        ASTNode *n = ast_alloc(NODE_THROW, t->line);
        n->as.throw_stmt.value = val;
        return n;
    }

    /* if / else if / else */
    if (t->type == TOKEN_IF) {
        advance(p);
        ASTNode *cond = parse_expression(p);
        ASTNode **then_stmts; int then_cnt;
        parse_block(p, &then_stmts, &then_cnt);

        ASTNode **else_stmts = NULL;
        int else_cnt = 0;

        if (match(p, TOKEN_ELSE)) {
            advance(p);
            if (match(p, TOKEN_IF)) {
                /* else if -> recursive, wrap in single-element else block */
                ASTNode *elif = parse_statement(p);
                else_stmts = calloc(1, sizeof(ASTNode *));
                else_stmts[0] = elif;
                else_cnt = 1;
            } else {
                parse_block(p, &else_stmts, &else_cnt);
            }
        }

        ASTNode *n = ast_alloc(NODE_IF, t->line);
        n->as.if_stmt.cond = cond;
        n->as.if_stmt.then_stmts = then_stmts;
        n->as.if_stmt.then_count = then_cnt;
        n->as.if_stmt.else_stmts = else_stmts;
        n->as.if_stmt.else_count = else_cnt;
        return n;
    }

    /* while */
    if (t->type == TOKEN_WHILE) {
        advance(p);
        ASTNode *cond = parse_expression(p);
        ASTNode **body; int bcnt;
        parse_block(p, &body, &bcnt);
        ASTNode *n = ast_alloc(NODE_WHILE, t->line);
        n->as.while_loop.cond = cond;
        n->as.while_loop.body = body;
        n->as.while_loop.body_count = bcnt;
        return n;
    }

    /* for */
    if (t->type == TOKEN_FOR) {
        advance(p);
        Token *var = consume(p, TOKEN_IDENTIFIER, "expected variable name in for");
        consume(p, TOKEN_IN, "expected 'in'");
        ASTNode *iter = parse_expression(p);
        ASTNode **body; int bcnt;
        parse_block(p, &body, &bcnt);
        ASTNode *n = ast_alloc(NODE_FOR, t->line);
        n->as.for_loop.var = strdup(var->value);
        n->as.for_loop.iterable = iter;
        n->as.for_loop.body = body;
        n->as.for_loop.body_count = bcnt;
        return n;
    }

    /* let */
    if (t->type == TOKEN_LET) {
        advance(p);
        Token *name = consume(p, TOKEN_IDENTIFIER, "expected variable name");
        consume(p, TOKEN_ASSIGN, "expected '=' in assignment");
        ASTNode *val = parse_expression(p);
        consume(p, TOKEN_SEMICOLON, "expected ';' after assignment");
        ASTNode *n = ast_alloc(NODE_ASSIGNMENT, t->line);
        n->as.assign.name = strdup(name->value);
        n->as.assign.value = val;
        return n;
    }

    /* print */
    if (t->type == TOKEN_PRINT) {
        advance(p);
        ASTNode *expr = parse_expression(p);
        consume(p, TOKEN_SEMICOLON, "expected ';' after print");
        ASTNode *n = ast_alloc(NODE_PRINT, t->line);
        n->as.print_stmt.expr = expr;
        return n;
    }

    /* Reassignment: identifier = expr; or identifier += expr; etc. */
    if (t->type == TOKEN_IDENTIFIER) {
        Token *next = peek_at(p, 1);
        if (next) {
            /* Plain assignment */
            if (next->type == TOKEN_ASSIGN) {
                Token *id = advance(p);
                advance(p); /* consume '=' */
                ASTNode *val = parse_expression(p);
                consume(p, TOKEN_SEMICOLON, "expected ';' after assignment");
                ASTNode *n = ast_alloc(NODE_ASSIGNMENT, id->line);
                n->as.assign.name = strdup(id->value);
                n->as.assign.value = val;
                return n;
            }
            /* Compound assignment */
            if (next->type == TOKEN_PLUS_ASSIGN || next->type == TOKEN_MINUS_ASSIGN ||
                next->type == TOKEN_MULTIPLY_ASSIGN || next->type == TOKEN_DIVIDE_ASSIGN) {
                Token *id = advance(p);
                Token *op = advance(p);
                ASTNode *val = parse_expression(p);
                consume(p, TOKEN_SEMICOLON, "expected ';' after compound assignment");
                ASTNode *n = ast_alloc(NODE_COMPOUND_ASSIGN, id->line);
                n->as.compound.name = strdup(id->value);
                n->as.compound.op = op->type;
                n->as.compound.value = val;
                return n;
            }
        }
    }

    /* Expression statement (may be dot_assign, index_assign, or plain expr) */
    if (!match(p, TOKEN_EOF)) {
        ASTNode *expr = parse_expression(p);

        /* Check if it's an assignment to dot/index target */
        if (match(p, TOKEN_ASSIGN)) {
            advance(p);
            ASTNode *val = parse_expression(p);
            consume(p, TOKEN_SEMICOLON, "expected ';' after assignment");

            if (expr->type == NODE_DOT_ACCESS) {
                ASTNode *n = ast_alloc(NODE_DOT_ASSIGN, expr->line);
                n->as.dot_assign.object = expr->as.dot.object;
                n->as.dot_assign.field = expr->as.dot.field;
                n->as.dot_assign.value = val;
                n->as.dot_assign.is_bracket = 0;
                /* Don't free expr -- we stole its children */
                free(expr);
                return n;
            }
            if (expr->type == NODE_INDEX) {
                ASTNode *n = ast_alloc(NODE_DOT_ASSIGN, expr->line);
                n->as.dot_assign.object = expr->as.index.object;
                n->as.dot_assign.field = NULL;
                n->as.dot_assign.value = val;
                n->as.dot_assign.is_bracket = 1;
                /* Stash index expression in the object's place - need a hack.
                   Use the unused field pointer for the index expression. Actually,
                   we need both the object and the index. Let me restructure. */
                /* Actually for bracket assigns like obj["key"] = val or arr[0] = val,
                   we'll use a different approach: store in dot_assign with is_bracket=1,
                   and store the index node as a field pointer. We need to extend the
                   union or use a different approach. Simplest: just re-use the NODE_DOT_ASSIGN
                   with a creative mapping.

                   For bracket assignment:
                   - dot_assign.object = the container
                   - dot_assign.field = NULL (indicates bracket)
                   - dot_assign.value = the RHS value
                   - dot_assign.is_bracket = 1

                   But we lose the index expression... We need to store it somewhere.
                   The cleanest solution: add an extra member. But the union is already defined.

                   Alternative: put the index expression in the value temporarily and
                   wrap in a special node. That's ugly.

                   Best approach: use a wrapper node. Let's store the index as the dot_assign's
                   "object" being a NODE_INDEX that we partially decompose in the interpreter.

                   Actually simplest: We'll create a NODE_DOT_ASSIGN where:
                   - object = the container (arr, obj)
                   - field = NULL
                   - value = the RHS
                   - is_bracket = 1
                   And we need one more field for the index expr.

                   Since the union member dot_assign already has {object, field, value, is_bracket},
                   and field is char*, we can't store an ASTNode* there directly.

                   Hack: use a wrapper. We'll wrap the container+index in an INDEX node and
                   let the interpreter decompose it. */
                n->as.dot_assign.object = expr->as.index.object;
                /* Store index expression... we need a place. Let's use a temporary trick:
                   put the index expr at dot_assign.value and the actual value in a binary node.
                   No, that's terrible.

                   Cleanest fix: add a 'bracket_index' field to dot_assign. But the header is
                   already defined with a specific layout. Let me re-read the header... */
                /* Actually, I can re-define the union to have a separate bracket_assign member,
                   or more practically, I'll just create the INDEX node as a wrapper and
                   let the interpreter handle it.

                   Even simpler: Just keep the expr as NODE_INDEX and tag it specially.
                   The interpreter can check: if the statement is a NODE_INDEX followed by
                   assignment... but we already consumed the assign.

                   OK, most pragmatic solution: for bracket assignment, we create a
                   NODE_DOT_ASSIGN where object = the INDEX node itself. The interpreter
                   checks is_bracket and decomposes the INDEX node. */
                free(n);
                n = ast_alloc(NODE_DOT_ASSIGN, expr->line);
                n->as.dot_assign.object = expr; /* the INDEX node */
                n->as.dot_assign.field = NULL;
                n->as.dot_assign.value = val;
                n->as.dot_assign.is_bracket = 1;
                return n;
            }

            /* Assignment to simple variable via expression (shouldn't normally happen) */
            parser_error(p, "invalid assignment target");
        }

        consume(p, TOKEN_SEMICOLON, "expected ';' after expression");
        return expr;
    }

    return NULL;
}

/* ---- Public API ---- */

void parser_init(Parser *p, Token *tokens, int count) {
    p->tokens = tokens;
    p->count = count;
    p->pos = 0;
}

ASTNode *parser_parse(Parser *p) {
    int cap = 32, cnt = 0;
    ASTNode **stmts = calloc(cap, sizeof(ASTNode *));

    while (!match(p, TOKEN_EOF)) {
        ASTNode *s = parse_statement(p);
        if (s) {
            if (cnt >= cap) { cap *= 2; stmts = realloc(stmts, cap * sizeof(ASTNode *)); }
            stmts[cnt++] = s;
        }
    }

    ASTNode *prog = ast_alloc(NODE_PROGRAM, 1);
    prog->as.program.stmts = stmts;
    prog->as.program.count = cnt;
    return prog;
}

void ast_free(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
    case NODE_STRING:
        free(node->as.string);
        break;
    case NODE_VARIABLE:
        free(node->as.var_name);
        break;
    case NODE_ARRAY:
        for (int i = 0; i < node->as.array.count; i++)
            ast_free(node->as.array.elements[i]);
        free(node->as.array.elements);
        break;
    case NODE_OBJECT:
        for (int i = 0; i < node->as.object.count; i++) {
            free(node->as.object.keys[i]);
            ast_free(node->as.object.values[i]);
        }
        free(node->as.object.keys);
        free(node->as.object.values);
        break;
    case NODE_BINARY_OP:
        ast_free(node->as.binary.left);
        ast_free(node->as.binary.right);
        break;
    case NODE_UNARY_OP:
        ast_free(node->as.unary.operand);
        break;
    case NODE_ASSIGNMENT:
        free(node->as.assign.name);
        ast_free(node->as.assign.value);
        break;
    case NODE_COMPOUND_ASSIGN:
        free(node->as.compound.name);
        ast_free(node->as.compound.value);
        break;
    case NODE_TERNARY:
        ast_free(node->as.ternary.cond);
        ast_free(node->as.ternary.then_expr);
        ast_free(node->as.ternary.else_expr);
        break;
    case NODE_STRING_INTERP:
        for (int i = 0; i < node->as.interp.count; i++)
            ast_free(node->as.interp.parts[i]);
        free(node->as.interp.parts);
        break;
    case NODE_INDEX:
        ast_free(node->as.index.object);
        ast_free(node->as.index.index);
        break;
    case NODE_DOT_ACCESS:
        ast_free(node->as.dot.object);
        free(node->as.dot.field);
        break;
    case NODE_DOT_ASSIGN:
        ast_free(node->as.dot_assign.object);
        free(node->as.dot_assign.field);
        ast_free(node->as.dot_assign.value);
        break;
    case NODE_METHOD_CALL:
        ast_free(node->as.method.object);
        free(node->as.method.method);
        for (int i = 0; i < node->as.method.arg_count; i++)
            ast_free(node->as.method.args[i]);
        free(node->as.method.args);
        break;
    case NODE_IF:
        ast_free(node->as.if_stmt.cond);
        for (int i = 0; i < node->as.if_stmt.then_count; i++)
            ast_free(node->as.if_stmt.then_stmts[i]);
        free(node->as.if_stmt.then_stmts);
        for (int i = 0; i < node->as.if_stmt.else_count; i++)
            ast_free(node->as.if_stmt.else_stmts[i]);
        free(node->as.if_stmt.else_stmts);
        break;
    case NODE_WHILE:
        ast_free(node->as.while_loop.cond);
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
    case NODE_FUNCTION_DEF:
        free(node->as.func_def.name);
        for (int i = 0; i < node->as.func_def.param_count; i++)
            free(node->as.func_def.params[i]);
        free(node->as.func_def.params);
        for (int i = 0; i < node->as.func_def.body_count; i++)
            ast_free(node->as.func_def.body[i]);
        free(node->as.func_def.body);
        break;
    case NODE_FUNCTION_CALL:
        free(node->as.func_call.name);
        for (int i = 0; i < node->as.func_call.arg_count; i++)
            ast_free(node->as.func_call.args[i]);
        free(node->as.func_call.args);
        break;
    case NODE_RETURN:
        ast_free(node->as.ret.value);
        break;
    case NODE_CLASS_DEF:
        free(node->as.class_def.name);
        for (int i = 0; i < node->as.class_def.method_count; i++)
            ast_free(node->as.class_def.methods[i]);
        free(node->as.class_def.methods);
        break;
    case NODE_NEW_INSTANCE:
        free(node->as.new_inst.class_name);
        for (int i = 0; i < node->as.new_inst.arg_count; i++)
            ast_free(node->as.new_inst.args[i]);
        free(node->as.new_inst.args);
        break;
    case NODE_TRY_CATCH:
        for (int i = 0; i < node->as.try_catch.try_count; i++)
            ast_free(node->as.try_catch.try_stmts[i]);
        free(node->as.try_catch.try_stmts);
        free(node->as.try_catch.catch_var);
        for (int i = 0; i < node->as.try_catch.catch_count; i++)
            ast_free(node->as.try_catch.catch_stmts[i]);
        free(node->as.try_catch.catch_stmts);
        break;
    case NODE_THROW:
        ast_free(node->as.throw_stmt.value);
        break;
    case NODE_PRINT:
        ast_free(node->as.print_stmt.expr);
        break;
    case NODE_IMPORT:
        free(node->as.import_stmt.path);
        break;
    case NODE_PROGRAM:
        for (int i = 0; i < node->as.program.count; i++)
            ast_free(node->as.program.stmts[i]);
        free(node->as.program.stmts);
        break;
    default:
        break;
    }
    free(node);
}
