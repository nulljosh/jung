#ifndef JUNG_VALUE_H
#define JUNG_VALUE_H

#include <stddef.h>

typedef enum {
    VAL_NULL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STRING,
    VAL_ARRAY,
    VAL_OBJECT,
    VAL_FUNCTION,
    VAL_BUILTIN
} ValueType;

typedef struct Value Value;
typedef struct Table Table;
typedef struct ASTNode ASTNode;

/* Function parameter: name + optional default expression */
typedef struct {
    char *name;
    ASTNode *default_val; /* NULL if no default */
} Param;

typedef struct {
    char *name;
    Param *params;
    int param_count;
    ASTNode **body;
    int body_count;
} FuncDef;

/* Builtin function pointer: receives array of Value, count, returns Value */
typedef Value (*BuiltinFn)(Value *args, int argc);

struct Value {
    ValueType type;
    int refcount;
    union {
        int boolean;
        double number;
        struct { char *str; int len; } string;
        struct { Value *items; int count; int cap; } array;
        Table *object;
        FuncDef *func;
        BuiltinFn builtin;
    } as;
};

/* Constructors -- all return stack values. Strings/arrays/objects are heap-backed. */
Value val_null(void);
Value val_bool(int b);
Value val_number(double n);
Value val_string(const char *s, int len);
Value val_string_take(char *s, int len);
Value val_array(int initial_cap);
Value val_object(void);
Value val_func(FuncDef *f);
Value val_builtin(BuiltinFn fn);

/* Reference counting */
Value val_copy(Value v);
void  val_free(Value *v);

/* Array helpers */
void  val_array_push(Value *arr, Value item);
Value val_array_pop(Value *arr);
Value val_array_get(Value *arr, int idx);
void  val_array_set(Value *arr, int idx, Value item);

/* Truthiness */
int   val_is_truthy(Value v);

/* Equality */
int   val_equal(Value a, Value b);

/* Format to string for printing */
char *val_to_string(Value v);

/* Type name */
const char *val_type_name(Value v);

#endif
