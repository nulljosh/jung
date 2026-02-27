#ifndef JUNG_VALUE_H
#define JUNG_VALUE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef enum {
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
    VAL_NULL,
    VAL_ARRAY,
    VAL_OBJECT,
    VAL_FUNCTION
} ValueType;

/* Forward declare ASTNode */
struct ASTNode;

typedef struct Value {
    ValueType type;
    int refcount;
    union {
        double number;
        char *string;
        int boolean;
        struct {
            struct Value **items;
            int length;
            int capacity;
        } array;
        struct {
            char **keys;
            struct Value **values;
            int length;
            int capacity;
        } object;
        struct {
            char *name;
            char **params;
            int param_count;
            struct ASTNode *body;     /* NODE_PROGRAM or block body */
            int body_count;           /* number of statements in body */
            struct ASTNode **body_stmts; /* array of statement pointers */
        } function;
    } as;
} Value;

Value *value_new_number(double n);
Value *value_new_string(const char *s);
Value *value_new_string_owned(char *s);  /* takes ownership, no copy */
Value *value_new_bool(int b);
Value *value_new_null(void);
Value *value_new_array(void);
Value *value_new_object(void);
Value *value_new_function(const char *name, char **params, int param_count,
                          struct ASTNode **body_stmts, int body_count);

void value_retain(Value *v);
void value_release(Value *v);

char *value_to_string(Value *v);
int value_is_truthy(Value *v);
int value_equals(Value *a, Value *b);

/* Array operations */
void value_array_push(Value *arr, Value *item);
Value *value_array_pop(Value *arr);
Value *value_array_get(Value *arr, int index);
void value_array_set(Value *arr, int index, Value *item);

/* Object operations */
void value_object_set(Value *obj, const char *key, Value *val);
Value *value_object_get(Value *obj, const char *key);
int value_object_has(Value *obj, const char *key);

/* JSON */
char *value_to_json(Value *v);
Value *value_from_json(const char *json, int *pos);

#endif
