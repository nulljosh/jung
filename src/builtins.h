#ifndef JUNG_BUILTINS_H
#define JUNG_BUILTINS_H

#include "value.h"

/* Register all builtin functions into the interpreter */
struct Interpreter;
void builtins_register(struct Interpreter *it);

#endif
