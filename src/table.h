#ifndef JUNG_TABLE_H
#define JUNG_TABLE_H

#include "value.h"

typedef struct TableEntry {
    char *key;
    Value value;
    struct TableEntry *next;
} TableEntry;

struct Table {
    TableEntry **entries;
    int cap;
    int count;
    int refcount;
};

void  table_init(Table *t);
void  table_free(Table *t);
void  table_set(Table *t, const char *key, Value val);
int   table_get(Table *t, const char *key, Value *out);
int   table_has(Table *t, const char *key);
void  table_delete(Table *t, const char *key);

/* Iteration: collect all keys into a Value array */
Value table_keys(Table *t);
Value table_values(Table *t);

#endif
