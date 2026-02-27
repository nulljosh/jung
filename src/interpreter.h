#ifndef JUNG_INTERPRETER_H
#define JUNG_INTERPRETER_H

#include "parser.h"
#include "value.h"
#include "table.h"
#include <setjmp.h>

#define MAX_CALL_DEPTH 200
#define MAX_IMPORTED 64
#define MAX_SCOPES 256
#define MAX_TRY_DEPTH 32

typedef struct Scope {
    Table vars;
} Scope;

typedef struct Interpreter {
    Scope scopes[MAX_SCOPES];
    int scope_depth;
    Table globals;        /* global variables */
    Table functions;      /* user-defined functions (VAL_FUNCTION) */
    Table builtins;       /* builtin functions (VAL_BUILTIN) */
    Table classes;        /* class definitions (VAL_OBJECT with method table) */
    Value *this_obj;      /* current 'this' pointer for methods, NULL if none */
    int call_depth;
    char *imported[MAX_IMPORTED];
    int imported_count;
    int break_flag;
    int continue_flag;
    int return_flag;
    Value return_value;

    /* Exception handling */
    jmp_buf try_stack[MAX_TRY_DEPTH];
    int try_depth;
    char *exception_msg;  /* current exception message, NULL if none */
    int exception_active;
} Interpreter;

void  interp_init(Interpreter *it);
void  interp_free(Interpreter *it);
Value interp_eval(Interpreter *it, ASTNode *node);
void  interp_exec(Interpreter *it, ASTNode **stmts, int count);
void  interp_run(Interpreter *it, const char *source);

/* Variable lookup across scopes */
int   interp_get_var(Interpreter *it, const char *name, Value *out);
void  interp_set_var(Interpreter *it, const char *name, Value val);
void  interp_def_var(Interpreter *it, const char *name, Value val);

#endif
