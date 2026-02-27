#ifndef JUNG_INTERPRETER_H
#define JUNG_INTERPRETER_H

#include "parser.h"
#include "value.h"
#include "table.h"

typedef enum {
    STATUS_OK,
    STATUS_RETURN,
    STATUS_BREAK,
    STATUS_CONTINUE,
    STATUS_THROW
} ExecStatus;

typedef struct {
    ExecStatus status;
    Value *value;
} ExecResult;

typedef struct Interpreter {
    Table *variables;
    Table *functions;  /* stores Value(VAL_FUNCTION) */
    Table *classes;    /* stores Value(VAL_OBJECT) with __methods__ */
    Value *current_instance;
    int call_depth;
    char *import_stack[32];
    int import_depth;
} Interpreter;

void interpreter_init(Interpreter *interp);
void interpreter_free(Interpreter *interp);

ExecResult exec_node(Interpreter *interp, ASTNode *node);
ExecResult exec_stmts(Interpreter *interp, ASTNode **stmts, int count);

/* Run a full source string (lex -> parse -> execute) */
int interpreter_run_source(Interpreter *interp, const char *source);

/* REPL */
void interpreter_repl(Interpreter *interp);

#endif
