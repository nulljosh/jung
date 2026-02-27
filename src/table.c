#include "table.h"
#include <string.h>
#include <stdlib.h>

Table *table_new(void) {
    Table *t = calloc(1, sizeof(Table));
    t->capacity = 16;
    t->length = 0;
    t->keys = calloc(16, sizeof(char *));
    t->values = calloc(16, sizeof(Value *));
    return t;
}

void table_free(Table *t) {
    if (!t) return;
    for (int i = 0; i < t->length; i++) {
        free(t->keys[i]);
        value_release(t->values[i]);
    }
    free(t->keys);
    free(t->values);
    free(t);
}

Value *table_get(Table *t, const char *key) {
    for (int i = 0; i < t->length; i++) {
        if (strcmp(t->keys[i], key) == 0) {
            return t->values[i];
        }
    }
    return NULL;
}

void table_set(Table *t, const char *key, Value *val) {
    /* Update existing */
    for (int i = 0; i < t->length; i++) {
        if (strcmp(t->keys[i], key) == 0) {
            value_retain(val);
            value_release(t->values[i]);
            t->values[i] = val;
            return;
        }
    }

    /* New entry */
    if (t->length >= t->capacity) {
        t->capacity *= 2;
        t->keys = realloc(t->keys, t->capacity * sizeof(char *));
        t->values = realloc(t->values, t->capacity * sizeof(Value *));
    }
    t->keys[t->length] = strdup(key);
    value_retain(val);
    t->values[t->length] = val;
    t->length++;
}

int table_has(Table *t, const char *key) {
    for (int i = 0; i < t->length; i++) {
        if (strcmp(t->keys[i], key) == 0) return 1;
    }
    return 0;
}

void table_delete(Table *t, const char *key) {
    for (int i = 0; i < t->length; i++) {
        if (strcmp(t->keys[i], key) == 0) {
            free(t->keys[i]);
            value_release(t->values[i]);
            /* Shift remaining */
            for (int j = i; j < t->length - 1; j++) {
                t->keys[j] = t->keys[j + 1];
                t->values[j] = t->values[j + 1];
            }
            t->length--;
            return;
        }
    }
}

Table *table_copy(Table *t) {
    Table *copy = table_new();
    for (int i = 0; i < t->length; i++) {
        table_set(copy, t->keys[i], t->values[i]);
    }
    return copy;
}
