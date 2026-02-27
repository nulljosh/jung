#ifndef JUNG_TABLE_H
#define JUNG_TABLE_H

#include "value.h"

typedef struct {
    char **keys;
    Value **values;
    int length;
    int capacity;
} Table;

Table *table_new(void);
void table_free(Table *t);
Value *table_get(Table *t, const char *key);
void table_set(Table *t, const char *key, Value *val);
int table_has(Table *t, const char *key);
void table_delete(Table *t, const char *key);
Table *table_copy(Table *t);

#endif
