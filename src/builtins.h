#ifndef JUNG_BUILTINS_H
#define JUNG_BUILTINS_H

#include "interpreter.h"

/* Returns 1 if name is a builtin, 0 otherwise.
   If it is, result is set to the return value. */
int builtin_call(Interpreter *interp, const char *name,
                 Value **args, int arg_count, ExecResult *result);

/* Object/string/array method dispatch via dot notation.
   Returns 1 if handled. */
int builtin_method(Interpreter *interp, Value *obj, const char *method,
                   Value **args, int arg_count, ExecResult *result);

#endif
